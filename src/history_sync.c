// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE
#include "history_sync.h"
#include "https_request.h"
#include "bili_api.h"
#include "bili_detail.h"
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>
#if defined(__PPU__) || defined(__CELLOS_LV2__)
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/systime.h>
static sys_mutex_t mutex;static sys_ppu_thread_t thread;
#define LOCK() sysMutexLock(mutex,0)
#define UNLOCK() sysMutexUnlock(mutex)
#define SLEEP() sysUsleep(20000)
#define THREAD_RETURN void
static uint64_t clock_us(void){return (uint64_t)sysGetSystemTime();}
#else
#include <pthread.h>
#include <unistd.h>
#include <time.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;static pthread_t thread;
#define LOCK() pthread_mutex_lock(&mutex)
#define UNLOCK() pthread_mutex_unlock(&mutex)
#define SLEEP() usleep(20000)
#define THREAD_RETURN void*
static uint64_t clock_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
#endif
#define QUEUE_SIZE 8
static struct {uint64_t aid,cid;unsigned progress,duration,sequence,failures;uint64_t retry;} queue[QUEUE_SIZE];
static struct {uint64_t aid,cid,duration,last_tick,last_report;unsigned progress;int seen,paused,ended,enabled;} playing;
static char account[1024],csrf[33],mid[32];
static unsigned sequence,epoch;static volatile unsigned running,closing;
static uint64_t close_deadline;
static int started,status;static const char *ca_path;static void (*logger)(const char*);
static int valid_account(const char *cookie,char *token,char *uid)
{
    char session[1024];
    return cookie && *cookie && strlen(cookie)<sizeof(account) &&
        !bili_detail_cookie_value(cookie,"bili_jct",token,33) && strlen(token)==32 && strspn(token,"0123456789abcdefABCDEF")==32 &&
        !bili_detail_cookie_value(cookie,"DedeUserID",uid,32) && *uid && strspn(uid,"0123456789")==strlen(uid) &&
        !bili_detail_cookie_value(cookie,"SESSDATA",session,sizeof(session));
}
typedef struct {unsigned epoch;uint64_t deadline;} request_guard;
static int canceled(void *arg)
{
    request_guard *g=arg;int stop;
    LOCK();stop=!running || epoch!=g->epoch || clock_us()>=g->deadline || (closing && clock_us()>=close_deadline);UNLOCK();return stop;
}
static void enqueue(uint64_t aid,uint64_t cid,unsigned progress,unsigned duration)
{
    if(!started || !aid || !cid)return;
    LOCK();if(!account[0] || closing){UNLOCK();return;}
    unsigned slot=QUEUE_SIZE,oldest=0;
    for(unsigned i=0;i<QUEUE_SIZE;i++){
        if(queue[i].sequence && queue[i].aid==aid && queue[i].cid==cid){slot=i;break;}
        if(!queue[i].sequence)slot=i;
        if(queue[i].sequence<queue[oldest].sequence)oldest=i;
    }
    if(slot==QUEUE_SIZE){slot=oldest;if(logger)logger("HISTORY_QUEUE_FULL oldest_pending_replaced");}
    // Retain failure backoff when a newer position replaces an unsent update.
    if(queue[slot].aid!=aid || queue[slot].cid!=cid)memset(&queue[slot],0,sizeof(queue[slot]));
    queue[slot].aid=aid;queue[slot].cid=cid;queue[slot].progress=progress;queue[slot].duration=duration;
    queue[slot].sequence=++sequence;status=1;UNLOCK();
}
static THREAD_RETURN worker(void *arg)
{
    (void)arg;
    while(1){
        unsigned slot=QUEUE_SIZE,job_sequence=0,progress=0,duration=0;uint64_t aid=0,cid=0;
        char cookie[1024],token[33],uid[32];request_guard guard={0};
        LOCK();uint64_t now=clock_us();
        if(!running || (closing && now>=close_deadline)){UNLOCK();break;}
        unsigned oldest=~0u;
        for(unsigned i=0;i<QUEUE_SIZE;i++)if(queue[i].sequence && (closing || now>=queue[i].retry) && queue[i].sequence<oldest){slot=i;oldest=queue[i].sequence;}
        if(slot<QUEUE_SIZE){
            job_sequence=queue[slot].sequence;aid=queue[slot].aid;cid=queue[slot].cid;progress=queue[slot].progress;duration=queue[slot].duration;
            snprintf(cookie,sizeof(cookie),"%s",account);snprintf(token,sizeof(token),"%s",csrf);snprintf(uid,sizeof(uid),"%s",mid);
            guard.epoch=epoch;guard.deadline=now+8000000;
        }else if(closing){UNLOCK();break;}
        UNLOCK();if(slot==QUEUE_SIZE){SLEEP();continue;}
        char form[320];snprintf(form,sizeof(form),"mid=%s&csrf=%s&aid=%llu&cid=%llu&progress=%u&duration=%u&type=3&dt=2",uid,token,(unsigned long long)aid,(unsigned long long)cid,progress,duration);
        wili_https_response r={0};int rc=wili_https_post_form("https://api.bilibili.com/x/v2/history/report",ca_path,cookie,form,65536,canceled,&guard,&r,logger);
        int code=-1;struct json_object *root=!rc && r.status==200?wili_bili_json_parse(r.body,r.bytes):NULL,*value=NULL;
        if(root && json_object_object_get_ex(root,"code",&value) && json_object_is_type(value,json_type_int))code=json_object_get_int(value);
        if(root)json_object_put(root);
        int ok=!rc && r.status==200 && code==0;
        LOCK();if(epoch==guard.epoch && running){
            char line[192];snprintf(line,sizeof(line),"HISTORY_REPORT aid=%llu cid=%llu progress=%u result=%s http=%ld code=%d",(unsigned long long)aid,(unsigned long long)cid,progress,ok?"accepted":"retry",r.status,code);if(logger)logger(line);
            if(queue[slot].aid==aid && queue[slot].cid==cid){
                if(ok){if(queue[slot].sequence==job_sequence)memset(&queue[slot],0,sizeof(queue[slot]));else{queue[slot].retry=0;queue[slot].failures=0;}}
                else{if(queue[slot].failures<4)queue[slot].failures++;unsigned delay=5u<<queue[slot].failures;if(delay>60 || code==-101 || code==-111)delay=60;queue[slot].retry=clock_us()+(uint64_t)delay*1000000;}
            }
            status=ok?2:-1;
            // One exit attempt per queued update; never spin on a failed POST.
            if(!ok && closing)running=0;
        }UNLOCK();wili_https_response_free(&r);
    }
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    sysThreadExit(0);
#else
    return NULL;
#endif
}
int history_sync_init(const char *ca,void (*log)(const char*))
{
    if(started)return 0;
    ca_path=ca;logger=log;running=1;closing=0;int rc;
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    sys_mutex_attr_t attr;sysMutexAttrInitialize(attr);if(sysMutexCreate(&mutex,&attr)){running=0;return -1;}
    rc=sysThreadCreate(&thread,worker,NULL,1250,131072,THREAD_JOINABLE,"bibi-history");
    if(rc)sysMutexDestroy(mutex);
#else
    rc=pthread_create(&thread,NULL,worker,NULL);
#endif
    if(rc){running=0;return -1;}started=1;return 0;
}
void history_sync_account(const char *cookie)
{
    if(!started)return;
    char token[33]={0},uid[32]={0};int valid=valid_account(cookie,token,uid);
    LOCK();if(strcmp(account,valid?cookie:"")){
        epoch++;memset(queue,0,sizeof(queue));status=0;
        snprintf(account,sizeof(account),"%s",valid?cookie:"");snprintf(csrf,sizeof(csrf),"%s",valid?token:"");snprintf(mid,sizeof(mid),"%s",valid?uid:"");
    }UNLOCK();
}
void history_sync_begin(uint64_t aid,uint64_t cid,uint64_t duration_ms,const char *cookie)
{
    history_sync_account(cookie);memset(&playing,0,sizeof(playing));if(!started)return;
    LOCK();playing.enabled=account[0]!=0;UNLOCK();playing.aid=aid;playing.cid=cid;playing.duration=duration_ms;
}
static unsigned seconds_at(uint64_t ms)
{if(playing.duration && ms>playing.duration)ms=playing.duration;return ms>86400000?86400:(unsigned)(ms/1000);}
void history_sync_tick(uint64_t ms,int paused,int ended,uint64_t now)
{
    if(!started || !playing.enabled)return;
    unsigned sec=seconds_at(ended?playing.duration:ms);
    int changed=sec!=playing.progress;
    int seek=playing.seen && ((sec>playing.progress && sec-playing.progress>3) || (sec<playing.progress && playing.progress-sec>1));
    int report=!playing.seen || (changed && now-playing.last_report>=15000000) || (paused!=playing.paused) || (ended && !playing.ended) || seek;
    if(report){enqueue(playing.aid,playing.cid,sec,(unsigned)(playing.duration/1000));playing.last_report=now;}
    playing.seen=1;playing.progress=sec;playing.paused=paused;playing.ended=ended;playing.last_tick=now;
}
void history_sync_end(uint64_t ms,int ended)
{
    if(playing.enabled && playing.seen)enqueue(playing.aid,playing.cid,seconds_at(ended?playing.duration:ms),(unsigned)(playing.duration/1000));
    playing.enabled=0;
}
void history_sync_close_begin(unsigned drain_ms)
{
    if(!started)return;
    LOCK();if(!closing){closing=1;close_deadline=clock_us()+(uint64_t)drain_ms*1000;}if(!drain_ms)running=0;UNLOCK();
}
void history_sync_shutdown(unsigned drain_ms)
{
    if(!started)return;
    history_sync_close_begin(drain_ms);
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    u64 result;sysThreadJoin(thread,&result);sysMutexDestroy(mutex);
#else
    pthread_join(thread,NULL);
#endif
    started=0;running=closing=0;
}
int history_sync_status(void)
{if(!started)return 0;LOCK();int value=status;UNLOCK();return value;}
