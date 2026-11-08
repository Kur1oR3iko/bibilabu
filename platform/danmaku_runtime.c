// SPDX-License-Identifier: GPL-3.0-or-later
#include "danmaku_runtime.h"
#include "danmaku_retry.h"
#include "user_settings.h"
#include "https_request.h"
#include "ui_font.h"
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/systime.h>
#include <rsx/rsx.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
static sys_mutex_t mutex;
static sys_ppu_thread_t thread;
static volatile unsigned running,epoch,density=WILI_DEFAULT_DANMAKU_DENSITY,allowed;
static volatile unsigned settings_dirty;
static uint64_t settings_retry;
static uint64_t cid;
static unsigned wanted,preload,initialized,revision;
static struct {wili_dm_segment segment;unsigned epoch,ready;wili_dm_retry retry;} cache[2];
static wili_dm_segment current;
static wili_dm_player player;
static wili_dm_draw draw;
static uint32_t *atlas,atlas_offset;
static uint32_t glyph[WILI_DM_SLOT_WIDTH*WILI_DM_SLOT_HEIGHT];
static unsigned painted[WILI_DM_LANES],reported_expired;
static uint32_t last_overload_ms;
static uint64_t copy_retry_after;
static const char *ca_path;
static void (*logger)(const char*);
static int cancel(void *arg)
{return !__sync_add_and_fetch(&running,0) || !__sync_add_and_fetch(&allowed,0) || !__sync_add_and_fetch(&density,0) || (unsigned)(uintptr_t)arg!=__sync_add_and_fetch(&epoch,0);}
static void save_density(int force)
{
    if(!settings_dirty || (!force && (uint64_t)sysGetSystemTime()<settings_retry))return;
    if(!__sync_lock_test_and_set(&settings_dirty,0))return;
    unsigned value=__sync_add_and_fetch(&density,0);
    int rc=wili_settings_save_density(WILI_SETTINGS_PATH,value);
    char line[96];snprintf(line,sizeof(line),"DANMAKU_SETTINGS_SAVE density=%u result=%s",value,rc?"retry":"saved");logger(line);
    if(rc){__sync_lock_test_and_set(&settings_dirty,1);settings_retry=(uint64_t)sysGetSystemTime()+5000000;}
    else settings_retry=0;
}
static void worker(void *arg)
{
    (void)arg;wili_dm_segment *parsed=calloc(1,sizeof(*parsed));
    if(!parsed){logger("DANMAKU_ALLOC_FAILED");sysThreadExit(0);return;}
    while(__sync_add_and_fetch(&running,0)){
        save_density(0);
        uint64_t job_cid=0;unsigned segment=0,generation=0;
        sysMutexLock(mutex,0);
        if(cid && density && allowed){
            for(unsigned i=0;i<(preload?2u:1u);i++){unsigned candidate=wanted+i;
                if(candidate && candidate<=240 && (!cache[candidate%2].ready || cache[candidate%2].epoch!=epoch || cache[candidate%2].segment.index!=candidate) && wili_dm_retry_due(&cache[candidate%2].retry,epoch,candidate,sysGetSystemTime())){
                    job_cid=cid;segment=candidate;generation=epoch;break;
                }
            }
        }
        sysMutexUnlock(mutex);
        if(!segment){sysUsleep(50000);continue;}
        char url[256];snprintf(url,sizeof(url),"https://api.bilibili.com/x/v2/dm/web/seg.so?type=1&oid=%llu&segment_index=%u",(unsigned long long)job_cid,segment);
        wili_https_response response={0};
        int rc=wili_https_get(url,ca_path,NULL,WILI_DM_RESPONSE_BYTES,cancel,(void*)(uintptr_t)generation,&response,logger);
        long http=response.status;size_t bytes=response.bytes;
        int parse_rc=!rc && http==200?wili_dm_parse(response.body,response.bytes,segment,parsed):-2;
        int ok=!rc && http==200 && !parse_rc;
        int curl_code=response.curl_code;
        wili_https_response_free(&response);
        sysMutexLock(mutex,0);
        if(epoch==generation && cid==job_cid && allowed && density){
            unsigned retry_seconds=0;
            if(ok){
                wili_dm_segment_free(&cache[segment%2].segment);
                cache[segment%2].segment=*parsed;memset(parsed,0,sizeof(*parsed));
                cache[segment%2].epoch=generation;cache[segment%2].ready=1;
                memset(&cache[segment%2].retry,0,sizeof(cache[segment%2].retry));
            }else{
                // A failed fetch is not a successfully loaded empty segment.
                retry_seconds=wili_dm_retry_fail(&cache[segment%2].retry,generation,segment,sysGetSystemTime(),http);
            }
            const wili_dm_segment *saved=&cache[segment%2].segment;
            char line[256];snprintf(line,sizeof(line),"DANMAKU_SEGMENT index=%u count=%u unsupported=%u overflow=%u truncated=%u result=%s rc=%d curl=%d http=%ld bytes=%u parse=%d retry_s=%u",segment,ok?saved->count:0,ok?saved->unsupported:0,ok?saved->overflow:0,ok?saved->truncated:0,ok?"ready":"retry",rc,curl_code,http,(unsigned)bytes,parse_rc,retry_seconds);logger(line);
        }
        sysMutexUnlock(mutex);
    }
    save_density(1);wili_dm_segment_free(parsed);free(parsed);sysThreadExit(0);
}
int danmaku_init(const char *ca,void (*log)(const char*))
{
    if(initialized)return 0;
    ca_path=ca;logger=log;
    if(!settings_dirty)density=wili_settings_load_density(WILI_SETTINGS_PATH);
    settings_retry=0;
    {char line[80];snprintf(line,sizeof(line),"DANMAKU_SETTINGS_LOAD density=%u",density);logger(line);}
    atlas=memalign(1048576,WILI_DM_ATLAS_BYTES);if(!atlas)return -1;memset(atlas,0,WILI_DM_ATLAS_BYTES);
    if(gcmMapMainMemory(atlas,WILI_DM_ATLAS_BYTES,&atlas_offset)){free(atlas);atlas=NULL;return -1;}
    sys_mutex_attr_t attr;sysMutexAttrInitialize(attr);
    if(sysMutexCreate(&mutex,&attr)){gcmUnmapIoAddress(atlas_offset);free(atlas);atlas=NULL;return -1;}
    running=1;
    if(sysThreadCreate(&thread,worker,NULL,1200,131072,THREAD_JOINABLE,"bibi-danmaku")){
        running=0;sysMutexDestroy(mutex);gcmUnmapIoAddress(atlas_offset);free(atlas);atlas=NULL;return -1;
    }
    initialized=1;return 0;
}
void danmaku_start(uint64_t value)
{
    if(!initialized)return;
    sysMutexLock(mutex,0);epoch++;cid=value;wanted=1;preload=allowed=0;sysMutexUnlock(mutex);
    wili_dm_segment_free(&current);memset(&player,0,sizeof(player));reported_expired=last_overload_ms=0;copy_retry_after=0;memset(painted,0,sizeof(painted));draw.count=0;revision++;
}
void danmaku_stop(void){danmaku_start(0);}
void danmaku_density(unsigned value){if(value>6)value=6;if(__sync_lock_test_and_set(&density,value)!=value)__sync_lock_test_and_set(&settings_dirty,1);revision++;}
unsigned danmaku_get_density(void){return density;}
unsigned danmaku_tick(uint32_t ms,int allow_network)
{
    if(!initialized)return revision;
    unsigned segment=ms/360000+1;
    sysMutexLock(mutex,0);wanted=segment;preload=ms%360000>=345000;allowed=allow_network!=0;
    if(current.index!=segment && (uint64_t)sysGetSystemTime()>=copy_retry_after && cache[segment%2].ready && cache[segment%2].epoch==epoch && cache[segment%2].segment.index==segment){
        if(wili_dm_segment_copy(&current,&cache[segment%2].segment)){
            copy_retry_after=sysGetSystemTime()+5000000;logger("DANMAKU_CURRENT_ALLOC_FAILED retry_s=5");
        }
    }
    sysMutexUnlock(mutex);
    unsigned before=draw.count;wili_dm_step(&player,current.index==segment?&current:NULL,ms,density);
    for(unsigned i=0;i<WILI_DM_LANES;i++)if(player.lanes[i].serial && painted[i]!=player.lanes[i].serial){
        uint32_t *row=atlas+(i/2)*WILI_DM_SLOT_HEIGHT*WILI_DM_ATLAS_WIDTH+(i%2)*WILI_DM_SLOT_WIDTH;
        memset(glyph,0,sizeof(glyph));
        unsigned size=player.lanes[i].event.size;int w=0,h=0;
        ui_font_measure_text((int)size,player.lanes[i].event.text,&w,&h);
        if(w>1018){size=size*1018/(unsigned)w;if(size<8)size=8;ui_font_measure_text((int)size,player.lanes[i].event.text,&w,&h);}
        ui_font_alpha_target(1);
        ui_font_draw_text(glyph,WILI_DM_SLOT_WIDTH,40,3,5,(int)size,player.lanes[i].event.text,0xe0000000);
        ui_font_draw_text(glyph,WILI_DM_SLOT_WIDTH,40,2,4,(int)size,player.lanes[i].event.text,0xe0000000|player.lanes[i].event.color);
        ui_font_alpha_target(0);
        player.lanes[i].width=w+6>1024?1024:(unsigned)(w+6);painted[i]=player.lanes[i].serial;
        for(unsigned y=0;y<WILI_DM_SLOT_HEIGHT;y++)memcpy(row+y*WILI_DM_ATLAS_WIDTH,glyph+y*WILI_DM_SLOT_WIDTH,WILI_DM_SLOT_WIDTH*4);
        for(unsigned y=0;y<WILI_DM_SLOT_HEIGHT;y++)for(unsigned at=0;at<WILI_DM_SLOT_WIDTH*4;at+=128)
            __asm__ volatile("dcbst 0,%0"::"r"((char*)(row+y*WILI_DM_ATLAS_WIDTH)+at):"memory");
        __sync_synchronize();revision++;
    }
    if(player.expired!=reported_expired && (!last_overload_ms || ms-last_overload_ms>=5000)){
        char line[128];snprintf(line,sizeof(line),"DANMAKU_OVERLOAD expired=%u slots=%u density=%u",player.expired,WILI_DM_LANES,density);logger(line);
        reported_expired=player.expired;last_overload_ms=ms;
    }
    wili_dm_geometry(&player,ms,&draw);draw.atlas_offset=atlas_offset;if(before!=draw.count)revision++;
    return revision;
}
const wili_dm_draw *danmaku_draw_list(void){return initialized?&draw:NULL;}
void danmaku_shutdown(void)
{
    if(!initialized)return;
    __sync_lock_test_and_set(&running,0);u64 result;sysThreadJoin(thread,&result);
    wili_dm_segment_free(&current);for(unsigned i=0;i<2;i++){wili_dm_segment_free(&cache[i].segment);cache[i].ready=0;}
    sysMutexDestroy(mutex);if(!gcmUnmapIoAddress(atlas_offset))free(atlas);atlas=NULL;initialized=0;
}
