// SPDX-License-Identifier: GPL-3.0-or-later
#include "https_request.h"
#include <sys/time.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// curl_global_init runs on the main thread before any workers start.
// Every request owns its easy handle; no lock spans DNS/TLS/network I/O.
typedef struct {wili_https_response *r;int (*cancel)(void*);void *arg;size_t headers;} request;
static size_t write_body(char *data,size_t size,size_t count,void *arg)
{
    request *q=arg;if(size && count>SIZE_MAX/size)return 0;
    size_t n=size*count;
    if(q->r->bytes>q->r->capacity || n>q->r->capacity-q->r->bytes){q->r->overflow=1;return 0;}
    memcpy(q->r->body+q->r->bytes,data,n);q->r->bytes+=n;q->r->body[q->r->bytes]=0;return n;
}
static int begins(const char *a,const char *b)
{
    while(*b){unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;if(x>='A' && x<='Z')x+=32;if(y>='A' && y<='Z')y+=32;if(x!=y)return 0;}return 1;
}
static int decimal(const char **p,uint64_t *out)
{
    uint64_t n=0;unsigned digits=0;
    while(**p>='0' && **p<='9'){unsigned d=(unsigned)(*(*p)++-'0');if(++digits>20 || n>(UINT64_MAX-d)/10)return -1;n=n*10+d;}
    if(!digits)return -1;
    *out=n;return 0;
}
static size_t header(char *data,size_t size,size_t count,void *arg)
{
    request *q=arg;if(size && count>SIZE_MAX/size)return 0;
    size_t n=size*count;if(n>4096 || n>32768-q->headers || memchr(data,0,n))return 0;q->headers+=n;
    char line[4097];memcpy(line,data,n);line[n]=0;
    if(begins(line,"HTTP/")){q->r->bytes=0;q->r->body[0]=0;q->r->has_range=0;q->r->server_epoch=0;}
    if(begins(line,"Date: ")){time_t epoch=curl_getdate(line+6,NULL);if(epoch>=1609459200 && (uint64_t)epoch<=4102444800ULL)q->r->server_epoch=(uint64_t)epoch;}
    if(begins(line,"Content-Range: bytes ")){
        uint64_t first,last,total;const char *value=line+21;
        if(decimal(&value,&first) || *value++!='-' || decimal(&value,&last) || *value++!='/' || decimal(&value,&total))return 0;
        while(*value=='\r' || *value=='\n' || *value==' ' || *value=='\t')value++;
        if(*value || first>last || last>=total)return 0;
        q->r->range_start=first;q->r->range_end=last;q->r->range_total=total;q->r->has_range=1;
    }
    if(begins(line,"Set-Cookie: ")){
        const char *val=line+12;
        const char *semi=strchr(val,';');
        size_t len=semi ? (size_t)(semi-val) : strlen(val);
        while(len>0 && (val[len-1]=='\r' || val[len-1]=='\n' || val[len-1]==' '))len--;
        if(len>0 && sizeof(q->r->set_cookie)-strlen(q->r->set_cookie)>len+3){
            if(q->r->set_cookie[0])strcat(q->r->set_cookie,"; ");
            strncat(q->r->set_cookie,val,len);
        }
    }
    return n;
}
static int progress(void *arg,curl_off_t a,curl_off_t b,curl_off_t c,curl_off_t d)
{
    (void)a;(void)b;(void)c;(void)d;request *q=arg;return q->cancel ? q->cancel(q->arg) : 0;
}
static int valid_url(const char *url)
{
    if(!url || strncmp(url,"https://",8) || !url[8] || url[8]=='/' || strlen(url)>4095)return 0;
    const char *p;for(p=url;*p;p++)if((unsigned char)*p<=32 || (unsigned char)*p>=127 || *p=='#')return 0;
    for(p=url+8;*p && *p!='/';p++)if(*p=='@')return 0;
    return 1;
}
static int perform(const char *url,const char *ca,const char *range,const char *cookie,const char *form,size_t cap,int (*cancel)(void*),void *arg,wili_https_response *r,void (*log)(const char*))
{
    if(!r || !log || !valid_url(url) || !cap || cap>WILI_HTTPS_MAX_RESPONSE_BYTES)return -1;
    memset(r,0,sizeof(*r));r->capacity=cap;r->body=malloc(cap+1);if(!r->body){return -1;}r->body[0]=0;
    const char *host=url+8,*slash=strchr(host,'/');size_t host_size=slash ? (size_t)(slash-host) : strlen(host);
    if(host_size>=sizeof(r->host)){wili_https_response_free(r);return -1;}memcpy(r->host,host,host_size);r->host[host_size]=0;
    CURL *easy=curl_easy_init();if(!easy){wili_https_response_free(r);return -1;}
    request q={r,cancel,arg,0};char line[384];snprintf(line,sizeof(line),"HTTPS_REQUEST_BEGIN host=%s cap=%u",r->host,(unsigned)cap);log(line);
    curl_easy_setopt(easy,CURLOPT_URL,url);curl_easy_setopt(easy,CURLOPT_PROTOCOLS,(long)CURLPROTO_HTTPS);
    curl_easy_setopt(easy,CURLOPT_REDIR_PROTOCOLS,(long)CURLPROTO_HTTPS);curl_easy_setopt(easy,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(easy,CURLOPT_MAXREDIRS,3L);
    curl_easy_setopt(easy,CURLOPT_CONNECTTIMEOUT_MS,4000L);curl_easy_setopt(easy,CURLOPT_TIMEOUT_MS,12000L);curl_easy_setopt(easy,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(easy,CURLOPT_PROXY,"");
    curl_easy_setopt(easy,CURLOPT_DNS_CACHE_TIMEOUT,0L);
    curl_easy_setopt(easy,CURLOPT_USERAGENT,"wiliwili");curl_easy_setopt(easy,CURLOPT_REFERER,"https://www.bilibili.com/");
    curl_easy_setopt(easy,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(easy,CURLOPT_SSL_VERIFYHOST,2L);curl_easy_setopt(easy,CURLOPT_SSLVERSION,(long)CURL_SSLVERSION_TLSv1_2);
    if(ca && *ca)curl_easy_setopt(easy,CURLOPT_CAINFO,ca);
    curl_easy_setopt(easy,CURLOPT_ACCEPT_ENCODING,"");
    if(range)curl_easy_setopt(easy,CURLOPT_RANGE,range);
    if(cookie && *cookie)curl_easy_setopt(easy,CURLOPT_COOKIE,cookie);
    if(form){curl_easy_setopt(easy,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(easy,CURLOPT_POSTFIELDS,form);curl_easy_setopt(easy,CURLOPT_POSTFIELDSIZE,(long)strlen(form));}
    curl_easy_setopt(easy,CURLOPT_WRITEFUNCTION,write_body);curl_easy_setopt(easy,CURLOPT_WRITEDATA,&q);
    curl_easy_setopt(easy,CURLOPT_HEADERFUNCTION,header);curl_easy_setopt(easy,CURLOPT_HEADERDATA,&q);
    curl_easy_setopt(easy,CURLOPT_XFERINFOFUNCTION,progress);curl_easy_setopt(easy,CURLOPT_XFERINFODATA,&q);curl_easy_setopt(easy,CURLOPT_NOPROGRESS,0L);
    curl_easy_setopt(easy,CURLOPT_ERRORBUFFER,r->error);
    r->curl_code=(int)curl_easy_perform(easy);curl_easy_getinfo(easy,CURLINFO_RESPONSE_CODE,&r->status);curl_easy_getinfo(easy,CURLINFO_SSL_VERIFYRESULT,&r->verify_result);
    r->tls_verified=r->status>0 && !r->verify_result;
    snprintf(line,sizeof(line),"HTTPS_REQUEST_END curl=%d http=%ld verify=%ld tls_verified=%d bytes=%u overflow=%d",r->curl_code,r->status,r->verify_result,r->tls_verified,(unsigned)r->bytes,r->overflow);log(line);
    if(r->curl_code){snprintf(line,sizeof(line),"HTTPS_ERROR reason=%s",curl_easy_strerror((CURLcode)r->curl_code));log(line);}
    curl_easy_cleanup(easy);
    
    return r->curl_code ? -1 : 0;
}
int wili_https_get_cookie(const char *url,const char *ca,const char *range,const char *cookie,size_t cap,int (*cancel)(void*),void *arg,wili_https_response *r,void (*log)(const char*))
{return perform(url,ca,range,cookie,NULL,cap,cancel,arg,r,log);}
int wili_https_post_form(const char *url,const char *ca,const char *cookie,const char *form,size_t cap,int (*cancel)(void*),void *arg,wili_https_response *r,void (*log)(const char*))
{
    if(!url || !form || strlen(form)>8192 || strncmp(url,"https://api.bilibili.com/",sizeof("https://api.bilibili.com/")-1))return -1;
    return perform(url,ca,NULL,cookie,form,cap,cancel,arg,r,log);
}
int wili_https_get(const char *url,const char *ca,const char *range,size_t cap,int (*cancel)(void*),void *arg,wili_https_response *r,void (*log)(const char*))
{
    return wili_https_get_cookie(url,ca,range,NULL,cap,cancel,arg,r,log);
}
void wili_https_response_free(wili_https_response *r){if(r){free(r->body);r->body=NULL;r->bytes=0;}}
