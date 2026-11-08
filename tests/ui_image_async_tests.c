// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE
#include "ui_image.h"
#include "https_request.h"
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
static const unsigned char fixture_png[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,2,8,2,0,0,0,253,212,154,115,0,0,0,18,73,68,65,84,120,156,99,212,8,168,96,96,96,96,98,0,3,0,11,26,0,244,150,200,14,181,0,0,0,0,73,69,78,68,174,66,96,130};
static volatile unsigned calls;
static void log_line(const char *s){(void)s;}
// Deterministic failed request: no internet or account involved.
int wili_https_get(const char *url,const char *ca,const char *range,size_t cap,
                   int (*cancel)(void*),void *arg,wili_https_response *r,void (*log)(const char*))
{
    (void)url;(void)ca;(void)range;(void)cap;(void)log;
    __sync_add_and_fetch(&calls,1);memset(r,0,sizeof(*r));
    for(int i=0;i<100 && !cancel(arg);i++)usleep(1000);
    if(strstr(url,"session-cache")){r->body=malloc(sizeof(fixture_png));assert(r->body);memcpy(r->body,fixture_png,sizeof(fixture_png));r->bytes=sizeof(fixture_png);r->status=200;return 0;}
    r->status=404;return -1;
}
void wili_https_response_free(wili_https_response *r){free(r->body);r->body=NULL;}
int main(void)
{
    unsigned char png[sizeof(fixture_png)];memcpy(png,fixture_png,sizeof(png));
    uint32_t *wall=malloc(1280*720*4);assert(wall);
    assert(!ui_image_decode_wallpaper(png,sizeof(png),wall));assert(wall[0]==0x001e3c5a && wall[1280*720-1]==wall[0]);
    png[16]=0x7f;assert(ui_image_decode_wallpaper(png,sizeof(png),wall)<0);free(wall);
    puts("PASS wallpaper decode, scale, bounded dimensions and malformed rejection");
    assert(!ui_image_init(NULL,log_line));uint32_t fb[64*64]={0};
    const char *url="https://example.invalid/cover.jpg";
    unsigned initial=ui_image_revision();
    for(int i=0;i<200;i++){assert(!ui_image_draw(fb,64,64,0,0,64,64,url));usleep(1000);}
    assert(__sync_add_and_fetch(&calls,0)==1);assert(ui_image_revision()>initial);
    for(int i=0;i<100;i++)assert(!ui_image_draw(fb,64,64,0,0,64,64,url));
    ui_image_pause(1);ui_image_draw(fb,64,64,0,0,64,64,"https://example.invalid/paused.jpg");
    usleep(60000);assert(__sync_add_and_fetch(&calls,0)==1);
    ui_image_shutdown();ui_image_shutdown();
    assert(!ui_image_init(NULL,log_line));unsigned prior=calls;int loaded=0;
    for(unsigned i=0;i<2000 && !loaded;i++){loaded=ui_image_draw(fb,64,64,0,0,64,64,"https://example.invalid/session-cache.jpg");usleep(1000);}assert(loaded && calls==prior+1);
    ui_image_shutdown();assert(!ui_image_init(NULL,log_line));loaded=0;
    for(unsigned i=0;i<2000 && !loaded;i++){loaded=ui_image_draw(fb,64,64,0,0,64,64,"https://example.invalid/session-cache.jpg");usleep(1000);}assert(loaded && calls==prior+1);
    ui_image_shutdown();ui_image_end_session();puts("PASS compressed thumbnail cache survives worker restart without another request");
    puts("PASS image in-flight deduplication, negative cache, pause, idempotent shutdown");
}
