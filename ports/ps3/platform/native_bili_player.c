// SPDX-License-Identifier: GPL-3.0-or-later
// 1.0 bounded native player for real Bilibili DASH media. Runtime-derived
// coded size, level, timescale, sample rate, duration and timestamps; B-frame
// reorder queue; 44100->48000 resampling; hardware-consumed audio clock.
// This is a PS3 playback backend, not the final wiliwili/Borealis UI.
#include "native_video_probe.h"
#include "native_audio_probe.h"
#include "bili_play.h"
#include "mp4_dash.h"
#include "resampler.h"
#include "pcm_ring.h"
#include "compressed_ring.h"
#include "video_buffer_policy.h"
#include "video_geometry.h"
#include "media_memory.h"
#include "vdec_ex.h"
#include "software_avc.h"
#include "audio_runtime.h"
#include "ui_sound.h"
#include "adec_bindings.h"
#include "adec_compat.h"
#include <codec/vdec.h>
#include <audio/audio.h>
#include <sysmodule/sysmodule.h>
#include <sys/spu.h>
#include <sys/file.h>
#include <sys/systime.h>
#include <sys/thread.h>
#include <sysutil/sysutil.h>
#include <ppu-asm.h>
#include <rsx/rsx.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(vdecType)==8 && sizeof(vdecAttr)==16,"vdec ABI");
_Static_assert(sizeof(vdecAU)==40 && offsetof(vdecAU,userdata)==24,"vdecAU ABI");
_Static_assert(sizeof(vdecPicture)==80 && offsetof(vdecPicture,codec_specific_addr)==72,"vdecPicture ABI");
_Static_assert(sizeof(audioPortParam)==32 && sizeof(audioPortConfig)==32,"audio port ABI");

#define VIDEO_POOL 8u
#define VIDEO_PACKET (512u*1024u)
#define AUDIO_POOL 8u
#define AUDIO_PACKET 8192u
#define REORDER_FRAMES 10u
// A synchronous HTTPS Range fetch (256KB media cache boundary) stalls the
// main thread for ~0.5s on real hardware. The old 32768-sample ring (0.68s)
// was drained by the audio port during that stall -> close_port/rebuffer ->
// a visible hiccup every ~8s (1.0.2 log: 20 HTTP_RANGE_OK == 20 rebuffers).
// 131072 samples = 2.73s of headroom; PREFILL 65536 = 1.37s.
#define PCM_RING 131072u
#define PREFILL 65536u
#define REOPEN_MIN 16384u
#define RESAMPLED_CAP 1200u
#define SCRATCH (65536u+256u)
#define BLOCK 256u
#define BLOCKS 32u
#define LEAD 4u
#define OUT_RATE 48000u
#define HTTP_BLOCK_SIZE (256u*1024u)
#define V_PREFETCH_SLOTS 128u
#define V_COMPRESSED_BYTES (16u*1024u*1024u)
#define A_PREFETCH_SLOTS 64u

typedef struct {
    wili_dash_sample sample;
    size_t bytes;
    volatile uint32_t ready;
    uint8_t *data;
    uint32_t offset;
} wili_prefetch_slot;

static wili_bili_session sess;
static struct {
    void (*log)(const char*);
    uint64_t frequency,started,pause_started,last_stream_log;
    unsigned coded_w,coded_h,display_w,display_h,level,refs,video_timescale,audio_rate;
    uint64_t video_bytes,duration_ms,start_pts,empty_edit_ms,total_out,audio_end_started;
    unsigned color_matrix,crop_x,crop_y,full_range,sar_w,sar_h;
    volatile u32 audio_final,final_samples;
    int failed,complete,paused,sequence,opened,mapped,audio_started,clock_started;
    void *frame_arena;size_t frame_stride,frame_arena_bytes;int display_slot;
    u32 handle,callback_opd[2],rsx_offset;
    wili_media_memory work_block,frame_block,compressed_block;
    int software,soft_worker_started;wili_software_avc *soft_codec;sys_ppu_thread_t soft_thread;
    volatile u32 soft_running,soft_done,soft_eof,soft_error;
    void *work;uint32_t *display;uint8_t *vpool,*apool;
    wili_yuv_layout frame_layout[REORDER_FRAMES];
    uint8_t *frames[REORDER_FRAMES];uint64_t frame_pts[REORDER_FRAMES];volatile int frame_used[REORDER_FRAMES];
    uint64_t pts_map[64],a_pts_map[64];uint32_t pts_tag[64];
    uint64_t seek_base_us,audio_target_pts;int hold_after_seek;
    volatile u32 au_done,pending,seq_done,fatal,rejected,picture_event_warnings;
    volatile u32 fed,decoded;unsigned presented,has_pixels,picture_warnings;
    size_t packet_bytes;wili_dash_sample next;int packet_ready,input_eof,ending;
    // network prefetch
    sys_ppu_thread_t net_thread[2];
    volatile uint32_t net_running,net_done[2];
    int net_worker_started;
    wili_compressed_ring compressed;
    uint8_t *net_scratch;
    volatile uint64_t v_ticks_queued;
    volatile u32 video_buffering;
    uint64_t buffer_started;unsigned video_rebuffers;
    wili_prefetch_slot v_slots[V_PREFETCH_SLOTS];
    wili_prefetch_slot a_slots[A_PREFETCH_SLOTS];
    volatile u32 v_read,v_write,a_read,a_write,net_error,v_eof,a_eof;
    volatile u32 v_queued_bytes,a_queued_bytes;
    // audio
    const wili_adec_api *api;uint32_t adec,adec_opd[2];void *adec_work;
    float *scratch,*pcm,*resampled;wili_resampler rs;
    uint32_t a_fed,a_decoded;volatile uint32_t a_produced;size_t a_packet_bytes;wili_dash_sample a_next;
    volatile uint32_t a_pending,a_au_done,a_seq_done,a_decode_error,a_buffering,a_rebuffers;
    int adec_open,a_sequence,a_ending,a_decode_complete,a_packet_ready,a_input_eof,a_reported,a_reported_finished;
    sys_ppu_thread_t thread;volatile uint32_t running,thread_done,pause_request,consumed,finished,error,error_stage;
    uint32_t port;int port_open,port_running,audio_init_done,prepared,worker_started;
    audioPortConfig config;uint32_t base,queued,last_read,hardware_blocks;uint64_t last_poll;
    int pcm_wait_logged,pcm_sanitize_logged,audio_fatal_logged;
} p;

static void rc_log(const char *name,s32 rc)
{
    char line[128];snprintf(line,sizeof(line),"%s rc=0x%08x",name,(u32)rc);p.log(line);
}
static void memory_log(const char *stage)
{
    struct mallinfo info=mallinfo();char line[224];
    snprintf(line,sizeof(line),"PLAYER_MEMORY_HEAP stage=%s arena=%u used=%u free=%u releasable=%u system_owned=%u",stage,
        (unsigned)info.arena,(unsigned)info.uordblks,(unsigned)info.fordblks,(unsigned)info.keepcost,
        (unsigned)(p.work_block.bytes+p.frame_block.bytes+p.compressed_block.bytes));p.log(line);
}
static u32 snap(volatile u32 *v){return __sync_add_and_fetch(v,0);}
static uint64_t video_queued_ms(void){return p.video_timescale?__sync_add_and_fetch(&p.v_ticks_queued,0)*1000/p.video_timescale:0;}
static int video_prefill_ready(void)
{
    return video_queued_ms()>=2500 || snap(&p.v_eof) ||
        snap(&p.v_write)-snap(&p.v_read)>=V_PREFETCH_SLOTS-4 ||
        snap(&p.compressed.head)-snap(&p.compressed.tail)>=V_COMPRESSED_BYTES-VIDEO_PACKET;
}
static void set(volatile u32 *v,u32 n){__sync_lock_test_and_set(v,n);}
static uint64_t time_us(void)
{
    uint64_t ticks;__asm__ volatile("mftb %0":"=r"(ticks));
    return ticks/p.frequency*1000000ULL+(ticks%p.frequency)*1000000ULL/p.frequency;
}
static uint32_t vdec_callback(u32 handle,u32 type,u32 data,u32 arg)
{
    (void)handle;(void)arg;
    if(type==VDEC_CALLBACK_AUDONE){if(data)__sync_add_and_fetch(&p.rejected,1);__sync_add_and_fetch(&p.au_done,1);}
    else if(type==VDEC_CALLBACK_PICOUT)__sync_add_and_fetch(&p.pending,1);
    else if(type==VDEC_CALLBACK_SEQDONE)__sync_lock_test_and_set(&p.seq_done,1);
    else if(type==VDEC_CALLBACK_ERROR){if(data==VDEC_ERROR_PIC)__sync_add_and_fetch(&p.picture_event_warnings,1);else __sync_lock_test_and_set(&p.fatal,data?data:1);}
    return 0;
}
static uint32_t aac_callback(uint32_t handle,uint32_t type,uint32_t data,uint32_t arg)
{
    (void)handle;(void)arg;
    if(type==0)__sync_add_and_fetch(&p.a_au_done,1);
    else if(type==1)__sync_add_and_fetch(&p.a_pending,1);
    else if(type==2)set(&p.a_decode_error,data?data:1);
    else if(type==3)set(&p.a_seq_done,1);
    return 0;
}
static void flush_cache(const void *base,size_t bytes)
{
    size_t at;for(at=0;at<bytes;at+=128)__asm__ volatile("dcbst 0,%0"::"r"((const uint8_t*)base+at):"memory");
    __sync_synchronize();
}
wili_bili_session *wili_bili_play_session(void){return &sess;}
unsigned wili_bili_play_revision(void){return sess.revision+p.decoded+p.presented;}

// --- network prefetch worker / ring buffer -----------------------------------
static int net_cancel(void *arg)
{
    (void)arg;
    return !snap(&p.net_running);
}
// The worker owns both MP4 parsers and both CURL handles. Consumers never
// enter a network read, and a packet stays owned until its ready flag is released.
static int queued_packet(int audio,void *dst,size_t cap,size_t *bytes,wili_dash_sample *sample)
{
    volatile u32 *read=audio?&p.a_read:&p.v_read,*write=audio?&p.a_write:&p.v_write;
    unsigned count=audio?A_PREFETCH_SLOTS:V_PREFETCH_SLOTS;
    wili_prefetch_slot *slots=audio?p.a_slots:p.v_slots;
    unsigned r=snap(read);
    if(r==snap(write))return snap(&p.net_error)?-1:(snap(audio?&p.a_eof:&p.v_eof)?0:2);
    wili_prefetch_slot *slot=&slots[r%count];
    if(!snap(&slot->ready))return 2;
    if(slot->bytes>cap)return -1;
    if(audio)memcpy(dst,slot->data,slot->bytes);
    else if(wili_compressed_pop(&p.compressed,dst,(u32)slot->bytes,slot->offset))return -1;
    *bytes=slot->bytes;*sample=slot->sample;
    if(!audio)__sync_sub_and_fetch(&p.v_ticks_queued,slot->sample.duration);
    __sync_sub_and_fetch(audio?&p.a_queued_bytes:&p.v_queued_bytes,(u32)slot->bytes);
    set(&slot->ready,0);set(read,r+1);return 1;
}
static void net_worker(void *arg)
{
    int audio=(int)(uintptr_t)arg;
    while(snap(&p.net_running)){
        volatile u32 *write=audio?&p.a_write:&p.v_write;
        unsigned w=snap(write),count=audio?A_PREFETCH_SLOTS:V_PREFETCH_SLOTS;
        if(snap(audio?&p.a_eof:&p.v_eof))break;
        if(w-snap(audio?&p.a_read:&p.v_read)>=count){sysUsleep(5000);continue;}
        wili_prefetch_slot *slot=&(audio?p.a_slots:p.v_slots)[w%count];
        int rc=wili_mp4_dash_packet(audio?&sess.audio:&sess.video,audio?slot->data:p.net_scratch,
                                   audio?AUDIO_PACKET:VIDEO_PACKET,&slot->bytes,&slot->sample);
        if(rc<0){if(snap(&p.net_running)){p.log(audio?"NETWORK_AAC_PACKET_FAILED":"NETWORK_AVC_PACKET_FAILED");set(&p.net_error,1);}break;}
        if(!rc){set(audio?&p.a_eof:&p.v_eof,1);continue;}
        if(!audio){
            do{rc=wili_compressed_push(&p.compressed,p.net_scratch,(u32)slot->bytes,&slot->offset);if(!rc)sysUsleep(5000);}
            while(!rc && snap(&p.net_running));
            if(rc<=0){if(rc<0)set(&p.net_error,1);break;}
            __sync_add_and_fetch(&p.v_ticks_queued,slot->sample.duration);
        }
        __sync_add_and_fetch(audio?&p.a_queued_bytes:&p.v_queued_bytes,(u32)slot->bytes);
        set(&slot->ready,1);set(write,w+1);
    }
    set(&p.net_done[audio],1);sysThreadExit(0);
}
static int prefetch_init(void)
{
    unsigned i;
    int memory_rc=wili_media_memory_alloc(&p.compressed_block,V_COMPRESSED_BYTES,1);rc_log("VIDEO_QUEUE_SYSTEM_ALLOC",memory_rc);
    if(memory_rc)return -1;
    p.compressed.data=p.compressed_block.data;p.compressed.capacity=V_COMPRESSED_BYTES;
    p.net_scratch=memalign(128,VIDEO_PACKET);
    if(!p.compressed.data || !p.net_scratch || wili_http_range_set_window(&sess.video_range,1024u*1024u))return -1;
    for(i=0;i<A_PREFETCH_SLOTS;i++){p.a_slots[i].data=memalign(128,AUDIO_PACKET);if(!p.a_slots[i].data)return -1;}
    sess.video_range.cancel=net_cancel;sess.audio_range.cancel=net_cancel;
    set(&p.net_running,1);
    for(i=0;i<2;i++){
        s32 rc=sysThreadCreate(&p.net_thread[i],net_worker,(void*)(uintptr_t)i,i?950:1000,262144,THREAD_JOINABLE,i?"bibi-aac-net":"bibi-avc-net");
        rc_log(i?"AUDIO_NET_WORKER_CREATE":"VIDEO_NET_WORKER_CREATE",rc);
        if(rc){set(&p.net_running,0);return -1;}p.net_worker_started|=1<<i;
    }
    p.log("PACKET_WORKER_READY independent_AV video_descriptors=128 video_arena=16MiB video_range=1MiB audio=64x8KiB");return 0;
}

// --- audio decoding / resampling ----------------------------------------------
static int pull_pcm(int discard)
{
    if(!snap(&p.a_pending))return 0;
    // A PCM item removed from cellAdec cannot be put back. Before taking it,
    // ensure the destination ring can hold a whole resampler output; otherwise
    // leave it queued and let the audio port catch up (no stalled playback).
    if(!discard){
        uint32_t cons=snap(&p.consumed);
        uint32_t held=p.a_produced-cons;
        if(p.a_produced<cons || held>PCM_RING || (uint64_t)held+RESAMPLED_CAP+64>PCM_RING){
            if(!p.pcm_wait_logged){p.log("PCM_RING_DEFERRED item_left_queued_until_port_consumes");p.pcm_wait_logged=1;}
            return 0;
        }
    }
    uint32_t address=0;int32_t rc=p.api->item(p.adec,&address);
    if((u32)rc==WILI_ADEC_EMPTY)return 0;
    if(rc || !address){rc_log("ADEC_PCM_ITEM",rc);return -1;}
    const wili_adec_pcm *item=(const void*)(uintptr_t)address;
    const wili_aac_info_prefix *info=(const void*)(uintptr_t)item->bsi;
    unsigned slots=item->size/(1024*sizeof(float));
    int bad=item->status || item->size%(1024*sizeof(float)) || slots<2 || slots>8 ||
            item->size>SCRATCH || !info || info->rate!=p.audio_rate || info->channels!=2 || info->sbr ||
            item->au.userdata!=p.a_decoded;
    if(bad && !discard){
        char line[192];snprintf(line,sizeof(line),"AAC_PCM_ITEM status=%08x bytes=%u rate=%u channels=%u sbr=%u userdata=%llu expected=%u",
            item->status,item->size,info?info->rate:0,info?info->channels:0,info?info->sbr:0,
            (unsigned long long)item->au.userdata,p.a_decoded);p.log(line);
    }
    rc=p.api->pcm(p.adec,(bad||discard)?NULL:p.scratch);
    if(rc){rc_log("ADEC_GET_PCM",rc);return -1;}
    __sync_sub_and_fetch(&p.a_pending,1);
    if(discard)return 1;
    if(bad){p.log("AAC_PCM_REJECTED status_size_rate_channels_or_order");return -1;}
    unsigned i;for(i=0;i<256;i++)if(((uint8_t*)p.scratch)[SCRATCH+i]!=0xa5){p.log("AAC_PCM_GUARD_CHANGED");return -1;}
    uint32_t produced=0,trim=0;
    if(p.seek_base_us){uint64_t pts=p.a_pts_map[p.a_decoded&63];if(pts<p.audio_target_pts){uint64_t distance=p.audio_target_pts-pts;trim=distance>1024?1024:(uint32_t)distance;}}
    if(trim<1024 && (wili_resampler_feed(&p.rs,p.scratch+trim*2,1024-trim,p.resampled,RESAMPLED_CAP,&produced) || produced>RESAMPLED_CAP))return -1;
    // Real 44100Hz cellAdec float PCM may contain NaN or slight >1 peaks. The
    // ring writer rejects those by design; sanitize instead of killing the
    // whole player, and report the first offending bit pattern once.
    uint32_t first_bits=0;
    uint32_t bad_samples=wili_pcm_sanitize(p.resampled,produced*2u,&first_bits);
    uint32_t cons=snap(&p.consumed),held=p.a_produced-cons;
    if(bad_samples && !p.pcm_sanitize_logged){
        char line[192];snprintf(line,sizeof(line),"PCM_SANITIZED bad=%u first_bits=%08x held=%u n=%u produced=%u consumed=%u",
            bad_samples,first_bits,held,produced,p.a_produced,cons);p.log(line);
        p.pcm_sanitize_logged=1;
    }
    if(produced && wili_pcm_ring_write(p.pcm,PCM_RING,p.a_produced,cons,p.resampled,produced,2)){
        char line[224];snprintf(line,sizeof(line),"PCM_RING_WRITE_REJECTED produced=%u consumed=%u held=%u n=%u bad=%u room=%u",
            p.a_produced,cons,held,produced,bad_samples,held<PCM_RING ? PCM_RING-held : 0);p.log(line);
        return -1;
    }
    __sync_add_and_fetch(&p.a_produced,(u32)produced);
    p.a_decoded++;
    if(p.a_decoded==1)p.log("FIRST_NATIVE_AAC_PCM_RETRIEVED");
    return 1;
}
static int close_adec(void)
{
    if(p.adec_open){
        if(p.a_sequence && (!snap(&p.a_seq_done) || snap(&p.a_pending))){
            uint64_t begin=time_us();
            while(time_us()-begin<2000000){
                if(snap(&p.a_pending) && pull_pcm(1)<0)break;
                if(!p.a_ending){int32_t rc=p.api->end(p.adec);if(!rc)p.a_ending=1;else if((u32)rc!=WILI_ADEC_BUSY)break;}
                if(snap(&p.a_seq_done) && !snap(&p.a_pending))break;
                sysUsleep(1000);
            }
            if(!snap(&p.a_seq_done) || snap(&p.a_pending)){p.log("AAC_DRAIN_INCOMPLETE retain_decoder_buffers");return -1;}
        }
        p.log("ADEC_CLOSE_BEGIN");int32_t rc=p.api->close(p.adec);rc_log("ADEC_CLOSE",rc);if(rc)return -1;
        p.adec_open=0;
    }
    free(p.apool);free(p.adec_work);free(p.scratch);free(p.resampled);
    p.apool=NULL;p.adec_work=NULL;p.scratch=NULL;p.resampled=NULL;
    return 0;
}
static int audio_pump(void)
{
    if(p.a_decode_complete)return 0;
    if(!p.a_sequence || snap(&p.a_decode_error)){
        if(snap(&p.a_decode_error))rc_log("ADEC_CALLBACK_ERROR",(int32_t)snap(&p.a_decode_error));
        return -1;
    }
    uint64_t now=time_us();
    if(now-p.last_stream_log>=1000000){
        uint32_t held_now=p.a_produced-snap(&p.consumed);
        char line[224];snprintf(line,sizeof(line),"AUDIO_STREAM_BUFFER produced=%u consumed=%u held=%u capacity=%u room=%u aus=%u rebuffers=%u",
            p.a_produced,snap(&p.consumed),held_now,PCM_RING,PCM_RING-held_now,p.a_decoded,snap(&p.a_rebuffers));p.log(line);p.last_stream_log=now;
    }
    unsigned burst;
    for(burst=0;burst<4;burst++){
        uint32_t held=p.a_produced-snap(&p.consumed);
        if(held>PCM_RING){p.log("PCM_RING_OWNERSHIP_ERROR");return -1;}
        if(held>PCM_RING-2048)break;
        if(pull_pcm(0)<0)return -1;
        if(!p.a_input_eof && p.a_fed-snap(&p.a_au_done)<AUDIO_POOL){
            uint8_t *packet=p.apool+(p.a_fed%AUDIO_POOL)*AUDIO_PACKET;
            if(!p.a_packet_ready){
                int rc=queued_packet(1,packet,AUDIO_PACKET,&p.a_packet_bytes,&p.a_next);
                if(rc==2)break;
                if(rc<0){p.log("MP4_AAC_PACKET_FAILED");return -1;}
                if(!rc)p.a_input_eof=1;else p.a_packet_ready=1;
            }
            if(p.a_packet_ready){
                flush_cache(packet,p.a_packet_bytes);
                uint64_t ticks=p.a_next.pts/p.audio_rate*90000ULL+(p.a_next.pts%p.audio_rate)*90000ULL/p.audio_rate;
                wili_adec_au au={(u32)(uintptr_t)packet,(u32)p.a_packet_bytes,{(u32)(ticks>>32),(u32)ticks},p.a_fed};
                p.a_pts_map[p.a_fed&63]=p.a_next.pts;
                int32_t rc=p.api->decode(p.adec,&au);
                if(!rc){p.a_fed++;p.a_packet_ready=0;}
                else if((u32)rc!=WILI_ADEC_BUSY){rc_log("ADEC_DECODE",rc);return -1;}
            }
        }
        if(p.a_input_eof && !p.a_ending){
            int32_t rc=p.api->end(p.adec);if(!rc)p.a_ending=1;else if((u32)rc!=WILI_ADEC_BUSY){rc_log("ADEC_END",rc);return -1;}
        }
        if(p.a_ending && snap(&p.a_seq_done) && !snap(&p.a_pending)){
            char line[160];snprintf(line,sizeof(line),"AAC_STREAM_DECODE_COMPLETE aus=%u source_samples=%llu output_samples=%u",
                p.a_decoded,(unsigned long long)(uint64_t)p.a_decoded*1024,p.a_produced);p.log(line);
            if(close_adec())return -1;
            set(&p.final_samples,snap(&p.a_produced));set(&p.audio_final,1);
            p.a_decode_complete=1;return 0;
        }
    }
    return 0;
}
static int audio_failed(void)
{
    u32 error=snap(&p.error);
    if(error && !p.a_reported){
        char line[192];snprintf(line,sizeof(line),"AUDIO_WORKER_ERROR stage=%u rc=0x%08x channels=%llu blocks=%llu port_bytes=%u",
            snap(&p.error_stage),error,(unsigned long long)p.config.channelCount,(unsigned long long)p.config.numBlocks,p.config.portSize);
        p.log(line);p.a_reported=1;
    }
    return error!=0;
}
// --- audio port worker ---------------------------------------------------------
static void worker_error(unsigned stage,int32_t rc){set(&p.error_stage,stage);set(&p.error,(u32)(rc?rc:-1));}
static uint64_t output_limit(void){return snap(&p.audio_final)?snap(&p.final_samples):p.total_out;}
static int write_block(unsigned block)
{
    float *dst=(void*)(uintptr_t)(p.config.audioDataStart+block*BLOCK*2*sizeof(float));
    uint64_t remaining=output_limit()>p.queued ? output_limit()-p.queued : 0;
    unsigned n=remaining>BLOCK ? BLOCK : (unsigned)remaining;
    u32 produced=snap(&p.a_produced);
    if(produced-p.queued<n)return 0;
    if(n && wili_pcm_ring_read(p.pcm,PCM_RING,p.queued,produced,dst,n))return 0;
    if(n<BLOCK)memset(dst+n*2,0,(BLOCK-n)*2*sizeof(float));
    p.queued+=n;__sync_synchronize();return 1;
}
static int close_port(void)
{
    if(p.port_running){int32_t rc=audioPortStop(p.port);if(rc){worker_error(6,rc);return -1;}p.port_running=0;}
    if(p.port_open){int32_t rc=audioPortClose(p.port);if(rc){worker_error(7,rc);return -1;}p.port_open=0;}
    return 0;
}
static int open_segment(void)
{
    audioPortParam param={2,32,AUDIO_PORT_INITLEVEL,0.5f};
    int32_t rc=audioPortOpen(&param,&p.port);if(rc){worker_error(1,rc);return -1;}p.port_open=1;
    rc=audioGetPortConfig(p.port,&p.config);if(rc){worker_error(2,rc);return -1;}
    if(p.config.channelCount!=2 || p.config.numBlocks!=BLOCKS || p.config.portSize!=BLOCKS*BLOCK*2*sizeof(float) ||
       !p.config.readIndex || !p.config.audioDataStart){worker_error(3,-1);return -1;}
    memset((void*)(uintptr_t)p.config.audioDataStart,0,p.config.portSize);
    rc=audioPortStart(p.port);if(rc){worker_error(4,rc);return -1;}p.port_running=1;
    p.last_read=(u32)*(volatile uint64_t*)(uintptr_t)p.config.readIndex;
    if(p.last_read>=BLOCKS){worker_error(5,-1);return -1;}
    p.base=snap(&p.consumed);p.queued=p.base;p.hardware_blocks=0;
    unsigned delta;for(delta=LEAD;delta<BLOCKS;delta++)if(!write_block((p.last_read+delta)%BLOCKS)){worker_error(10,-1);return -1;}
    p.last_poll=time_us();return 0;
}
static int poll_port(void)
{
    uint64_t now=time_us();
    if(now-p.last_poll>100000){worker_error(8,-1);return -1;}
    unsigned current=(unsigned)*(volatile uint64_t*)(uintptr_t)p.config.readIndex;
    if(current>=BLOCKS){worker_error(9,-1);return -1;}
    unsigned delta=(current+BLOCKS-p.last_read)%BLOCKS,i;
    p.hardware_blocks+=delta;
    uint64_t consumed=(uint64_t)p.base+(p.hardware_blocks>LEAD ? (uint64_t)(p.hardware_blocks-LEAD)*BLOCK : 0);
    if(consumed>output_limit())consumed=output_limit();
    set(&p.consumed,(u32)consumed);
    for(i=0;i<delta;i++){
        if(!write_block((p.last_read+i)%BLOCKS)){
            if(close_port())return -1;
            set(&p.a_buffering,1);__sync_add_and_fetch(&p.a_rebuffers,1);return 1;
        }
    }
    p.last_read=current;p.last_poll=now;return 0;
}
static void audio_worker(void *arg)
{
    (void)arg;
    while(snap(&p.running)){
        if(snap(&p.pause_request) || snap(&p.video_buffering)){
            if(p.port_running){if(poll_port()<0 || close_port())break;}
            sysUsleep(1000);continue;
        }
        if(!p.port_open){
            uint32_t available=snap(&p.a_produced)-snap(&p.consumed);
            uint64_t remaining=output_limit()>snap(&p.consumed) ? output_limit()-snap(&p.consumed) : 0;
            // Reopen as soon as a short buffer is available; do not wait for a
            // full PREFILL after an underflow, so a rare rebuffer stays short.
            uint32_t want=remaining<REOPEN_MIN ? (uint32_t)remaining : REOPEN_MIN;
            if(available<want){set(&p.a_buffering,1);sysUsleep(1000);continue;}
            if(open_segment())break;
            set(&p.a_buffering,0);
        }
        int poll=poll_port();if(poll<0)break;if(poll>0)continue;
        if((uint64_t)snap(&p.consumed)>=output_limit()){set(&p.finished,1);break;}
        sysUsleep(1000);
    }
    close_port();set(&p.thread_done,1);sysThreadExit(0);
}
static int audio_launch(void)
{
    if(!p.prepared || p.worker_started)return 0;
    set(&p.running,1);
    int32_t rc=sysThreadCreate(&p.thread,audio_worker,NULL,450,65536,THREAD_JOINABLE,"wili-bili-audio");
    rc_log("AUDIO_WORKER_CREATE",rc);if(rc){set(&p.running,0);return -1;}
    p.worker_started=1;p.log("AUDIO_CLOCK_STARTED rate=48000 block=256 preroll=4 resampled_from=44100");return 0;
}
void wili_audio_pause(int pause){if(p.prepared)set(&p.pause_request,pause?1:0);}
static int audio_prepare(void)
{
    p.log("AUDIO_PREPARE_BEGIN real_DASH_AAC");
    int32_t rc=sysModuleLoad(SYSMODULE_ADEC);rc_log("ADEC_BASE_MODULE_LOAD",rc);
    if(rc && (u32)rc!=SYSMODULE_ERR_DUPLICATE)return -1;
    rc=sysModuleLoad(WILI_MODULE_M4AAC);rc_log("AAC_MODULE_LOAD",rc);
    if(rc && (u32)rc!=SYSMODULE_ERR_DUPLICATE)return -1;
    __sync_synchronize();p.api=wili_resolve_adec(p.log);if(!p.api)return -1;
    wili_adec_type type={WILI_CODEC_M4AAC};wili_adec_attr attr={0};
    p.log("ADEC_QUERY_BEGIN");rc=p.api->query(&type,&attr);rc_log("ADEC_QUERY",rc);if(rc)return -1;
    if(!attr.memory || attr.memory>16*1024*1024)return -1;
    unsigned memory=(attr.memory+4095)&~4095u;
    {char line[96];snprintf(line,sizeof(line),"ADEC_WORK_MEMORY bytes=%u",memory);p.log(line);}
    p.adec_work=memalign(128,memory);p.scratch=memalign(128,SCRATCH+256);
    p.pcm=memalign(128,PCM_RING*2*sizeof(float));p.resampled=memalign(128,RESAMPLED_CAP*2*sizeof(float));
    p.apool=memalign(128,AUDIO_POOL*AUDIO_PACKET);
    if(!p.adec_work || !p.scratch || !p.pcm || !p.resampled || !p.apool){p.log("AUDIO_ALLOC_FAILED");return -1;}
    memset((uint8_t*)p.scratch+SCRATCH,0xa5,256);
    if(wili_resampler_init(&p.rs,p.audio_rate,OUT_RATE)){p.log("RESAMPLER_INIT_FAILED");return -1;}
    wili_adec_resource resource={memory,(u32)(uintptr_t)p.adec_work,450,250,16384};
    wili_adec_callback cb={__build_opd32(aac_callback,p.adec_opd),0};
    p.log("ADEC_OPEN_BEGIN");rc=p.api->open(&type,&resource,&cb,&p.adec);rc_log("ADEC_OPEN",rc);if(rc)return -1;
    p.adec_open=1;wili_aac_param param={1,{0,0},1};
    p.log("ADEC_START_BEGIN");rc=p.api->start(p.adec,&param);rc_log("ADEC_START",rc);if(rc)return -1;
    p.a_sequence=1;
    const wili_dash_info *ai=wili_mp4_dash_info(&sess.audio);
    {char line[160];snprintf(line,sizeof(line),"AAC_PRIMING_POLICY elst_start_pts=%llu trim_samples=0 (no blind 1024 trim)",
        (unsigned long long)ai->start_pts);p.log(line);}
    p.log("AAC_PREFILL_SCHEDULED nonblocking_main_loop");
    rc=ui_audio_runtime_acquire(p.log);if(rc){rc_log("AUDIO_RUNTIME_ACQUIRE",rc);return -1;}
    p.audio_init_done=1;p.prepared=1;return 0;
}

// --- video ---------------------------------------------------------------------
static int free_slot(void)
{
    unsigned i;for(i=0;i<REORDER_FRAMES;i++)if(!p.frame_used[i])return (int)i;return -1;
}
static uint64_t presentation_us(uint64_t pts);
static int pull_picture(void)
{
    if(!snap(&p.pending))return 0;
    int slot=free_slot();if(slot<0)return 0;
    u32 item_address=0;s32 rc=vdecGetPicItem(p.handle,&item_address);
    if((u32)rc==VDEC_ERROR_EMPTY)return 0;
    if(rc || !item_address){rc_log("VDEC_PIC_ITEM",rc);return -1;}
    const vdecPicture *item=(const void*)(uintptr_t)item_address;
    const vdecH264Info *info=(const void*)(uintptr_t)item->codec_specific_addr;
    uint32_t tag=(u32)item->userdata[0];
    int known=p.pts_tag[tag&63]==tag;
    int picture_warning=item->status==VDEC_ERROR_PIC;
    unsigned reported_w=info?info->width:0,reported_h=info?info->height:0;
    int height_ok=reported_h==p.coded_h || reported_h==p.display_h || reported_h==((p.coded_h+31)&~31u);
    wili_yuv_layout layout;
    int geometry_ok=reported_w==p.coded_w && height_ok && !wili_yuv_layout_init(&layout,reported_w,reported_h) && layout.bytes<=p.frame_stride;
    int skipped=item->attr==VDEC_PICTURE_SKIPPED;
    int bad=(item->status && !picture_warning) ||
            (!skipped && (!info || !geometry_ok || item->attr!=VDEC_PICTURE_NORMAL || !known));
    int preroll=known && presentation_us(p.pts_map[tag&63])<p.seek_base_us;
    if(bad || (!p.decoded && info)){
        char line[224];snprintf(line,sizeof(line),"VDEC_PICTURE_DETAIL status=%08x attr=%u reported=%ux%u userdata=%llu tag=%u known=%d decoded=%u",
            item->status,item->attr,info?info->width:0,info?info->height:0,
            (unsigned long long)item->userdata[0],tag,known,p.decoded);p.log(line);
    }
    if(picture_warning){
        p.picture_warnings++;
        if(p.picture_warnings<=4 || p.picture_warnings%120==0){char line[160];snprintf(line,sizeof(line),"VDEC_PICTURE_WARNING status=%08x tag=%u count=%u retrieval_required",item->status,tag,p.picture_warnings);p.log(line);}
    }
    vdecPictureFormat format={VDEC_PICFMT_YUV420P,p.color_matrix,0};
    if(!bad && !skipped && !preroll)memset(p.frames[slot]+p.frame_stride-128,0xa5,128);
    rc=vdecGetPicture(p.handle,&format,(bad || skipped || preroll)?NULL:p.frames[slot]);
    if(rc){rc_log("VDEC_GET_PICTURE",rc);return -1;}
    __sync_sub_and_fetch(&p.pending,1);
    if(bad){p.log("VDEC_PICTURE_REJECTED size_status_attr_or_unknown_userdata");return -1;}
    if(skipped || preroll){p.decoded++;return 1;}
    for(unsigned guard=0;guard<128;guard++)if(p.frames[slot][p.frame_stride-128+guard]!=0xa5){p.log("YUV_OUTPUT_GUARD_CHANGED");return -1;}
    if(wili_yuv_repack(p.frames[slot],p.frame_stride,&layout)){p.log("YUV_LAYOUT_REJECTED");return -1;}
    p.frame_layout[slot]=layout;
    flush_cache(p.frames[slot],layout.bytes);
    p.frame_pts[slot]=p.pts_map[tag&63];p.frame_used[slot]=1;p.decoded++;
    if(p.decoded==1)p.log("FIRST_NATIVE_PICTURE_RETRIEVED");
    if(p.decoded%30==0){
        char line[256];snprintf(line,sizeof(line),"VIDEO_PROGRESS fed=%u decoded=%u presented=%u audio_samples=%u v_buf=%llu a_buf=%llu picture_warn=%u callback_warn=%u",
            p.fed,p.decoded,p.presented,snap(&p.consumed),
            (unsigned long long)snap(&p.v_queued_bytes),
            (unsigned long long)snap(&p.a_queued_bytes),p.picture_warnings,snap(&p.picture_event_warnings));p.log(line);
    }
    return 1;
}
// Shutdown drain: release picture items without needing a free reorder slot.
// 1.0.1 hardware log hit VDEC_DRAIN_INCOMPLETE because pull_picture() returns
// 0 (without consuming the item) as soon as all 16 reorder slots are occupied.
static int discard_picture(void)
{
    if(!snap(&p.pending))return 0;
    u32 item_address=0;s32 rc=vdecGetPicItem(p.handle,&item_address);
    if((u32)rc==VDEC_ERROR_EMPTY)return 0;
    if(rc || !item_address)return -1;
    vdecPictureFormat format={VDEC_PICFMT_YUV420P,p.color_matrix,0};
    rc=vdecGetPicture(p.handle,&format,NULL);
    if(rc){rc_log("VDEC_DISCARD_PICTURE",rc);return -1;}
    __sync_sub_and_fetch(&p.pending,1);
    return 1;
}
static uint64_t presentation_us(uint64_t pts)
{
    uint64_t base=p.empty_edit_ms*1000ULL;
    if(pts<=p.start_pts)return base;
    return base+(pts-p.start_pts)*1000000ULL/(p.video_timescale?p.video_timescale:1);
}
static int present_due(void)
{
    int best=-1;uint64_t best_t=UINT64_MAX,i;
    for(i=0;i<REORDER_FRAMES;i++)if(p.frame_used[i]==1){
        __sync_synchronize();uint64_t t=presentation_us(p.frame_pts[i]);
        if(t<best_t){best_t=t;best=(int)i;}
    }
    if(best<0 || (!p.audio_started && p.has_pixels))return 0;
    if(p.audio_started){
        uint64_t audio_us=p.seek_base_us+(uint64_t)snap(&p.consumed)*1000000ULL/OUT_RATE;
        if(snap(&p.finished)){if(!p.audio_end_started)p.audio_end_started=time_us();audio_us+=time_us()-p.audio_end_started;}
        if(best_t>audio_us)return 0;
    }
    if(p.display_slot>=0)__sync_lock_test_and_set(&p.frame_used[p.display_slot],0);
    p.display_slot=best;p.display=(uint32_t*)p.frames[best];
    __sync_lock_test_and_set(&p.frame_used[best],2);p.presented++;p.has_pixels=1;
    if(p.presented==1){p.log("FIRST_REAL_VIDEO_FRAME_PRESENTED");if(p.hold_after_seek){p.paused=1;p.pause_started=time_us();wili_audio_pause(1);}}
    return 1;
}
static unsigned ready_frame_count(void)
{
    unsigned count=0;for(unsigned i=0;i<REORDER_FRAMES;i++)if(p.frame_used[i]==1)count++;return count;
}
static int frames_pending(void){return ready_frame_count()!=0;}
static void update_video_buffering(void)
{
    if(!p.audio_started || p.paused || p.complete || snap(&p.net_error))return;
    uint64_t audio=p.seek_base_us+(uint64_t)snap(&p.consumed)*1000000ULL/OUT_RATE;
    uint64_t latest=p.display_slot>=0?presentation_us(p.frame_pts[p.display_slot]):audio;
    for(unsigned i=0;i<REORDER_FRAMES;i++)if(p.frame_used[i]==1){__sync_synchronize();uint64_t pts=presentation_us(p.frame_pts[i]);if(pts>latest)latest=pts;}
    int64_t ahead=(int64_t)latest-(int64_t)audio;
    int was=(int)snap(&p.video_buffering);
    uint64_t queued=video_queued_ms();if(video_prefill_ready() && queued<2500)queued=2500;
    int next=p.software?wili_software_should_buffer(was,ahead,snap(&p.soft_eof),ready_frame_count()):wili_video_should_buffer(was,queued,ahead,snap(&p.v_eof));
    if(next!=was){
        uint64_t now=time_us();char line[192];
        if(next){p.buffer_started=now;p.video_rebuffers++;}
        snprintf(line,sizeof(line),"VIDEO_BUFFERING_%s queued_ms=%u ahead_ms=%lld count=%u waited_ms=%u",
            next?"BEGIN":"END",(unsigned)video_queued_ms(),(long long)(ahead/1000),p.video_rebuffers,
            next?0:(unsigned)((now-p.buffer_started)/1000));p.log(line);set(&p.video_buffering,next?1:0);
    }
}
static int software_frame(void *arg,const uint8_t *const planes[3],const int lines[3],unsigned width,unsigned height,int64_t pts)
{
    (void)arg;if(pts<0)return -1;
    if(presentation_us((uint64_t)pts)<p.seek_base_us)return 0;
    wili_yuv_layout layout;if(wili_yuv_layout_init(&layout,width,height) || layout.bytes>p.frame_stride)return -1;
    int slot=-1;
    while(snap(&p.soft_running) && slot<0){
        for(unsigned i=0;i<REORDER_FRAMES;i++)if(!p.frame_used[i] && __sync_bool_compare_and_swap(&p.frame_used[i],0,3)){slot=(int)i;break;}
        if(slot<0)sysUsleep(1000);
    }
    if(slot<0)return -1;
    for(unsigned plane=0;plane<3;plane++){
        unsigned rows=plane?height/2:height,columns=plane?width/2:width;
        for(unsigned row=0;row<rows;row++)memcpy(p.frames[slot]+layout.offset[plane]+row*layout.pitch[plane],planes[plane]+row*lines[plane],columns);
    }
    flush_cache(p.frames[slot],layout.bytes);p.frame_layout[slot]=layout;p.frame_pts[slot]=(uint64_t)pts;
    __sync_synchronize();__sync_lock_test_and_set(&p.frame_used[slot],1);
    unsigned decoded=__sync_add_and_fetch(&p.decoded,1);
    if(decoded==1)p.log("SOFTWARE_AVC_FIRST_FRAME_READY");
    if(decoded%30==0){char line[160];snprintf(line,sizeof(line),"SOFTWARE_AVC_PROGRESS decoded=%u cpu_ms=%llu",decoded,(unsigned long long)(wili_software_avc_cpu_us(p.soft_codec)/1000));p.log(line);}
    return 0;
}
static void software_worker(void *arg)
{
    (void)arg;
    while(snap(&p.soft_running)){
        size_t bytes=0;wili_dash_sample sample;int rc=queued_packet(0,p.vpool,VIDEO_PACKET,&bytes,&sample);
        if(rc==2){sysUsleep(1000);continue;}
        if(rc<0){set(&p.soft_error,1);break;}
        int decoded=rc?wili_software_avc_decode(p.soft_codec,p.vpool,bytes,(int64_t)sample.pts,(int64_t)sample.dts,software_frame,NULL):
                       wili_software_avc_decode(p.soft_codec,NULL,0,0,0,software_frame,NULL);
        if(decoded){if(snap(&p.soft_running)){rc_log("SOFTWARE_AVC_DECODE",decoded);set(&p.soft_error,1);}break;}
        if(!rc){set(&p.soft_eof,1);break;}
        __sync_add_and_fetch(&p.fed,1);
    }
    set(&p.soft_done,1);sysThreadExit(0);
}
static void software_start(const char *reason)
{
    char line[160];snprintf(line,sizeof(line),"SOFTWARE_AVC_FALLBACK reason=%s coded=%ux%u refs=%u",reason,p.coded_w,p.coded_h,p.refs);p.log(line);
    if(p.work_block.data){int rc=wili_media_memory_free(&p.work_block);rc_log("VDEC_UNUSED_WORK_FREE",rc);if(rc){p.failed=1;return;}p.work=NULL;}
    p.soft_codec=wili_software_avc_open(p.coded_w,p.coded_h,p.video_timescale);
    if(!p.soft_codec){p.log("SOFTWARE_AVC_INIT_FAILED");p.failed=1;return;}
    if(!p.mapped){int rc=gcmMapMainMemory(p.frame_arena,(u32)p.frame_arena_bytes,&p.rsx_offset);rc_log("SOFTWARE_VIDEO_RSX_MAP",rc);if(rc){p.failed=1;return;}p.mapped=1;}
    // avcodec applies SPS cropping before emitting its YUV frame.
    p.crop_x=p.crop_y=0;p.software=1;p.sequence=1;set(&p.soft_running,1);
    int rc=sysThreadCreate(&p.soft_thread,software_worker,NULL,900,512*1024,THREAD_JOINABLE,"bibi-software-avc");rc_log("SOFTWARE_AVC_WORKER_CREATE",rc);
    if(rc){set(&p.soft_running,0);p.failed=1;return;}p.soft_worker_started=1;p.log("SOFTWARE_AVC_SCHEDULING priority=900 prefill_ready_frames=6 late_tolerance_ms=80");
}
void wili_video_start(uint64_t now,void (*log)(const char*))
{
    if(p.log){log("PLAYER_START_REJECTED previous_session_not_stopped");return;}
    memset(&p,0,sizeof(p));p.display_slot=-1;p.log=log;p.frequency=sysGetTimebaseFrequency();p.started=now;
    if(!p.frequency || !sess.ready){p.log("BILI_PLAY_INIT_FAILED session_or_clock");p.failed=1;return;}
    const wili_dash_info *vi=wili_mp4_dash_info(&sess.video),*ai=wili_mp4_dash_info(&sess.audio);
    p.coded_w=vi->coded_width;p.coded_h=vi->coded_height;p.display_w=vi->display_width;p.display_h=vi->display_height;
    p.crop_x=vi->sps.crop_left*2;p.crop_y=vi->sps.crop_top*2;
    p.full_range=vi->sps.video_full_range;p.sar_w=vi->sar_width?vi->sar_width:1;p.sar_h=vi->sar_height?vi->sar_height:1;
    p.color_matrix=(vi->sps.matrix_coefficients==1 || ((vi->sps.matrix_coefficients==0 || vi->sps.matrix_coefficients==2) && (p.coded_w>=1280 || p.coded_h>=720)))?VDEC_COLOR_MATRIX_BT709:VDEC_COLOR_MATRIX_BT601;
    if(wili_mp4_dash_prepare_ps3(&sess.video,&p.level)){
        p.log("AVC_PS3_POLICY_REJECTED unsupported_SPS_or_DPB");p.failed=1;return;
    }
    {char line[160];snprintf(line,sizeof(line),"AVC_PS3_LEVEL source=%u decoder=%u coded=%ux%u refs=%u hrd=%u",vi->level,p.level,vi->coded_width,vi->coded_height,vi->refs,vi->sps.hrd_present);p.log(line);}
    p.refs=vi->refs;p.video_timescale=vi->timescale;p.audio_rate=ai->sample_rate;
    p.start_pts=vi->start_pts;p.empty_edit_ms=vi->empty_edit_ms;p.duration_ms=sess.duration_ms;
    p.video_bytes=(uint64_t)p.coded_w*((p.coded_h+31)&~31u)*3/2+256;
    p.seek_base_us=sess.seek_ms*1000ULL;
    uint64_t audio_ms=sess.seek_ms>ai->empty_edit_ms?sess.seek_ms-ai->empty_edit_ms:0;
    p.audio_target_pts=ai->start_pts+audio_ms*p.audio_rate/1000;
    p.total_out=(p.duration_ms>sess.seek_ms?p.duration_ms-sess.seek_ms:0)*48ULL;
    if(!p.coded_w || !p.coded_h || !p.level || !p.audio_rate || !p.total_out ||
       p.video_bytes+256>8*1024*1024 || !wili_video_geometry(p.coded_w,p.coded_h) || p.refs>9 || (!wili_video_is_720(p.coded_w,p.coded_h) && p.refs>4)){
        p.log("BILI_PLAY_UNSUPPORTED_MEDIA_CONFIG");p.failed=1;return;
    }
    {char line[280];snprintf(line,sizeof(line),"BILI_PLAY_CONFIG format=YUV420P coded=%ux%u display=%ux%u advertised_fps=%u level=%u profile=%u refs=%u timescale=%u start_pts=%llu empty_edit_ms=%llu audio_rate=%u duration_ms=%llu total_out=%llu reorder_queue=%u pcm_ring=%u prefill=%u v_slots=%u a_slots=%u",
        p.coded_w,p.coded_h,p.display_w,p.display_h,sess.advertised_fps,p.level,vi->sps.profile,p.refs,p.video_timescale,
        (unsigned long long)p.start_pts,(unsigned long long)p.empty_edit_ms,p.audio_rate,
        (unsigned long long)p.duration_ms,(unsigned long long)p.total_out,REORDER_FRAMES,PCM_RING,PREFILL,
        V_PREFETCH_SLOTS,A_PREFETCH_SLOTS);log(line);}
    p.vpool=memalign(128,VIDEO_POOL*VIDEO_PACKET);
    if(wili_yuv_pool_plan(p.coded_w,p.coded_h,REORDER_FRAMES,&p.frame_stride,&p.frame_arena_bytes)){p.log("YUV_POOL_PLAN_FAILED");p.failed=1;return;}
    {char line[192];snprintf(line,sizeof(line),"PLAYER_MEMORY fixed_main_bytes=%u frame_slots=%u frame_stride=%u decoder_work=queried_separately",
        (unsigned)(p.frame_arena_bytes+VIDEO_POOL*VIDEO_PACKET+V_COMPRESSED_BYTES+VIDEO_PACKET+
        (AUDIO_POOL+A_PREFETCH_SLOTS)*AUDIO_PACKET+PCM_RING*8+SCRATCH+256+RESAMPLED_CAP*8),REORDER_FRAMES,(unsigned)p.frame_stride);p.log(line);}
    int frame_rc=wili_media_memory_alloc(&p.frame_block,p.frame_arena_bytes,1);rc_log("VIDEO_FRAMES_SYSTEM_ALLOC",frame_rc);
    p.frame_arena=p.frame_block.data;
    unsigned i;for(i=0;i<REORDER_FRAMES;i++)p.frames[i]=p.frame_arena?(uint8_t*)p.frame_arena+i*p.frame_stride:NULL;
    if(!p.vpool || !p.frame_arena) {p.log("VIDEO_ALLOC_FAILED");p.failed=1;return;}
    for(i=0;i<REORDER_FRAMES;i++)if(!p.frames[i]){p.log("VIDEO_REORDER_ALLOC_FAILED");p.failed=1;return;}
    if(prefetch_init()){p.log("PREFETCH_INIT_FAILED");p.failed=1;return;}
    s32 rc=sysSpuInitialize(6,0);rc_log("SPU_INITIALIZE",rc);if(rc){p.failed=1;return;}
    if(audio_prepare()){p.log("AUDIO_PREPARE_FAILED");p.failed=1;return;}
    p.log("AAC_PREPARED_PREFILL_IN_MAIN_LOOP");
    rc=sysModuleLoad(SYSMODULE_VDEC_H264);rc_log("VDEC_H264_MODULE_LOAD",rc);
    if(rc && (u32)rc!=SYSMODULE_ERR_DUPLICATE){p.failed=1;return;}
    vdecType type={VDEC_CODEC_TYPE_H264,p.level};vdecAttr attr;memset(&attr,0,sizeof(attr));
    int extended=p.coded_h>1088;
    wili_vdec_avc_spec spec __attribute__((aligned(16)))={0};
    wili_vdec_type_ex type_ex __attribute__((aligned(16)))={VDEC_CODEC_TYPE_H264,p.level,0};
    if(extended){
        if(!wili_vdec_ex_available(p.log) || wili_vdec_ex_spec(&vi->sps,&spec)){
            software_start("extended_binding_or_spec");return;
        }
        type_ex.specific_info=(uint32_t)(uintptr_t)&spec;
        char line[144];snprintf(line,sizeof(line),"VDEC_EX_CONFIG coded=%ux%u refs=%u dpb=%u requested_buffers=%u",p.coded_w,p.coded_h,p.refs,vi->sps.max_dec_frame_buffering,spec.buffers);p.log(line);
        wili_vdec_attr extended_attr={0};rc=wiliVdecQueryAttrEx(&type_ex,&extended_attr);rc_log("VDEC_EX_QUERY",rc);
        if(rc){p.log("VDEC_EX_QUERY_REJECTED no_buffer_count_reduction");software_start("extended_query_rejected");return;}
        attr.mem_size=extended_attr.memory;attr.cmd_depth=extended_attr.depth;
    }else{
        p.log("VDEC_QUERY_BEGIN");rc=vdecQueryAttr(&type,&attr);rc_log("VDEC_QUERY",rc);if(rc){p.failed=1;return;}
    }
    {char line[128];snprintf(line,sizeof(line),"VDEC_REQUIREMENTS memory=%u queue=%u",attr.mem_size,attr.cmd_depth);p.log(line);}
    if(!attr.mem_size || attr.mem_size>64*1024*1024){p.log("VDEC_MEMORY_REQUIREMENT_REJECTED");p.failed=1;return;}
    memory_log("before_vdec_work");
    int work_rc=wili_media_memory_alloc(&p.work_block,attr.mem_size,0);rc_log("VDEC_WORK_SYSTEM_ALLOC",work_rc);
    p.work=p.work_block.data;
    if(work_rc){p.log("VDEC_ALLOC_FAILED");p.failed=1;return;}
    memory_log("before_vdec_open");
    p.log("VIDEO_RSX_MAP_BEGIN");rc=gcmMapMainMemory(p.frame_arena,(u32)p.frame_arena_bytes,&p.rsx_offset);rc_log("VIDEO_RSX_MAP",rc);
    if(rc){p.failed=1;return;}p.mapped=1;
    vdecConfig config={(u32)(uintptr_t)p.work,attr.mem_size,720,64*1024,200,4};
    vdecClosure closure={__build_opd32(vdec_callback,p.callback_opd),0};
    if(extended){
        wili_vdec_resource_ex resource={(uint32_t)(uintptr_t)p.work,attr.mem_size,720,64*1024,200,4,0};
        wili_vdec_callback callback={closure.fn,closure.arg};
        p.log("VDEC_EX_OPEN_BEGIN");rc=wiliVdecOpenEx(&type_ex,&resource,&callback,&p.handle);rc_log("VDEC_EX_OPEN",rc);
    }else{p.log("VDEC_OPEN_BEGIN");rc=vdecOpen(&type,&config,&closure,&p.handle);rc_log("VDEC_OPEN",rc);}
    if(rc){if(extended)software_start("extended_open_rejected");else p.failed=1;return;}
    p.opened=1;p.log("VDEC_START_BEGIN");rc=vdecStartSequence(p.handle);rc_log("VDEC_START",rc);if(rc){p.failed=1;return;}
    p.sequence=1;
    {char line[160];snprintf(line,sizeof(line),"YUV_OUTPUT_READY frame_pool_bytes=%u bt709=%u full_range=%u SAR=%u:%u",(unsigned)p.frame_arena_bytes,p.color_matrix==VDEC_COLOR_MATRIX_BT709,p.full_range,p.sar_w,p.sar_h);p.log(line);}
    p.log("BILI_PLAY_VDEC_AND_AUDIO_READY");
}
void wili_video_tick(uint64_t now)
{
    if(p.failed){wili_audio_pause(1);return;}
    if(audio_pump()<0 || audio_failed()){
        if(!p.audio_fatal_logged){p.log("BILI_PLAY_AUDIO_FATAL see_previous_line");p.audio_fatal_logged=1;}
        p.failed=1;wili_audio_pause(1);return;
    }
    if(!p.sequence || p.paused || p.complete)return;
    if(!p.clock_started){p.started=now;p.clock_started=1;}
    if(snap(&p.fatal) || snap(&p.rejected)){p.log("VDEC_CALLBACK_ERROR_OR_REJECTED_AU");p.failed=1;return;}
    if(p.software){
        if(snap(&p.soft_error)){p.log("SOFTWARE_AVC_FAILED");p.failed=1;wili_audio_pause(1);return;}
        __sync_synchronize();for(unsigned i=0;i<4;i++)if(!present_due())break;
        update_video_buffering();
        if(snap(&p.soft_eof) && !frames_pending()){p.complete=1;p.log("SOFTWARE_AVC_COMPLETE");}
        return;
    }
    unsigned k;
    for(k=0;k<4;k++)if(snap(&p.pending) && free_slot()>=0)if(pull_picture()<0){p.failed=1;return;}
    for(k=0;k<4;k++)if(!present_due())break;
    update_video_buffering();
    for(k=0;k<4 && !p.input_eof && p.fed-snap(&p.au_done)<VIDEO_POOL;k++){
        uint8_t *packet=p.vpool+(p.fed%VIDEO_POOL)*VIDEO_PACKET;
        if(!p.packet_ready){
            int rc=queued_packet(0,packet,VIDEO_PACKET,&p.packet_bytes,&p.next);
            if(rc==2)break;
            if(rc<0){p.log("MP4_VIDEO_PACKET_FAILED");p.failed=1;break;}
            if(!rc){p.input_eof=1;break;}
            p.packet_ready=1;
        }
        flush_cache(packet,p.packet_bytes);
        vdecAU au;memset(&au,0,sizeof(au));au.packet_addr=(u32)(uintptr_t)packet;au.packet_size=(u32)p.packet_bytes;
        uint64_t pts=p.next.pts/p.video_timescale*90000ULL+(p.next.pts%p.video_timescale)*90000ULL/p.video_timescale;
        au.pts.low=(u32)(pts>>32);au.pts.hi=(u32)pts;uint64_t dts=p.next.dts/p.video_timescale*90000ULL+(p.next.dts%p.video_timescale)*90000ULL/p.video_timescale;
        au.dts.low=(u32)(dts>>32);au.dts.hi=(u32)dts;au.userdata=p.fed;
        p.pts_map[p.fed&63]=p.next.pts;p.pts_tag[p.fed&63]=p.fed;
        s32 rc=vdecDecodeAu(p.handle,VDEC_DECODER_MODE_NORMAL,&au);
        if(!rc){p.fed++;p.packet_ready=0;}
        else if((u32)rc==VDEC_ERROR_BUSY)break;
        else {rc_log("VDEC_DECODE_AU",rc);p.failed=1;break;}
    }
    if(p.input_eof && !p.ending){
        s32 rc=vdecEndSequence(p.handle);
        if(!rc){p.ending=1;p.log("VDEC_END_SUBMITTED");}
        else if((u32)rc!=VDEC_ERROR_BUSY){rc_log("VDEC_END",rc);p.failed=1;}
    }
    if(p.ending && snap(&p.seq_done) && !snap(&p.pending) && !frames_pending()){
        p.complete=1;
        char line[192];snprintf(line,sizeof(line),"VIDEO_DECODE_COMPLETE fed=%u decoded=%u presented=%u audio_consumed=%u duration_ms=%llu",
            p.fed,p.decoded,p.presented,snap(&p.consumed),(unsigned long long)p.duration_ms);p.log(line);
    }
}
void wili_video_pause(uint64_t now)
{
    if(!p.sequence || p.failed || p.complete)return;
    if(p.paused){p.started+=now-p.pause_started;p.paused=0;wili_audio_pause(0);p.log("AV_RESUMED");}
    else {p.pause_started=now;p.paused=1;wili_audio_pause(1);p.log("AV_PAUSED");}
}
static int stop_internal(int keep_session)
{
    if(!p.log)return 0;
    p.log("BILI_PLAY_STOP_BEGIN");set(&p.soft_running,0);
    if(p.soft_worker_started){
        uint64_t limit=time_us()+3000000;while(!snap(&p.soft_done) && time_us()<limit)sysUsleep(1000);
        if(!snap(&p.soft_done)){p.log("SOFTWARE_AVC_STOP_TIMEOUT retain_live_buffers");return -1;}
        uint64_t result;sysThreadJoin(p.soft_thread,&result);p.soft_worker_started=0;
    }
    wili_software_avc_close(p.soft_codec);p.soft_codec=NULL;
    if(p.net_worker_started){
        set(&p.net_running,0);uint64_t begin=time_us();
        unsigned done=0;
        do{done=0;for(unsigned i=0;i<2;i++)if(!(p.net_worker_started&(1<<i)) || snap(&p.net_done[i]))done++;
            if(done<2)sysUsleep(1000);
        }while(done<2 && time_us()-begin<7000000);
        if(done<2){p.log("NET_WORKER_STOP_TIMEOUT retain_buffers_for_kernel_teardown");return -1;}
        for(unsigned i=0;i<2;i++)if(p.net_worker_started&(1<<i)){uint64_t result=0;rc_log("NET_WORKER_JOIN",sysThreadJoin(p.net_thread[i],&result));}
        p.net_worker_started=0;
    }
    if(p.worker_started){
        set(&p.running,0);uint64_t begin=time_us();
        while(!snap(&p.thread_done) && time_us()-begin<1000000)sysUsleep(1000);
        if(!snap(&p.thread_done)){p.log("AUDIO_WORKER_STOP_TIMEOUT retain_buffers_for_kernel_teardown");return -1;}
        uint64_t result=0;rc_log("AUDIO_WORKER_JOIN",sysThreadJoin(p.thread,&result));p.worker_started=0;
    }
    if(close_adec()){p.log("ADEC_CLOSE_FAILED retain_buffers_for_kernel_teardown");return -1;}
    if(p.port_open && close_port()){p.log("AUDIO_PORT_CLOSE_FAILED");return -1;}
    if(p.audio_init_done){int32_t rc=ui_audio_runtime_release(p.log);rc_log("AUDIO_RUNTIME_RELEASE",rc);if(rc)return -1;p.audio_init_done=0;}
    if(p.opened){
        int drained=1;
        if(p.sequence && (!snap(&p.seq_done) || snap(&p.pending))){
            unsigned waited;
            for(waited=0;waited<2000;waited++){
                if(snap(&p.pending) && discard_picture()<0)break;
                if(!p.ending){s32 rc=vdecEndSequence(p.handle);if(!rc)p.ending=1;else if((u32)rc!=VDEC_ERROR_BUSY)break;}
                if(snap(&p.seq_done) && !snap(&p.pending))break;
                sysUsleep(1000);
            }
            if(!snap(&p.seq_done) || snap(&p.pending)){
                p.log("VDEC_DRAIN_INCOMPLETE retain_decoder_buffers_for_kernel_teardown");drained=0;
            }
        }
        if(!drained)return -1;
        if(drained){
            p.log("VDEC_CLOSE_BEGIN");s32 rc=vdecClose(p.handle);rc_log("VDEC_CLOSE",rc);
            if(rc){p.log("VDEC_CLOSE_FAILED retain_buffers_for_kernel_teardown");return -1;}
            else p.opened=0;
        }
    }
    if(p.mapped){s32 rc=gcmUnmapIoAddress(p.rsx_offset);rc_log("VIDEO_RSX_UNMAP",rc);if(rc)return -1;p.mapped=0;}
    int mem_rc=wili_media_memory_free(&p.work_block);rc_log("VDEC_WORK_SYSTEM_FREE",mem_rc);if(mem_rc)return -1;
    mem_rc=wili_media_memory_free(&p.frame_block);rc_log("VIDEO_FRAMES_SYSTEM_FREE",mem_rc);if(mem_rc)return -1;
    mem_rc=wili_media_memory_free(&p.compressed_block);rc_log("VIDEO_QUEUE_SYSTEM_FREE",mem_rc);if(mem_rc)return -1;
    free(p.vpool);free(p.pcm);
    unsigned j;
    p.compressed.data=NULL;free(p.net_scratch);
    for(j=0;j<A_PREFETCH_SLOTS;j++){free(p.a_slots[j].data);p.a_slots[j].data=NULL;}
    p.work=NULL;p.vpool=NULL;p.pcm=NULL;p.display=NULL;
    if(!keep_session)wili_bili_session_close(&sess,p.log);
    memory_log("after_release");
    p.log("BILI_PLAY_RESOURCES_RELEASED");
    memset(&p,0,sizeof(p));return 0;
}
void wili_video_cancel_workers(void){set(&p.soft_running,0);set(&p.net_running,0);set(&p.running,0);}
int wili_video_stop_safe(void){return stop_internal(0);}
void wili_video_stop(void){(void)wili_video_stop_safe();}
int wili_video_seek(uint64_t target_ms,uint64_t now)
{
    if(!p.log || !sess.ready || !p.duration_ms)return -1;
    void (*log)(const char*)=p.log;int was_paused=p.paused;
    if(target_ms>=p.duration_ms)target_ms=p.duration_ms>1000?p.duration_ms-1000:0;
    if(stop_internal(1))return -2;
    uint64_t vstart=0,astart=0;
    if(wili_mp4_dash_seek(&sess.video,target_ms,&vstart) || wili_mp4_dash_seek(&sess.audio,target_ms,&astart)){log("SEEK_INDEX_FAILED");return -1;}
    sess.seek_ms=target_ms;
    char line[160];snprintf(line,sizeof(line),"SEEK_BEGIN target_ms=%llu video_fragment_ms=%llu audio_fragment_ms=%llu",(unsigned long long)target_ms,(unsigned long long)vstart,(unsigned long long)astart);log(line);
    wili_video_start(now,log);p.hold_after_seek=was_paused;
    return p.failed?-1:0;
}
const uint32_t *wili_video_pixels(unsigned *width,unsigned *height)
{
    *width=*height=0;return NULL; // Native playback exposes planar data through wili_video_surface only.
}
void wili_video_status(char *out,unsigned cap)
{
    unsigned seconds=(unsigned)(snap(&p.consumed)/OUT_RATE);
    snprintf(out,cap,"%s  %u/%us  frames %u",p.failed?"ERROR: SEE LOG":p.complete?"COMPLETE":p.paused?"PAUSED":"PLAYING",
             seconds,(unsigned)(p.duration_ms/1000),p.decoded);
}
void wili_audio_status(char *out,unsigned cap)
{
    uint32_t samples=snap(&p.consumed);
    snprintf(out,cap,"Audio: %u.%02us / %us%s",samples/OUT_RATE,(samples%OUT_RATE)*100/OUT_RATE,
             (unsigned)(p.duration_ms/1000),snap(&p.finished)?" DONE":snap(&p.a_buffering)?" BUFFER":"");
}
unsigned wili_audio_state(void)
{
    if(snap(&p.error))return 4;
    if(snap(&p.finished)){
        if(!p.a_reported_finished){p.log("AUDIO_PLAYBACK_COMPLETE");p.a_reported_finished=1;}return 3;
    }
    if(snap(&p.pause_request))return 2;
    if(snap(&p.video_buffering))return 5;
    if(p.prepared && !p.audio_started)return 5;
    return snap(&p.a_buffering)?5:p.worker_started?1:0;
}
unsigned wili_video_generation(void){return p.presented;}
unsigned wili_video_buffer_ms(void){return (unsigned)video_queued_ms();}
void wili_video_log_clock(void)
{
    if(!p.log || p.display_slot<0)return;
    uint64_t audio=p.seek_base_us+(uint64_t)snap(&p.consumed)*1000000ULL/OUT_RATE;
    uint64_t video=presentation_us(p.frame_pts[p.display_slot]);char line[192];
    snprintf(line,sizeof(line),"VIDEO_CLOCK audio_ms=%llu video_ms=%llu skew_ms=%lld decoded=%u selected=%u rebuffers=%u v_buffer_ms=%u v_rebuffers=%u",
        (unsigned long long)(audio/1000),(unsigned long long)(video/1000),
        (long long)((int64_t)video-(int64_t)audio)/1000,p.decoded,p.presented,snap(&p.a_rebuffers),(unsigned)video_queued_ms(),p.video_rebuffers);p.log(line);
}
uint32_t wili_video_rsx_offset(void){return p.rsx_offset+(p.display_slot>=0?(u32)(p.display_slot*p.frame_stride):0);}
int wili_video_presented(void)
{
    if(!p.failed && !p.paused && p.has_pixels && !p.audio_started &&
       (p.a_produced>=PREFILL || (p.a_decode_complete && p.a_produced)) &&
       video_prefill_ready() && (!p.software || wili_software_prefill_ready(ready_frame_count(),snap(&p.soft_eof)))){
        if(audio_launch()){p.failed=1;return -1;}
        p.audio_started=1;
    }
    return 0;
}
unsigned wili_video_state(void){return (p.failed?4u:p.complete?3u:p.paused?2u:p.sequence?1u:0u)|(wili_audio_state()<<4);}

uint64_t wili_video_playback_time_us(void)
{
    return p.seek_base_us+(uint64_t)snap(&p.consumed) * 1000000ULL / OUT_RATE;
}
uint64_t wili_video_duration_ms(void)
{
    return p.duration_ms;
}
void wili_video_get_buffer_status(size_t *v_buf, size_t *a_buf)
{
    if(v_buf)*v_buf=snap(&p.v_queued_bytes);
    if(a_buf)*a_buf=snap(&p.a_queued_bytes);
}
int wili_video_is_paused(void)
{
    return p.paused;
}

unsigned wili_video_pitch(void){return p.coded_w;}

int wili_video_surface(wili_yuv_surface *out)
{
    if(!out || !p.has_pixels || p.display_slot<0)return 0;
    memset(out,0,sizeof(*out));out->layout=p.frame_layout[p.display_slot];
    out->rsx_base=p.rsx_offset+(u32)(p.display_slot*p.frame_stride);
    out->visible_width=p.display_w;out->visible_height=p.display_h;out->crop_x=p.crop_x;
    out->crop_y=out->layout.height==p.display_h?0:p.crop_y;
    out->aspect_num=(uint64_t)p.display_w*p.sar_w;out->aspect_den=(uint64_t)p.display_h*p.sar_h;
    out->bt709=p.color_matrix==VDEC_COLOR_MATRIX_BT709;out->full_range=p.full_range;
    return 1;
}
