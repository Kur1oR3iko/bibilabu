// SPDX-License-Identifier: GPL-3.0-or-later
// One quiet app-mixed PS3 audio port plays a short synthetic two-note XMB tick.
#include "ui_sound.h"
#include "audio_runtime.h"
#include "ui_chime.h"
#include <audio/audio.h>
#include <sys/thread.h>
#include <sys/systime.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#define BLOCK 256u
#define BLOCKS 8u
#define QUEUE 8u
#define NOTE1 1728u // 36ms at 48kHz
#define NOTE2_START 1056u
#define NOTE2_END 3840u // 80ms total
typedef struct {uint64_t start;} click_event;
static click_event events[QUEUE];
static volatile uint32_t head,tail,running,worker_done,ready,muted;
static volatile uint64_t frames_written;
static float chime[UI_CHIME_FRAMES];
static uint64_t last_tone_end;
static sys_ppu_thread_t thread;
static audioPortConfig config;
static uint32_t port;
static int port_open,port_running,runtime_owned;
static void (*logger)(const char*);

static void rc_log(const char *name,int32_t rc)
{
    if(logger){char line[112];snprintf(line,sizeof(line),"%s rc=%08x",name,(unsigned)rc);logger(line);}
}
void ui_sound_click(void)
{
    if(!__sync_add_and_fetch(&ready,0) || __sync_add_and_fetch(&muted,0))return;
    uint32_t h=__sync_add_and_fetch(&head,0),t=__sync_add_and_fetch(&tail,0);
    if(h-t>=QUEUE)return;
    events[h%QUEUE].start=__sync_add_and_fetch(&frames_written,0)+BLOCK;
    __sync_synchronize();__sync_lock_test_and_set(&head,h+1);
}
void ui_sound_set_muted(int value){__sync_lock_test_and_set(&muted,value!=0);}
static void mix_block(float *dst,uint64_t start)
{
    uint32_t consumed=__sync_add_and_fetch(&tail,0),submitted=__sync_add_and_fetch(&head,0);
    memset(dst,0,BLOCK*2*sizeof(float));
    for(uint32_t e=consumed;e!=submitted;e++){
        uint64_t begin=events[e%QUEUE].start,end=begin+UI_CHIME_FRAMES;
        if(begin>=start+BLOCK)break;
        if(end>start){
            unsigned lead=begin>start?(unsigned)(begin-start):0;
            unsigned offset=start>begin?(unsigned)(start-begin):0;
            ui_chime_mix(dst+lead*2,BLOCK-lead,chime,offset);
            if(end>last_tone_end)last_tone_end=end;
        }
        if(e==consumed && end<=start+BLOCK)consumed++;
    }
    __sync_lock_test_and_set(&tail,consumed);
    __sync_synchronize();__sync_lock_test_and_set(&frames_written,start+BLOCK);
}
static void worker(void *arg)
{
    (void)arg;
    volatile uint64_t *read=(volatile uint64_t*)(uintptr_t)config.readIndex;
    unsigned last=(unsigned)*read;float *data=(void*)(uintptr_t)config.audioDataStart;int idle=0;
    // Leave the currently scanned block silent, then fill the rest of the ring.
    for(unsigned i=1;i<BLOCKS;i++)mix_block(data+((last+i)%BLOCKS)*BLOCK*2,frames_written);
    while(__sync_add_and_fetch(&running,0)){
        unsigned current=(unsigned)*read;
        if(current>=BLOCKS){logger("UI_SOUND_INVALID_READ_INDEX");break;}
        int quiet=__sync_add_and_fetch(&muted,0) ||
            (__sync_add_and_fetch(&head,0)==__sync_add_and_fetch(&tail,0) && frames_written>=last_tone_end+BLOCKS*BLOCK);
        if(quiet){
            if(!idle)memset(data,0,config.portSize);
            if(muted)__sync_lock_test_and_set(&tail,__sync_add_and_fetch(&head,0));
            last=current;idle=1;sysUsleep(20000);continue;
        }
        if(idle){for(unsigned i=1;i<BLOCKS;i++)mix_block(data+((current+i)%BLOCKS)*BLOCK*2,frames_written);last=current;idle=0;}
        unsigned delta=(current+BLOCKS-last)%BLOCKS;
        for(unsigned i=0;i<delta;i++)mix_block(data+((last+i)%BLOCKS)*BLOCK*2,frames_written);
        last=current;sysUsleep(4000);
    }
    __sync_lock_test_and_set(&worker_done,1);sysThreadExit(0);
}
int ui_sound_start(void (*log)(const char*))
{
    if(ready)return 0;
    logger=log;
    if(ui_audio_runtime_acquire(log))return -1;
    runtime_owned=1;
    audioPortParam params={2,BLOCKS,AUDIO_PORT_INITLEVEL,0.30f};
    s32 rc=audioPortOpen(&params,&port);rc_log("UI_SOUND_PORT_OPEN",rc);
    if(rc)goto fail;
    port_open=1;
    rc=audioGetPortConfig(port,&config);rc_log("UI_SOUND_PORT_CONFIG",rc);
    if(rc || config.channelCount!=2 || config.numBlocks!=BLOCKS ||
       config.portSize!=BLOCKS*BLOCK*2*sizeof(float) || !config.readIndex || !config.audioDataStart)goto fail;
    memset((void*)(uintptr_t)config.audioDataStart,0,config.portSize);
    rc=audioPortStart(port);rc_log("UI_SOUND_PORT_START",rc);if(rc)goto fail;port_running=1;
    running=1;worker_done=0;head=tail=0;frames_written=0;last_tone_end=0;muted=0;
    ui_chime_generate(chime);
    rc=sysThreadCreate(&thread,worker,NULL,1100,32768,THREAD_JOINABLE,"wili-ui-sound");
    rc_log("UI_SOUND_WORKER_CREATE",rc);if(rc){running=0;goto fail;}
    ready=1;if(log)log("UI_SOUND_READY sample_rate=48000 two_note_tick");return 0;
fail:
    if(port_running){int32_t close_rc=audioPortStop(port);rc_log("UI_SOUND_PORT_STOP",close_rc);port_running=0;}
    if(port_open){int32_t close_rc=audioPortClose(port);rc_log("UI_SOUND_PORT_CLOSE",close_rc);if(close_rc)return -1;port_open=0;}
    if(runtime_owned){ui_audio_runtime_release(log);runtime_owned=0;}return -1;
}
int ui_sound_stop(void)
{
    if(!runtime_owned)return 0;
    __sync_lock_test_and_set(&ready,0);__sync_lock_test_and_set(&running,0);
    uint64_t begin=0;__asm__ volatile("mftb %0":"=r"(begin));
    uint64_t freq=sysGetTimebaseFrequency();
    while(!__sync_add_and_fetch(&worker_done,0)){
        uint64_t now=0;__asm__ volatile("mftb %0":"=r"(now));
        if(freq && (now-begin)/freq>2)return -1;
        sysUsleep(1000);
    }
    uint64_t result=0;rc_log("UI_SOUND_WORKER_JOIN",sysThreadJoin(thread,&result));
    if(port_running){int32_t rc=audioPortStop(port);rc_log("UI_SOUND_PORT_STOP",rc);if(rc)return -1;port_running=0;}
    if(port_open){int32_t rc=audioPortClose(port);rc_log("UI_SOUND_PORT_CLOSE",rc);if(rc)return -1;port_open=0;}
    int rc=ui_audio_runtime_release(logger);if(rc)return -1;runtime_owned=0;return 0;
}
