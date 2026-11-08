// SPDX-License-Identifier: GPL-3.0-or-later
// Incremental fragmented-MP4 AAC decoding with bounded compressed/PCM rings.
#include "native_audio_probe.h"
#include "adec_compat.h"
#include "adec_bindings.h"
#include "aac_test_stream.h"
#include "avc_test_stream.h"
#include "test_audio_identity.h"
#include "mp4_file.h"
#include "pcm_ring.h"
#include <audio/audio.h>
#include <sysmodule/sysmodule.h>
#include <sys/file.h>
#include <sys/systime.h>
#include <sys/thread.h>
#include <sysutil/sysutil.h>
#include <ppu-asm.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#define RATE 48000u
#define TOTAL (WILI_AUDIO_DURATION_SECONDS*RATE)
#define PCM_RING 32768u
#define PREFILL 16384u
#define AUDIO_POOL 8u
#define AUDIO_PACKET 8192u
#define BLOCK 256u
#define BLOCKS 32u
#define LEAD 4u
#define SCRATCH 65536u
_Static_assert(sizeof(audioPortParam)==32 && sizeof(audioPortConfig)==32,"Audio port ABI");

static struct {
    void (*log)(const char*);
    const wili_adec_api *api;
    uint8_t *file;void *work;float *scratch,*pcm;
    wili_mp4_file source;
    wili_mp4_sample next;size_t packet_bytes;
    int packet_ready,input_eof,decode_complete;
    uint32_t adec,opd[2],fed,decoded,copied;
    volatile uint32_t pending,au_done,seq_done,decode_error,produced,buffering,rebuffers;
    int adec_open,sequence,ending,initialized,prepared,worker_started,reported,reported_finished;
    sys_ppu_thread_t thread;
    volatile uint32_t running,thread_done,pause_request,consumed,finished,error,error_stage;
    uint64_t frequency;
    uint32_t port;
    int port_open,port_running;
    audioPortConfig config;
    uint32_t base,queued,last_read,hardware_blocks;
    uint64_t last_poll,last_stream_log;
} a;
static uint32_t snap(volatile uint32_t *p){return __sync_add_and_fetch(p,0);}
static void set(volatile uint32_t *p,uint32_t n){__sync_lock_test_and_set(p,n);}
static uint64_t time_us(void)
{
    uint64_t ticks;__asm__ volatile("mftb %0":"=r"(ticks));
    return ticks/a.frequency*1000000+(ticks%a.frequency)*1000000/a.frequency;
}
static void rc_log(const char *name,int32_t rc)
{
    char line[128];snprintf(line,sizeof(line),"%s rc=0x%08x",name,(uint32_t)rc);a.log(line);
}
static uint32_t aac_callback(uint32_t handle,uint32_t type,uint32_t data,uint32_t arg)
{
    (void)handle;(void)arg;
    if(type==0)__sync_add_and_fetch(&a.au_done,1);
    else if(type==1)__sync_add_and_fetch(&a.pending,1);
    else if(type==2)set(&a.decode_error,data ? data : 1);
    else if(type==3)set(&a.seq_done,1);
    return 0;
}
static int read_fixture(void)
{
    if(wili_mp4_file_open(&a.source,WILI_AUDIO_FILENAME,WILI_AUDIO_BYTES,WILI_AUDIO_CRC32,1,a.log))return -1;
    if(a.source.reader.timescale!=RATE){a.log("AAC_MP4_TIMESCALE_REJECTED");return -1;}
    a.file=memalign(128,AUDIO_POOL*AUDIO_PACKET);
    if(!a.file)return -1;
    a.log("AAC_FMP4_SOURCE_READY AU_pool=8 packet_cap=8192 PCM_ring_samples=32768 whole_file_allocation=0");return 0;
}

static int pull_pcm(int discard)
{
    if(!snap(&a.pending))return 0;
    uint32_t address=0;int32_t rc=a.api->item(a.adec,&address);
    if((uint32_t)rc==WILI_ADEC_EMPTY)return 0;
    if(rc || !address){rc_log("ADEC_PCM_ITEM",rc);return -1;}
    const wili_adec_pcm *item=(const void*)(uintptr_t)address;
    const wili_aac_info_prefix *info=(const void*)(uintptr_t)item->bsi;
    unsigned slots=item->size/(1024*sizeof(float));
    int bad=item->status || item->size%(1024*sizeof(float)) || slots<2 || slots>8 || item->size>SCRATCH || !info ||
            info->rate!=RATE || info->channels!=2 || info->sbr || item->au.userdata!=a.decoded;
    if(bad && !discard){
        char line[192];
        snprintf(line,sizeof(line),"AAC_PCM_ITEM status=%08x bytes=%u rate=%u channels=%u sbr=%u userdata=%llu expected=%u",item->status,item->size,info ? info->rate : 0,info ? info->channels : 0,info ? info->sbr : 0,(unsigned long long)item->au.userdata,a.decoded);
        a.log(line);
    }
    rc=a.api->pcm(a.adec,(discard || bad) ? NULL : a.scratch);
    if(rc){rc_log("ADEC_GET_PCM",rc);return -1;}
    __sync_sub_and_fetch(&a.pending,1);
    if(discard)return 1;
    if(bad){a.log("AAC_PCM_REJECTED status_size_rate_channels_or_order");return -1;}
    unsigned i;
    for(i=0;i<256;i++)if(((uint8_t*)a.scratch)[SCRATCH+i]!=0xa5){a.log("AAC_PCM_GUARD_CHANGED");return -1;}
    // Fixture-only priming trim. Keep unconsumed samples in the ring until
    // hardware consumption, so pause/rebuffer can discard queued port blocks
    // without losing their source PCM.
    if(a.decoded){
        unsigned count=TOTAL-a.copied;if(count>1024)count=1024;
        if(wili_pcm_ring_write(a.pcm,PCM_RING,a.copied,snap(&a.consumed),a.scratch,count,slots)){a.log("PCM_RING_WRITE_REJECTED");return -1;}
        a.copied+=count;__sync_synchronize();set(&a.produced,a.copied);
    }
    a.decoded++;
    if(a.decoded==1)a.log("FIRST_NATIVE_AAC_PCM_RETRIEVED");
    return 1;
}
static int close_decoder(void)
{
    if(a.adec_open){
        if(a.sequence && (!snap(&a.seq_done) || snap(&a.pending))){
            uint64_t begin=time_us();
            while(time_us()-begin<2000000){
                if(snap(&a.pending) && pull_pcm(1)<0)break;
                if(!a.ending){int32_t rc=a.api->end(a.adec);if(!rc)a.ending=1;else if((uint32_t)rc!=WILI_ADEC_BUSY)break;}
                if(snap(&a.seq_done) && !snap(&a.pending))break;
                sysUsleep(1000);
            }
            if(!snap(&a.seq_done) || snap(&a.pending)){a.log("AAC_DRAIN_INCOMPLETE retain_decoder_buffers");return -1;}
        }
        a.log("ADEC_CLOSE_BEGIN");int32_t rc=a.api->close(a.adec);rc_log("ADEC_CLOSE",rc);if(rc)return -1;
        a.adec_open=0;
    }
    free(a.file);free(a.work);free(a.scratch);a.file=NULL;a.work=NULL;a.scratch=NULL;
    return 0;
}
int wili_audio_pump(void)
{
    if(a.decode_complete)return 0;
    if(!a.sequence || snap(&a.decode_error)){
        if(snap(&a.decode_error))rc_log("ADEC_CALLBACK_ERROR",(int32_t)snap(&a.decode_error));
        return -1;
    }
    uint64_t now=time_us();
    if(now-a.last_stream_log>=1000000){
        char line[192];snprintf(line,sizeof(line),"AUDIO_STREAM_BUFFER produced=%u consumed=%u held=%u capacity=%u fragments=%u rebuffers=%u",snap(&a.produced),snap(&a.consumed),a.copied-snap(&a.consumed),PCM_RING,a.source.reader.fragments,snap(&a.rebuffers));a.log(line);a.last_stream_log=now;
    }
    unsigned burst;
    for(burst=0;burst<4;burst++){
        unsigned held=a.copied-snap(&a.consumed);
        if(held>PCM_RING){a.log("PCM_RING_OWNERSHIP_ERROR");return -1;}
        if(held>PCM_RING-1024)break;
        if(pull_pcm(0)<0)return -1;
        if(!a.input_eof && a.fed-snap(&a.au_done)<AUDIO_POOL){
            uint8_t *packet=a.file+(a.fed%AUDIO_POOL)*AUDIO_PACKET;
            if(!a.packet_ready){
                int result=wili_fmp4_packet(&a.source.reader,packet,AUDIO_PACKET,&a.packet_bytes,&a.next);
                if(result<0){a.log("MP4_AAC_PACKET_FAILED");return -1;}
                if(!result)a.input_eof=1;
                else {
                    if(a.next.pts!=a.next.dts || a.next.dts!=(uint64_t)a.fed*1024 || a.next.duration>1024){a.log("MP4_AAC_TIMING_REJECTED");return -1;}
                    a.packet_ready=1;
                }
            }
            if(a.packet_ready){
                unsigned cache_line;
                for(cache_line=0;cache_line<a.packet_bytes;cache_line+=128)
                    __asm__ volatile("dcbst 0,%0"::"r"(packet+cache_line):"memory");
                __sync_synchronize();
                uint64_t ticks=a.next.pts/RATE*90000+(a.next.pts%RATE)*90000/RATE;
                wili_adec_au au={(uint32_t)(uintptr_t)packet,(uint32_t)a.packet_bytes,{(uint32_t)(ticks>>32),(uint32_t)ticks},a.fed};
                int32_t rc=a.api->decode(a.adec,&au);
                if(!rc){a.fed++;a.packet_ready=0;}
                else if((uint32_t)rc!=WILI_ADEC_BUSY){rc_log("ADEC_DECODE",rc);return -1;}
            }
        }
        if(a.input_eof && !a.ending){
            int32_t rc=a.api->end(a.adec);if(!rc)a.ending=1;else if((uint32_t)rc!=WILI_ADEC_BUSY){rc_log("ADEC_END",rc);return -1;}
        }
        if(a.ending && snap(&a.seq_done) && !snap(&a.pending)){
            if(a.fed!=WILI_AUDIO_AUS || a.decoded!=a.fed || snap(&a.au_done)!=a.fed || a.copied!=TOTAL){a.log("AAC_STREAM_COUNT_MISMATCH");return -1;}
            char line[160];snprintf(line,sizeof(line),"AAC_STREAM_DECODE_COMPLETE aus=%u samples=%u fragments=%u PCM_capacity=%u",a.decoded,a.copied,a.source.reader.fragments,PCM_RING);a.log(line);
            if(close_decoder())return -1;
            a.decode_complete=1;return 0;
        }
    }
    return 0;
}
int wili_audio_prepare(void (*log)(const char*))
{
    memset(&a,0,sizeof(a));a.log=log;a.frequency=sysGetTimebaseFrequency();
    a.log("AUDIO_07_PREPARE_BEGIN streaming_fMP4_AAC");
    if(!a.frequency || read_fixture())return -1;
    a.log("ADEC_BASE_MODULE_LOAD_BEGIN");
    int32_t rc=sysModuleLoad(SYSMODULE_ADEC);rc_log("ADEC_BASE_MODULE_LOAD",rc);
    if(rc && (uint32_t)rc!=SYSMODULE_ERR_DUPLICATE)return -1;
    rc=sysModuleLoad(WILI_MODULE_M4AAC);rc_log("AAC_MODULE_LOAD",rc);
    if(rc && (uint32_t)rc!=SYSMODULE_ERR_DUPLICATE)return -1;
    __sync_synchronize();a.api=wili_resolve_adec(log);if(!a.api)return -1;
    wili_adec_type type={WILI_CODEC_M4AAC};wili_adec_attr attr={0};
    a.log("ADEC_QUERY_BEGIN");rc=a.api->query(&type,&attr);rc_log("ADEC_QUERY",rc);if(rc)return -1;
    if(!attr.memory || attr.memory>16*1024*1024)return -1;
    unsigned memory=(attr.memory+4095)&~4095u;
    {char line[96];snprintf(line,sizeof(line),"ADEC_WORK_MEMORY bytes=%u",memory);a.log(line);}
    a.work=memalign(128,memory);a.scratch=memalign(128,SCRATCH+256);a.pcm=memalign(128,PCM_RING*2*sizeof(float));
    if(!a.work || !a.scratch || !a.pcm){a.log("AUDIO_ALLOC_FAILED");return -1;}
    memset((uint8_t*)a.scratch+SCRATCH,0xa5,256);
    wili_adec_resource resource={memory,(uint32_t)(uintptr_t)a.work,450,250,16384};
    wili_adec_callback cb={__build_opd32(aac_callback,a.opd),0};
    a.log("ADEC_OPEN_BEGIN");rc=a.api->open(&type,&resource,&cb,&a.adec);rc_log("ADEC_OPEN",rc);if(rc)return -1;
    a.adec_open=1;wili_aac_param param={1,{0,0},1};
    a.log("ADEC_START_BEGIN");rc=a.api->start(a.adec,&param);rc_log("ADEC_START",rc);if(rc)return -1;
    a.sequence=1;uint64_t begin=time_us();
    while(snap(&a.produced)<PREFILL && time_us()-begin<5000000){
        sysUtilCheckCallback();if(wili_audio_pump())return -1;sysUsleep(1000);
    }
    if(snap(&a.produced)<PREFILL){a.log("AAC_STREAM_PREFILL_TIMEOUT");return -1;}
    a.log("AAC_STREAM_PREFILL_READY samples>=16384 decoder_stays_open fixed_PCM_capacity=32768");
    rc=sysModuleLoad(SYSMODULE_AUDIO);rc_log("AUDIO_MODULE_LOAD",rc);if(rc && (uint32_t)rc!=SYSMODULE_ERR_DUPLICATE)return -1;
    rc=audioInit();rc_log("AUDIO_INIT",rc);if(rc)return -1;
    a.initialized=1;a.prepared=1;a.log("AUDIO_STREAM_READY rate=48000 stereo=2 target_seconds=30");return 0;
}

static void worker_error(unsigned stage,int32_t rc)
{
    set(&a.error_stage,stage);set(&a.error,(uint32_t)(rc ? rc : -1));
}
static int write_block(unsigned block)
{
    float *dst=(void*)(uintptr_t)(a.config.audioDataStart+block*BLOCK*2*sizeof(float));
    unsigned n=TOTAL-a.queued;if(n>BLOCK)n=BLOCK;
    if(snap(&a.produced)-a.queued<n)return 0;
    if(wili_pcm_ring_read(a.pcm,PCM_RING,a.queued,snap(&a.produced),dst,n))return 0;
    if(n<BLOCK)memset(dst+n*2,0,(BLOCK-n)*2*sizeof(float));
    a.queued+=n;__sync_synchronize();return 1;
}

static int close_port(void)
{
    if(a.port_running){int32_t rc=audioPortStop(a.port);if(rc){worker_error(6,rc);return -1;}a.port_running=0;}
    if(a.port_open){int32_t rc=audioPortClose(a.port);if(rc){worker_error(7,rc);return -1;}a.port_open=0;}
    return 0;
}
static int open_segment(void)
{
    audioPortParam param={2,32,AUDIO_PORT_INITLEVEL,0.5f};
    int32_t rc=audioPortOpen(&param,&a.port);if(rc){worker_error(1,rc);return -1;}a.port_open=1;
    rc=audioGetPortConfig(a.port,&a.config);if(rc){worker_error(2,rc);return -1;}
    if(a.config.channelCount!=2 || a.config.numBlocks!=BLOCKS || a.config.portSize!=BLOCKS*BLOCK*2*sizeof(float) || !a.config.readIndex || !a.config.audioDataStart){worker_error(3,-1);return -1;}
    memset((void*)(uintptr_t)a.config.audioDataStart,0,a.config.portSize);
    rc=audioPortStart(a.port);if(rc){worker_error(4,rc);return -1;}a.port_running=1;
    a.last_read=(uint32_t)*(volatile uint64_t*)(uintptr_t)a.config.readIndex;
    if(a.last_read>=BLOCKS){worker_error(5,-1);return -1;}
    a.base=snap(&a.consumed);a.queued=a.base;a.hardware_blocks=0;
    // Start may clear the system buffer. Queue after start, safely ahead of the
    // current block; four silent preroll blocks provide a short write margin.
    unsigned delta;for(delta=LEAD;delta<BLOCKS;delta++)if(!write_block((a.last_read+delta)%BLOCKS)){worker_error(10,-1);return -1;}
    a.last_poll=time_us();return 0;
}
static int poll_port(void)
{
    uint64_t now=time_us();
    if(now-a.last_poll>100000){worker_error(8,-1);return -1;} // cannot infer full-ring wrap
    unsigned current=(unsigned)*(volatile uint64_t*)(uintptr_t)a.config.readIndex;
    if(current>=BLOCKS){worker_error(9,-1);return -1;}
    unsigned delta=(current+BLOCKS-a.last_read)%BLOCKS,i;
    a.hardware_blocks+=delta;
    uint32_t consumed=a.base+(a.hardware_blocks>LEAD ? (a.hardware_blocks-LEAD)*BLOCK : 0);
    if(consumed>TOTAL)consumed=TOTAL;
    set(&a.consumed,consumed);
    for(i=0;i<delta;i++){
        if(!write_block((a.last_read+i)%BLOCKS)){
            if(close_port())return -1;
            set(&a.buffering,1);__sync_add_and_fetch(&a.rebuffers,1);return 1;
        }
    }
    a.last_read=current;a.last_poll=now;
    return 0;
}
static void audio_worker(void *arg)
{
    (void)arg;
    while(snap(&a.running)){
        if(snap(&a.pause_request)){
            if(a.port_running){if(poll_port()<0 || close_port())break;}
            sysUsleep(1000);continue;
        }
        if(!a.port_open){
            unsigned available=snap(&a.produced)-snap(&a.consumed);
            unsigned remaining=TOTAL-snap(&a.consumed),want=remaining<PREFILL ? remaining : PREFILL;
            if(available<want){set(&a.buffering,1);sysUsleep(1000);continue;}
            if(open_segment())break;
            set(&a.buffering,0);
        }
        int poll=poll_port();if(poll<0)break;if(poll>0)continue;
        if(snap(&a.consumed)>=TOTAL){set(&a.finished,1);break;}
        sysUsleep(1000);
    }
    close_port();set(&a.thread_done,1);sysThreadExit(0);
}
int wili_audio_launch(void)
{
    if(!a.prepared)return -1;
    if(a.worker_started)return 0;
    set(&a.running,1);
    int32_t rc=sysThreadCreate(&a.thread,audio_worker,NULL,450,65536,THREAD_JOINABLE,"wili-audio");
    rc_log("AUDIO_WORKER_CREATE",rc);if(rc){set(&a.running,0);return -1;}
    a.worker_started=1;a.log("AUDIO_CLOCK_STARTED block_samples=256 ring_blocks=32 preroll_blocks=4");return 0;
}
uint32_t wili_audio_clock_samples(void){return snap(&a.consumed);}
void wili_audio_pause(int pause){if(a.prepared)set(&a.pause_request,pause ? 1 : 0);}
int wili_audio_failed(void)
{
    uint32_t error=snap(&a.error);
    if(error && !a.reported){char line[192];snprintf(line,sizeof(line),"AUDIO_WORKER_ERROR stage=%u rc=0x%08x channels=%llu blocks=%llu port_bytes=%u",snap(&a.error_stage),error,(unsigned long long)a.config.channelCount,(unsigned long long)a.config.numBlocks,a.config.portSize);a.log(line);a.reported=1;}
    return error!=0;
}
void wili_audio_stop(void)
{
    if(!a.log)return;
    if(a.worker_started){
        set(&a.running,0);uint64_t begin=time_us();
        while(!snap(&a.thread_done) && time_us()-begin<1000000)sysUsleep(1000);
        if(!snap(&a.thread_done)){a.log("AUDIO_WORKER_STOP_TIMEOUT retain_PCM_for_kernel_teardown");return;}
        uint64_t result=0;rc_log("AUDIO_WORKER_JOIN",sysThreadJoin(a.thread,&result));a.worker_started=0;
    }
    if(close_decoder())return;
    if(a.port_open && close_port()){a.log("AUDIO_PORT_CLOSE_FAILED");return;}
    if(a.initialized){rc_log("AUDIO_QUIT",audioQuit());a.initialized=0;}
    wili_mp4_file_close(&a.source);
    free(a.pcm);a.pcm=NULL;a.log("AUDIO_RESOURCES_RELEASED");
}
void wili_audio_status(char *out,unsigned capacity)
{
    uint32_t samples=snap(&a.consumed);
    snprintf(out,capacity,"Audio: %u.%02us / 30s%s",samples/RATE,(samples%RATE)*100/RATE,snap(&a.finished) ? " DONE" : snap(&a.buffering) ? " BUFFER" : "");
}
unsigned wili_audio_state(void)
{
    if(snap(&a.error))return 4;
    if(snap(&a.finished)){
        if(!a.reported_finished){a.log("AUDIO_PLAYBACK_COMPLETE samples=1440000");a.reported_finished=1;}
        return 3;
    }
    if(snap(&a.pause_request))return 2;
    return snap(&a.buffering) ? 5 : a.worker_started ? 1 : 0;
}
