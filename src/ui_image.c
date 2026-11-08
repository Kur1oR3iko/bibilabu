// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#include "ui_image.h"
#include "https_request.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__PPU__) || defined(__CELLOS_LV2__)
#include <sys/thread.h>
#include <sys/systime.h>
#include <sys/mutex.h>
#define THREAD_RET void
#define THREAD_ARG void *arg
#else
#include <pthread.h>
#include <unistd.h>
#define THREAD_RET void*
#define THREAD_ARG void *arg
#endif

#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb/stb_image.h"

#define IMAGE_CACHE_SIZE 16
#define THUMB_MAX 512
static struct image_entry {
    char url[512];int state,w,h;uint32_t *pixels;uint64_t used;
} cache[IMAGE_CACHE_SIZE];
static uint64_t use_counter;
// Keep compressed thumbnails for this launch; decoded pixels may be evicted.
static struct {char url[640];unsigned char *data;size_t bytes;} compressed_images[512];
static unsigned compressed_count;static size_t compressed_bytes;
static volatile unsigned active,paused,revision;
static int started;
static char wallpaper_request[256];
static uint32_t *wallpaper_ready;
static unsigned wallpaper_generation,wallpaper_done;
static const char *ca_path;
static void (*logger)(const char *);
#if defined(__PPU__) || defined(__CELLOS_LV2__)
static sys_ppu_thread_t s_thread = 0;
static sys_mutex_t s_img_mutex = 0;
static int s_img_mutex_inited = 0;
static void img_lock(void) {
    if(!s_img_mutex_inited) {
        sys_mutex_attr_t attr;
        sysMutexAttrInitialize(attr);
        attr.attr_recursive = SYS_MUTEX_ATTR_RECURSIVE;
        if(sysMutexCreate(&s_img_mutex, &attr) == 0) s_img_mutex_inited = 1;
    }
    if(s_img_mutex_inited) sysMutexLock(s_img_mutex, 0);
}
static void img_unlock(void) {
    if(s_img_mutex_inited) sysMutexUnlock(s_img_mutex);
}
#else
static pthread_t s_thread;
static pthread_mutex_t s_img_mutex = PTHREAD_MUTEX_INITIALIZER;
static void img_lock(void) { pthread_mutex_lock(&s_img_mutex); }
static void img_unlock(void) { pthread_mutex_unlock(&s_img_mutex); }
#endif

static void log_msg(const char *line){if(logger)logger(line);}
static int cancel(void *arg){(void)arg;return !__sync_add_and_fetch(&active,0)||__sync_add_and_fetch(&paused,0);}
int ui_image_decode_wallpaper(const unsigned char *data,size_t bytes,uint32_t *out)
{
    int w=0,h=0,ch=0;
    if(!data || !out || bytes>8*1024*1024 || !stbi_info_from_memory(data,(int)bytes,&w,&h,&ch) ||
       w<=0 || h<=0 || w>4096 || h>2160 || (uint64_t)w*h>1920u*1080u)return -1;
    unsigned char *raw=stbi_load_from_memory(data,(int)bytes,&w,&h,&ch,4);if(!raw)return -1;
    int cw=w,chh=h;
    if((int64_t)w*720>(int64_t)h*1280)cw=h*1280/720;else chh=w*720/1280;
    int ox=(w-cw)/2,oy=(h-chh)/2;
    for(int y=0;y<720;y++)for(int x=0;x<1280;x++){
        const unsigned char *pixel=raw+((oy+y*chh/720)*w+ox+x*cw/1280)*4;
        // A subtle veil plus text shadows keeps white XMB labels readable.
        out[y*1280+x]=((uint32_t)(pixel[0]*3/4)<<16)|((uint32_t)(pixel[1]*3/4)<<8)|(pixel[2]*3/4);
    }
    stbi_image_free(raw);return 0;
}
static void load_wallpaper(const char *path,unsigned generation)
{
    FILE *f=fopen(path,"rb");if(!f){log_msg("WALLPAPER_OPEN_FAILED");return;}
    long n=0;if(!fseek(f,0,SEEK_END))n=ftell(f);rewind(f);
    unsigned char *data=(n>0 && n<=8*1024*1024)?malloc((size_t)n):NULL;
    int ok=data && fread(data,1,(size_t)n,f)==(size_t)n;fclose(f);
    uint32_t *pixels=ok && !cancel(NULL)?malloc(1280*720*4):NULL;
    if(pixels && ui_image_decode_wallpaper(data,(size_t)n,pixels)){free(pixels);pixels=NULL;}
    free(data);
    img_lock();
    if(pixels && generation==wallpaper_generation){free(wallpaper_ready);wallpaper_ready=pixels;pixels=NULL;__sync_add_and_fetch(&revision,1);}
    if(cancel(NULL) && generation==wallpaper_generation)wallpaper_done=generation-1;
    int applied=wallpaper_ready!=NULL;img_unlock();free(pixels);log_msg(applied?"WALLPAPER_READY":"WALLPAPER_DECODE_FAILED_OR_CANCELED");
}
void ui_image_request_wallpaper(const char *path)
{
    if(!started || !path)return;
    img_lock();snprintf(wallpaper_request,sizeof(wallpaper_request),"%s",path);wallpaper_generation++;img_unlock();
}
int ui_image_take_wallpaper(uint32_t *out)
{
    int changed=0;if(!started || !out)return 0;img_lock();
    if(wallpaper_ready){memcpy(out,wallpaper_ready,1280*720*4);free(wallpaper_ready);wallpaper_ready=NULL;changed=1;}
    img_unlock();return changed;
}
static THREAD_RET image_worker(THREAD_ARG)
{
    (void)arg;
    while(__sync_add_and_fetch(&active,0)){
        if(!__sync_add_and_fetch(&paused,0)){
            char path[256];unsigned generation=0;img_lock();
            if(wallpaper_generation!=wallpaper_done){generation=wallpaper_generation;wallpaper_done=generation;snprintf(path,sizeof(path),"%s",wallpaper_request);}
            img_unlock();if(generation){load_wallpaper(path,generation);continue;}
        }
        int index=-1;char url[640];
        img_lock();
        if(!__sync_add_and_fetch(&paused,0))for(int i=0;i<IMAGE_CACHE_SIZE;i++)if(cache[i].state==2 && (index<0 || cache[i].used>cache[index].used))index=i;
        if(index>=0)cache[index].state=3;
        if(index>=0){
            const char *src=cache[index].url;
            snprintf(url,sizeof(url),"%s%s",!strncmp(src,"http://",7)?"https://":"",!strncmp(src,"http://",7)?src+7:src);
            if(!strchr(url,'@'))strncat(url,"@256w_144h.jpg",sizeof(url)-strlen(url)-1);
        }
        img_unlock();
        if(index<0){
#if defined(__PPU__) || defined(__CELLOS_LV2__)
            sysUsleep(20000);
#else
            usleep(20000);
#endif
            continue;
        }
        wili_https_response r={0};uint32_t *pixels=NULL;int w=0,h=0,ch=0;
        const unsigned char *encoded=NULL;size_t encoded_bytes=0;
        for(unsigned i=0;i<compressed_count;i++)if(!strcmp(compressed_images[i].url,url)){encoded=compressed_images[i].data;encoded_bytes=compressed_images[i].bytes;break;}
        int rc=0;
        if(!encoded){
            rc=wili_https_get(url,ca_path,NULL,256*1024,cancel,NULL,&r,log_msg);
            if(!rc && r.status==200 && r.body){encoded=(void*)r.body;encoded_bytes=r.bytes;
                if(compressed_count<512 && r.bytes<=8*1024*1024-compressed_bytes){
                    unsigned char *copy=malloc(r.bytes);if(copy){memcpy(copy,r.body,r.bytes);snprintf(compressed_images[compressed_count].url,640,"%s",url);
                        compressed_images[compressed_count].data=copy;compressed_images[compressed_count].bytes=r.bytes;compressed_count++;compressed_bytes+=r.bytes;}
                }
            }
        }
        if(!rc && encoded && stbi_info_from_memory(encoded,(int)encoded_bytes,&w,&h,&ch) && w>0 && h>0 && w<=THUMB_MAX && h<=THUMB_MAX){
            unsigned char *raw=stbi_load_from_memory(encoded,(int)encoded_bytes,&w,&h,&ch,4);
            if(raw){
                pixels=malloc((size_t)w*h*4);
                if(pixels)for(int i=0;i<w*h;i++)pixels[i]=((uint32_t)raw[i*4]<<16)|((uint32_t)raw[i*4+1]<<8)|raw[i*4+2];
                stbi_image_free(raw);
            }
        }
        wili_https_response_free(&r);
        img_lock();cache[index].pixels=pixels;cache[index].w=w;cache[index].h=h;
        cache[index].state=pixels?1:(cancel(NULL)?2:-1);
        __sync_add_and_fetch(&revision,1);img_unlock();
    }
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    sysThreadExit(0);
#else
    return NULL;
#endif
}
int ui_image_init(const char *ca,void (*log)(const char *))
{
    if(started)return 0;
    img_lock();
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    if(!s_img_mutex_inited)return -1;
#endif
    memset(cache,0,sizeof(cache));wallpaper_generation=wallpaper_done=0;ca_path=ca;logger=log;active=1;paused=0;img_unlock();
    int rc;
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    rc=sysThreadCreate(&s_thread,image_worker,NULL,1150,262144,THREAD_JOINABLE,"bibi-img");
#else
    rc=pthread_create(&s_thread,NULL,image_worker,NULL);
#endif
    if(rc){active=0;return -1;}started=1;return 0;
}
void ui_image_pause(int value){__sync_lock_test_and_set(&paused,value!=0);}
unsigned ui_image_revision(void){return __sync_add_and_fetch(&revision,0);}
void ui_image_cancel(void){__sync_lock_test_and_set(&active,0);}
void ui_image_end_session(void)
{
    // All image workers must be joined before this is called.
    for(unsigned i=0;i<compressed_count;i++)free(compressed_images[i].data);
    memset(compressed_images,0,sizeof(compressed_images));compressed_count=0;compressed_bytes=0;
}
void ui_image_shutdown(void)
{
    if(!started)return;
    __sync_lock_test_and_set(&active,0);
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    u64 result;sysThreadJoin(s_thread,&result);
#else
    pthread_join(s_thread,NULL);
#endif
    img_lock();for(int i=0;i<IMAGE_CACHE_SIZE;i++)free(cache[i].pixels);memset(cache,0,sizeof(cache));free(wallpaper_ready);wallpaper_ready=NULL;img_unlock();started=0;
}
int ui_image_draw(uint32_t *fb,unsigned fw,unsigned fh,int dx,int dy,int dw,int dh,const char *url)
{
    if(!started || !fb || !url || !*url || strlen(url)>=sizeof(cache[0].url) || dw<=0 || dh<=0)return 0;
    img_lock();struct image_entry *entry=NULL;
    for(int i=0;i<IMAGE_CACHE_SIZE;i++)if(cache[i].state && !strcmp(url,cache[i].url)){entry=&cache[i];break;}
    if(!entry){
        for(int i=0;i<IMAGE_CACHE_SIZE;i++)if(cache[i].state!=2 && cache[i].state!=3 && (!entry || cache[i].used<entry->used))entry=&cache[i];
        if(entry){free(entry->pixels);memset(entry,0,sizeof(*entry));snprintf(entry->url,sizeof(entry->url),"%s",url);entry->state=2;entry->used=++use_counter;}
        img_unlock();return 0;
    }
    entry->used=++use_counter;
    if(entry->state!=1){img_unlock();return 0;}
    for(int y=0;y<dh;y++){
        int py=dy+y;if(py<0 || py>=(int)fh)continue;
        const uint32_t *src=entry->pixels+(y*entry->h/dh)*entry->w;
        for(int x=0;x<dw;x++){int px=dx+x;if(px>=0 && px<(int)fw)fb[py*fw+px]=src[x*entry->w/dw];}
    }
    img_unlock();return 1;
}
