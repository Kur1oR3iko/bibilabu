// SPDX-License-Identifier: GPL-3.0-or-later
#include "https_request.h"
#include "danmaku.h"
#include <sys/time.h>
#include <curl/curl.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void log_line(const char *line){assert(line && *line);}
static int abort_request(void *arg){(void)arg;return 1;}
int main(int argc,char **argv)
{
    assert(argc==4);assert(!curl_global_init(CURL_GLOBAL_DEFAULT));
    const char *base=argv[1],*ca=argv[2];char url[1024];wili_https_response r={0};
    snprintf(url,sizeof(url),"%s/ok",base);assert(!wili_https_get(url,ca,NULL,4096,NULL,NULL,&r,log_line));
    assert(r.status==200 && r.tls_verified && r.bytes==11 && r.server_epoch>=1609459200);wili_https_response_free(&r);
    assert(wili_https_get(url,NULL,NULL,4096,NULL,NULL,&r,log_line)<0 && !r.tls_verified);wili_https_response_free(&r);
    snprintf(url,sizeof(url),"%s/ok",argv[3]);assert(wili_https_get(url,ca,NULL,4096,NULL,NULL,&r,log_line)<0 && !r.tls_verified);wili_https_response_free(&r);
    snprintf(url,sizeof(url),"%s/overflow",base);assert(wili_https_get(url,ca,NULL,32,NULL,NULL,&r,log_line)<0 && r.overflow);wili_https_response_free(&r);
    snprintf(url,sizeof(url),"%s/downgrade",base);assert(wili_https_get(url,ca,NULL,4096,NULL,NULL,&r,log_line)<0);wili_https_response_free(&r);
    snprintf(url,sizeof(url),"%s/range",base);assert(!wili_https_get(url,ca,"0-31",64,NULL,NULL,&r,log_line));
    assert(r.status==206 && r.tls_verified && r.has_range && r.range_start==0 && r.range_end==31 && r.range_total==64 && r.bytes==32);wili_https_response_free(&r);
    assert(wili_https_get(url,ca,NULL,4096,abort_request,NULL,&r,log_line)<0);wili_https_response_free(&r);
    assert(wili_https_get("http://localhost/file",ca,NULL,4096,NULL,NULL,&r,log_line)<0);
    assert(wili_https_get("https://user:secret@localhost/file",ca,NULL,4096,NULL,NULL,&r,log_line)<0);
    snprintf(url,sizeof(url),"%s/danmaku",base);
    assert(!wili_https_get(url,ca,NULL,WILI_DM_RESPONSE_BYTES,NULL,NULL,&r,log_line));
    assert(r.status==200 && r.tls_verified && r.bytes>2u*1024u*1024u);
    wili_dm_segment segment={0};assert(!wili_dm_parse(r.body,r.bytes,1,&segment));
    assert(segment.count==1 && !strcmp(segment.events[0].text,"真实传输弹幕"));
    wili_dm_segment_free(&segment);wili_https_response_free(&r);
    assert(wili_https_get(url,ca,NULL,WILI_HTTPS_MAX_RESPONSE_BYTES+1,NULL,NULL,&r,log_line)<0);
    puts("PASS actual >2 MiB TLS protobuf transfer with production danmaku cap, parser and hard maximum");
    curl_global_cleanup();puts("PASS HTTPS TLS1.2: trusted CA, rejected untrusted CA/wrong host, body cap, downgrade refusal, Range, server Date and cancellation");return 0;
}
