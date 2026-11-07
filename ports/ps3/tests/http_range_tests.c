// SPDX-License-Identifier: GPL-3.0-or-later
#include "http_range.h"
#include "fmp4_stream.h"
#include <curl/curl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void log_line(const char *text){assert(text && *text);}
static int read_at(void *arg,uint64_t at,void *out,size_t n){return wili_http_range_read(arg,at,out,n);}
static uint64_t file_size(const char *name)
{
    FILE *f=fopen(name,"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);assert(size>0);fclose(f);return (uint64_t)size;
}
static void read_movie(const char *base,const char *local,int audio)
{
    char url[2048];snprintf(url,sizeof(url),"%s/%s",base,audio ? "WILI_AUDIO.MP4" : "WILI_VIDEO.MP4");
    wili_http_range range;assert(!wili_http_range_open(&range,url,file_size(local),NULL,NULL,NULL,log_line));
    wili_fmp4 *reader=malloc(sizeof(*reader));uint8_t *packet=malloc(256*1024);assert(reader && packet);
    assert(!wili_fmp4_open(reader,read_at,&range,range.size,audio));
    unsigned count=0;for(;;){size_t n;wili_mp4_sample s;int rc=wili_fmp4_packet(reader,packet,256*1024,&n,&s);assert(rc>=0);if(!rc)break;count++;}
    assert(audio ? count==1408 : count==900);
    assert(range.requests<=range.size/WILI_HTTP_CACHE+8);
    printf("PASS HTTP fMP4 %s samples=%u requests=%llu downloaded=%llu cache=%u\n",audio ? "AAC" : "AVC",count,(unsigned long long)range.requests,(unsigned long long)range.downloaded,WILI_HTTP_CACHE);
    wili_http_range_close(&range);free(reader);free(packet);
}
static int abort_transfer(void *arg){(void)arg;return 1;}
int main(int argc,char **argv)
{
    assert(argc==4);assert(!curl_global_init(CURL_GLOBAL_DEFAULT));
    read_movie(argv[1],argv[2],0);read_movie(argv[1],argv[3],1);
    uint64_t size=file_size(argv[2]);char url[2048];uint8_t data[16];wili_http_range range;
    const char *bad[]={"ignore","wrong","encoded","short","huge"};unsigned i;
    for(i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
        snprintf(url,sizeof(url),"%s/%s/WILI_VIDEO.MP4",argv[1],bad[i]);assert(!wili_http_range_open(&range,url,size,NULL,NULL,NULL,log_line));
        assert(wili_http_range_read(&range,0,data,8)<0);wili_http_range_close(&range);
    }
    snprintf(url,sizeof(url),"%s/changed/WILI_VIDEO.MP4",argv[1]);assert(!wili_http_range_open(&range,url,size,NULL,NULL,NULL,log_line));
    assert(!wili_http_range_read(&range,0,data,8));assert(wili_http_range_read(&range,WILI_HTTP_CACHE,data,8)<0);wili_http_range_close(&range);
    snprintf(url,sizeof(url),"%s/redirect/WILI_VIDEO.MP4",argv[1]);assert(!wili_http_range_open(&range,url,size,NULL,NULL,NULL,log_line));assert(!wili_http_range_read(&range,0,data,8));
    assert(wili_http_range_read(&range,size,data,1)<0);wili_http_range_close(&range);
    assert(!wili_http_range_open(&range,url,size,NULL,abort_transfer,NULL,log_line));assert(wili_http_range_read(&range,0,data,8)<0);wili_http_range_close(&range);
    assert(wili_http_range_open(&range,"file:///etc/passwd",size,NULL,NULL,NULL,log_line)<0);
    assert(wili_http_range_open(&range,"http://user:secret@localhost/file",size,NULL,NULL,NULL,log_line)<0);
    assert(wili_http_range_open(&range,"http://localhost/file\r\nX: value",size,NULL,NULL,NULL,log_line)<0);
    snprintf(url,sizeof(url),"%s/WILI_VIDEO.MP4",argv[1]);assert(!wili_http_range_open(&range,url,size,NULL,NULL,NULL,log_line));
    uint8_t *custom=malloc(WILI_HTTP_CACHE);assert(custom);size_t got=0;
    assert(!wili_http_range_fetch_block(&range,0,custom,&got));assert(got==WILI_HTTP_CACHE);
    assert(!memcmp(custom,"\x00\x00\x00\x1c\x66\x74\x79\x70",8));
    free(custom);wili_http_range_close(&range);
    assert(!wili_http_range_open(&range,url,size,NULL,NULL,NULL,log_line));
    assert(!wili_http_range_set_window(&range,1024u*1024u));
    size_t length=1024u*1024u+97;uint64_t offset=1024u*1024u-31;
    uint8_t *large=malloc(length),*expected=malloc(length);assert(large && expected);
    FILE *local=fopen(argv[2],"rb");assert(local && !fseek(local,(long)offset,SEEK_SET));
    assert(fread(expected,1,length,local)==length);fclose(local);
    assert(!wili_http_range_read(&range,offset,large,length));assert(!memcmp(large,expected,length));assert(range.requests==3);
    // External callers retain their original 256KiB destination contract.
    custom=malloc(WILI_HTTP_CACHE);assert(custom);
    assert(!wili_http_range_fetch_block(&range,0,custom,&got) && got==WILI_HTTP_CACHE);free(custom);
    assert(wili_http_range_set_window(&range,12345)<0);
    assert(!wili_http_range_set_window(&range,WILI_HTTP_CACHE));
    assert(!wili_http_range_read(&range,offset,large,length));assert(!memcmp(large,expected,length));
    free(large);free(expected);wili_http_range_close(&range);
    puts("PASS 1MiB window across boundaries, shrink, ETag retention and fixed-capacity external block reads");
    curl_global_cleanup();puts("PASS ignored/wrong ranges, gzip, short body, oversized headers, ETag change, redirect, cancel, external block buffer and URL guards");return 0;
}
