// SPDX-License-Identifier: GPL-3.0-or-later
#include "http_range.h"
#include <sys/time.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
typedef struct {
    wili_http_range *r;uint8_t *dest;size_t received,header_bytes;uint64_t start,end,total,length;
    int range,length_seen,encoded,bad;char etag[160];
} transfer;
static int prefix(const char *a,const char *b)
{
    while(*b){unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='A' && x<='Z')x+=32;
        if(y>='A' && y<='Z')y+=32;
        if(x!=y)return 0;}
    return 1;
}
static int decimal(const char **p,uint64_t *out)
{
    const char *s=*p;uint64_t n=0;unsigned digits=0;
    while(*s>='0' && *s<='9'){
        unsigned d=(unsigned)(*s++-'0');if(++digits>20 || n>(UINT64_MAX-d)/10)return -1;n=n*10+d;
    }
    if(!digits)return -1;
    *p=s;*out=n;return 0;
}
static size_t headers(char *data,size_t size,size_t count,void *arg)
{
    transfer *t=arg;
    if(size && count>SIZE_MAX/size)return 0;
    size_t n=size*count;if(n>4096 || n>32768-t->header_bytes || memchr(data,0,n)){t->bad=1;return 0;}t->header_bytes+=n;
    char line[4097];memcpy(line,data,n);line[n]=0;
    while(n && (line[n-1]=='\r' || line[n-1]=='\n' || line[n-1]==' ' || line[n-1]=='\t'))line[--n]=0;
    if(prefix(line,"HTTP/")){
        t->received=0;t->range=0;t->length_seen=0;t->encoded=0;t->etag[0]=0;return size*count;
    }
    char *colon=strchr(line,':');if(!colon)return size*count;
    *colon=0;const char *value=colon+1;while(*value==' ' || *value=='\t')value++;
    if(prefix(line,"Content-Range") && strlen(line)==13){
        if(t->range || !prefix(value,"bytes ")){t->bad=1;return 0;}value+=6;
        if(decimal(&value,&t->start) || *value++!='-' || decimal(&value,&t->end) || *value++!='/' || decimal(&value,&t->total) || *value){t->bad=1;return 0;}t->range=1;
    }else if(prefix(line,"Content-Length") && strlen(line)==14){
        if(t->length_seen || decimal(&value,&t->length) || *value){t->bad=1;return 0;}t->length_seen=1;
    }else if(prefix(line,"Content-Encoding") && strlen(line)==16){
        if(strcmp(value,"identity")){t->encoded=1;return 0;}
    }else if(prefix(line,"ETag") && strlen(line)==4){
        size_t length=strlen(value);
        if(length>=sizeof(t->etag)){t->bad=1;return 0;}
        // Only strong entity validators can safely pin independently fetched ranges.
        if(length>=2 && value[0]=='"' && value[length-1]=='"')memcpy(t->etag,value,length+1);
    }
    return size*count;
}
static size_t body(char *data,size_t size,size_t count,void *arg)
{
    transfer *t=arg;if(size && count>SIZE_MAX/size)return 0;
    size_t n=size*count;
    size_t capacity=t->dest?WILI_HTTP_CACHE:t->r->capacity;
    if(t->received>capacity || n>capacity-t->received){t->bad=1;return 0;}
    uint8_t *dst=t->dest ? t->dest : t->r->cache;
    memcpy(dst+t->received,data,n);t->received+=n;return n;
}
static int progress(void *arg,curl_off_t total,curl_off_t now,curl_off_t up_total,curl_off_t up_now)
{
    (void)total;(void)now;(void)up_total;(void)up_now;
    wili_http_range *r=arg;return r->cancel ? r->cancel(r->cancel_arg) : 0;
}
static int safe_url(const char *url)
{
    if(!url || strlen(url)>=2048)return 0;
    const char *host;
    if(!strncmp(url,"http://",7))host=url+7;
    else if(!strncmp(url,"https://",8))host=url+8;else return 0;
    if(!*host || *host=='/' || *host=='?' || *host=='#')return 0;
    const char *p;
    for(p=url;*p;p++)if((unsigned char)*p<=32 || (unsigned char)*p>=127 || *p=='#')return 0;
    for(p=host;*p && *p!='/' && *p!='?';p++)if(*p=='@')return 0;
    return 1;
}
int wili_http_range_fetch_block(wili_http_range *r,uint64_t start,void *dst,size_t *out_received)
{
    if(!r || !r->easy || start>=r->size)return -1;
    size_t window=dst?WILI_HTTP_CACHE:r->capacity;
    uint64_t end=r->size-start>window ? start+window-1 : r->size-1;
    transfer t;memset(&t,0,sizeof(t));t.r=r;t.dest=(uint8_t*)dst;
    char range[80],if_range[192];snprintf(range,sizeof(range),"%llu-%llu",(unsigned long long)start,(unsigned long long)end);
    struct curl_slist *list=NULL;
    if(r->etag[0]){snprintf(if_range,sizeof(if_range),"If-Range: %s",r->etag);list=curl_slist_append(NULL,if_range);if(!list)return -1;}
    CURL *easy=r->easy;
    curl_easy_setopt(easy,CURLOPT_TIMEOUT_MS,window>WILI_HTTP_CACHE?12000L:6000L);
    curl_easy_setopt(easy,CURLOPT_RANGE,range);curl_easy_setopt(easy,CURLOPT_HTTPHEADER,list);
    curl_easy_setopt(easy,CURLOPT_HEADERDATA,&t);curl_easy_setopt(easy,CURLOPT_WRITEDATA,&t);
    r->error[0]=0;CURLcode rc=curl_easy_perform(easy);long status=0;curl_easy_getinfo(easy,CURLINFO_RESPONSE_CODE,&status);
    curl_easy_setopt(easy,CURLOPT_HTTPHEADER,NULL);curl_slist_free_all(list);r->requests++;
    int bad=rc!=CURLE_OK || status!=206 || t.bad || t.encoded || !t.range || t.start!=start || t.end!=end || t.total!=r->size ||
            t.received!=(size_t)(end-start+1) || (t.length_seen && t.length!=end-start+1) || (r->etag[0] && strcmp(r->etag,t.etag));
    char line[320];
    if(bad){
        const char *reason=rc!=CURLE_OK ? curl_easy_strerror(rc) : status!=206 ? "HTTP_status_not_206" : t.bad ? "invalid_headers_or_body_overflow" : t.encoded ? "encoded_body" :
            !t.range || t.start!=start || t.end!=end || t.total!=r->size ? "Content_Range_mismatch" : t.received!=(size_t)(end-start+1) || (t.length_seen && t.length!=end-start+1) ? "body_length_mismatch" : "ETag_changed_or_missing";
        snprintf(line,sizeof(line),"HTTP_RANGE_FAILED curl=%d status=%ld reason=%s bytes=%u expected=%u",(int)rc,status,reason,(unsigned)t.received,(unsigned)(end-start+1));r->log(line);
        if(!dst) r->valid=0;
        return -1;
    }
    if(!r->etag[0] && t.etag[0])memcpy(r->etag,t.etag,strlen(t.etag)+1);
    if(!dst){r->offset=start;r->valid=t.received;}
    r->downloaded+=t.received;
    if(out_received)*out_received=t.received;
    double total_s=0,first_s=0,connect_s=0,tls_s=0;long connects=0;
    curl_easy_getinfo(easy,CURLINFO_TOTAL_TIME,&total_s);curl_easy_getinfo(easy,CURLINFO_STARTTRANSFER_TIME,&first_s);
    curl_easy_getinfo(easy,CURLINFO_CONNECT_TIME,&connect_s);curl_easy_getinfo(easy,CURLINFO_APPCONNECT_TIME,&tls_s);
    curl_easy_getinfo(easy,CURLINFO_NUM_CONNECTS,&connects);
    snprintf(line,sizeof(line),"HTTP_RANGE_OK offset=%llu bytes=%u requests=%llu etag=%s total_ms=%u first_ms=%u connect_ms=%u tls_ms=%u new_connections=%ld",
        (unsigned long long)start,(unsigned)t.received,(unsigned long long)r->requests,r->etag[0]?"pinned":"absent",
        (unsigned)(total_s*1000),(unsigned)(first_s*1000),(unsigned)(connect_s*1000),(unsigned)(tls_s*1000),connects);r->log(line);return 0;
}
int wili_http_range_set_window(wili_http_range *r,size_t bytes)
{
    if(!r || !r->easy || bytes<WILI_HTTP_CACHE || bytes>2u*1024u*1024u || (bytes&(bytes-1)))return -1;
    if(bytes==r->capacity)return 0;
    uint8_t *cache=realloc(r->cache,bytes);if(!cache)return -1;
    r->cache=cache;r->capacity=bytes;if(r->valid>bytes)r->valid=0;return 0;
}
static int fill(wili_http_range *r,uint64_t start)
{
    size_t got=0;
    return wili_http_range_fetch_block(r,start,NULL,&got);
}
int wili_http_range_open(wili_http_range *r,const char *url,uint64_t size,const char *ca,int (*cancel)(void*),void *arg,void (*log)(const char*))
{
    if(!r || !log || !size || !safe_url(url))return -1;
    memset(r,0,sizeof(*r));r->size=size;r->cancel=cancel;r->cancel_arg=arg;r->log=log;memcpy(r->url,url,strlen(url)+1);
    r->capacity=WILI_HTTP_CACHE;r->cache=malloc(r->capacity);r->easy=curl_easy_init();
    if(!r->cache || !r->easy){wili_http_range_close(r);return -1;}
    CURL *easy=r->easy;long protocols=CURLPROTO_HTTP|CURLPROTO_HTTPS;
    curl_easy_setopt(easy,CURLOPT_URL,r->url);curl_easy_setopt(easy,CURLOPT_PROTOCOLS,protocols);
    curl_easy_setopt(easy,CURLOPT_REDIR_PROTOCOLS,!strncmp(url,"https://",8) ? (long)CURLPROTO_HTTPS : protocols);
    curl_easy_setopt(easy,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(easy,CURLOPT_MAXREDIRS,3L);
    curl_easy_setopt(easy,CURLOPT_CONNECTTIMEOUT_MS,2500L);curl_easy_setopt(easy,CURLOPT_TIMEOUT_MS,6000L);
    curl_easy_setopt(easy,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(easy,CURLOPT_PROXY,"");
    curl_easy_setopt(easy,CURLOPT_TCP_KEEPALIVE,1L);curl_easy_setopt(easy,CURLOPT_BUFFERSIZE,64L*1024L);
    curl_easy_setopt(easy,CURLOPT_USERAGENT,"wiliwili-ps3-probe/1.0");
    curl_easy_setopt(easy,CURLOPT_REFERER,"https://www.bilibili.com/");
    curl_easy_setopt(easy,CURLOPT_ACCEPT_ENCODING,"identity");curl_easy_setopt(easy,CURLOPT_HTTP_CONTENT_DECODING,0L);
    curl_easy_setopt(easy,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(easy,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(easy,CURLOPT_SSLVERSION,(long)CURL_SSLVERSION_TLSv1_2);
    if(ca && *ca)curl_easy_setopt(easy,CURLOPT_CAINFO,ca);
    curl_easy_setopt(easy,CURLOPT_WRITEFUNCTION,body);curl_easy_setopt(easy,CURLOPT_HEADERFUNCTION,headers);
    curl_easy_setopt(easy,CURLOPT_NOPROGRESS,0L);curl_easy_setopt(easy,CURLOPT_XFERINFOFUNCTION,progress);curl_easy_setopt(easy,CURLOPT_XFERINFODATA,r);
    curl_easy_setopt(easy,CURLOPT_ERRORBUFFER,r->error);
    return 0;
}
int wili_http_range_read(wili_http_range *r,uint64_t at,void *out,size_t n)
{
    if(!r || !r->easy || !out || at>r->size || n>r->size-at)return -1;
    size_t done=0;
    while(done<n){
        uint64_t pos=at+done;
        if(!r->valid || pos<r->offset || pos-r->offset>=r->valid){if(fill(r,(pos/r->capacity)*r->capacity))return -1;}
        size_t within=(size_t)(pos-r->offset),copy=r->valid-within;if(copy>n-done)copy=n-done;
        memcpy((uint8_t*)out+done,r->cache+within,copy);done+=copy;
    }
    return 0;
}
void wili_http_range_close(wili_http_range *r)
{
    if(!r)return;
    if(r->easy)curl_easy_cleanup(r->easy);
    free(r->cache);r->easy=NULL;r->cache=NULL;r->valid=0;
}
