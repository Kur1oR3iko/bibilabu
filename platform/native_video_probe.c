// SPDX-License-Identifier: GPL-3.0-or-later
// Fragmented-MP4 video streaming through the native decoder and bounded AU pool.
#include "native_video_probe.h"
#include "native_audio_probe.h"
#include "avc_test_stream.h"
#include "test_clip_identity.h"
#include "mp4_file.h"
#include "probe_identity.h"
#define VIDEO_POOL 8u
#define VIDEO_PACKET (256u*1024u)
#include <codec/vdec.h>
#include <sysmodule/sysmodule.h>
#include <sys/spu.h>
#include <sys/file.h>
#include <sys/systime.h>
#include <ppu-asm.h>
#include <rsx/rsx.h>
#include <malloc.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(vdecType)==8,"vdecType ABI");
_Static_assert(sizeof(vdecAttr)==16,"vdecAttr ABI");
_Static_assert(sizeof(vdecConfig)==24,"vdecConfig ABI");
_Static_assert(sizeof(vdecClosure)==8,"vdecClosure ABI");
_Static_assert(sizeof(vdecAU)==40 && offsetof(vdecAU,userdata)==24,"vdecAU ABI");
_Static_assert(sizeof(vdecPicture)==80 && offsetof(vdecPicture,codec_specific_addr)==72,"vdecPicture ABI");
_Static_assert(sizeof(vdecPictureFormat)==12,"vdecPictureFormat ABI");
_Static_assert(WILI_CLIP_WIDTH*WILI_CLIP_HEIGHT*4+256<=2*1024*1024,"Mapped output capacity");

static struct {
    void (*log)(const char *);
    uint8_t *file;
    void *work;
    uint32_t *pixels;
    wili_mp4_file source;
    wili_mp4_sample next;size_t packet_bytes;
    int packet_ready,input_eof;
    u32 handle,callback_opd[2],rsx_offset;
    int mapped,clock_started,audio_started;
    volatile u32 au_done,pending,seq_done,fatal,rejected;
    unsigned fed,decoded,has_pixels;
    int opened,sequence,ending,paused,failed,complete;
    uint64_t started,pause_started;
} v;

static void rc_log(const char *name,s32 rc)
{
    char line[128];snprintf(line,sizeof(line),"%s rc=0x%08x",name,(u32)rc);v.log(line);
}
static u32 snapshot(volatile u32 *value){return __sync_add_and_fetch(value,0);}
static u32 decoder_callback(u32 handle,u32 type,u32 data,u32 arg)
{
    (void)handle;(void)arg;
    if(type==VDEC_CALLBACK_AUDONE){
        if(data)__sync_add_and_fetch(&v.rejected,1);
        __sync_add_and_fetch(&v.au_done,1);
    }else if(type==VDEC_CALLBACK_PICOUT)__sync_add_and_fetch(&v.pending,1);
    else if(type==VDEC_CALLBACK_SEQDONE)__sync_lock_test_and_set(&v.seq_done,1);
    else if(type==VDEC_CALLBACK_ERROR)__sync_lock_test_and_set(&v.fatal,data ? data : 1);
    return 0;
}
static int load_clip(void)
{
    if(wili_mp4_file_open(&v.source,WILI_CLIP_FILENAME,WILI_CLIP_BYTES,WILI_CLIP_CRC32,0,v.log))return -1;
    wili_fmp4 *r=&v.source.reader;
    if(r->width!=WILI_CLIP_WIDTH || r->height!=WILI_CLIP_HEIGHT || r->level!=WILI_CLIP_LEVEL || r->refs>1){v.log("MP4_VIDEO_CONFIG_REJECTED");return -1;}
    v.file=memalign(128,VIDEO_POOL*VIDEO_PACKET);
    if(!v.file){v.log("VIDEO_PACKET_POOL_ALLOC_FAILED");return -1;}
    v.log("VIDEO_STREAM_READY coded=848x480 fps=30 AU_pool=8 packet_cap=262144 whole_file_allocation=0");return 0;
}

void wili_video_start(uint64_t now,void (*log)(const char *))
{
    memset(&v,0,sizeof(v));v.log=log;v.started=now;
    v.log(WILI_PROBE_ID " INIT_BEGIN fragmented_MP4_AV");
    if(load_clip())goto fail;
    s32 rc=sysSpuInitialize(6,0);rc_log("SPU_INITIALIZE",rc);if(rc)goto fail;
    if(wili_audio_prepare(log)){v.log("AUDIO_PREPARE_FAILED");goto fail;}
    v.log("AAC_STREAM_PREFILL_READY_BEFORE_VDEC_OPEN");
    v.log("VDEC_H264_MODULE_LOAD_BEGIN");
    rc=sysModuleLoad(SYSMODULE_VDEC_H264);rc_log("VDEC_H264_MODULE_LOAD",rc);
    if(rc && (u32)rc!=SYSMODULE_ERR_DUPLICATE)goto fail;
    vdecType type={VDEC_CODEC_TYPE_H264,WILI_CLIP_LEVEL};vdecAttr attr;
    memset(&attr,0,sizeof(attr));v.log("VDEC_QUERY_BEGIN");
    rc=vdecQueryAttr(&type,&attr);rc_log("VDEC_QUERY",rc);if(rc)goto fail;
    char line[128];snprintf(line,sizeof(line),"VDEC_REQUIREMENTS memory=%u queue=%u",attr.mem_size,attr.cmd_depth);v.log(line);
    if(!attr.mem_size || attr.mem_size>64*1024*1024){v.log("VDEC_MEMORY_REQUIREMENT_REJECTED");goto fail;}
    v.work=memalign(128,attr.mem_size);
    v.pixels=memalign(1024*1024,2*1024*1024);
    if(!v.work || !v.pixels){v.log("VDEC_ALLOC_FAILED");goto fail;}
    memset((uint8_t*)v.pixels+WILI_CLIP_WIDTH*WILI_CLIP_HEIGHT*4,0xa5,256);
    v.log("VIDEO_RSX_MAP_BEGIN");
    rc=gcmMapMainMemory(v.pixels,2*1024*1024,&v.rsx_offset);rc_log("VIDEO_RSX_MAP",rc);
    if(rc)goto fail;
    v.mapped=1;
    vdecConfig config={(u32)(uintptr_t)v.work,attr.mem_size,720,64*1024,200,4};
    vdecClosure closure={__build_opd32(decoder_callback,v.callback_opd),0};
    v.log("VDEC_OPEN_BEGIN");
    rc=vdecOpen(&type,&config,&closure,&v.handle);rc_log("VDEC_OPEN",rc);if(rc)goto fail;
    v.opened=1;v.log("VDEC_START_BEGIN");
    rc=vdecStartSequence(v.handle);rc_log("VDEC_START",rc);if(rc)goto fail;
    v.sequence=1;
    v.log("VDEC_AND_AUDIO_READY");return;
fail:
    v.failed=1;v.log("VIDEO_INIT_FAILED see_last_stage");
}
static int pull_picture(int discard)
{
    if(!snapshot(&v.pending))return 0;
    u32 item_address=0;s32 rc=vdecGetPicItem(v.handle,&item_address);
    if((u32)rc==VDEC_ERROR_EMPTY)return 0;
    if(rc || !item_address){rc_log("VDEC_PIC_ITEM",rc);return -1;}
    const vdecPicture *item=(const void*)(uintptr_t)item_address;
    const vdecH264Info *info=(const void*)(uintptr_t)item->codec_specific_addr;
    int bad=item->status || !info || info->width!=WILI_CLIP_WIDTH || info->height!=WILI_CLIP_HEIGHT || item->attr!=VDEC_PICTURE_NORMAL ||
            (!discard && item->userdata[0]!=v.decoded);
    if(!discard && (bad || !v.decoded)){
        char detail[224];
        snprintf(detail,sizeof(detail),"VDEC_PICTURE_DETAIL status=%08x attr=%u coded=%ux%u codec=%u userdata=%llu expected=%u au_count=%u bytes=%u",item->status,item->attr,info ? info->width : 0,info ? info->height : 0,item->codec_type,(unsigned long long)item->userdata[0],v.decoded,item->access_units,item->picture_size);v.log(detail);
    }
    vdecPictureFormat format={VDEC_PICFMT_ARGB32,VDEC_COLOR_MATRIX_BT601,255};
    rc=vdecGetPicture(v.handle,&format,(discard || bad) ? 0 : v.pixels);
    if(rc){rc_log("VDEC_GET_PICTURE",rc);return -1;}
    __sync_sub_and_fetch(&v.pending,1);
    if(bad && !discard){v.log("VDEC_PICTURE_REJECTED size_status_or_frame_order");return -1;}
    if(!discard){
        unsigned i;for(i=0;i<256;i++)if(((uint8_t*)v.pixels)[WILI_CLIP_WIDTH*WILI_CLIP_HEIGHT*4+i]!=0xa5){v.log("VDEC_OUTPUT_GUARD_CHANGED");return -1;}
        // Make any PPU-written conversion output visible before RSX DMA reads
        // mapped main memory. The caller fences the blit before reusing it.
        for(i=0;i<WILI_CLIP_WIDTH*WILI_CLIP_HEIGHT*4;i+=128)
            __asm__ volatile("dcbst 0,%0"::"r"((uint8_t*)v.pixels+i):"memory");
        __sync_synchronize();
        v.has_pixels=1;v.decoded++;
        if(v.decoded==1)v.log("FIRST_NATIVE_PICTURE_RETRIEVED");
        if(v.decoded%30==0){char line[128];snprintf(line,sizeof(line),"VIDEO_PROGRESS fed=%u au_done=%u decoded=%u pending=%u audio_samples=%u",v.fed,snapshot(&v.au_done),v.decoded,snapshot(&v.pending),wili_audio_clock_samples());v.log(line);}
    }
    return 1;
}
void wili_video_tick(uint64_t now)
{
    if(v.failed){wili_audio_pause(1);return;}
    if(wili_audio_pump()<0 || wili_audio_failed()){v.failed=1;wili_audio_pause(1);return;}
    if(v.failed || !v.sequence || v.paused || v.complete)return;
    if(!v.clock_started){v.started=now;v.clock_started=1;}
    if(snapshot(&v.fatal) || snapshot(&v.rejected)){v.log("VDEC_CALLBACK_ERROR_OR_REJECTED_AU");v.failed=1;return;}
    if(now-v.started>=(uint64_t)(WILI_CLIP_DURATION_SECONDS+30)*1000000ULL){v.log("VIDEO_PROGRESS_TIMEOUT");v.failed=1;return;}
    // Pull pictures while submitting input; waiting only for AUDONE can deadlock
    // once the decoder's decoded-picture buffers are full.
    if(!v.decoded && snapshot(&v.pending))v.started=now;
    if((uint64_t)wili_audio_clock_samples()*WILI_CLIP_FPS >= (uint64_t)v.decoded*48000){
        if(pull_picture(0)<0){v.failed=1;return;}
    }
    unsigned burst;
    for(burst=0;burst<4 && !v.input_eof && v.fed-snapshot(&v.au_done)<VIDEO_POOL;burst++){
        uint8_t *packet=v.file+(v.fed%VIDEO_POOL)*VIDEO_PACKET;
        if(!v.packet_ready){
            int result=wili_fmp4_packet(&v.source.reader,packet,VIDEO_PACKET,&v.packet_bytes,&v.next);
            if(result<0){v.log("MP4_VIDEO_PACKET_FAILED");v.failed=1;break;}
            if(!result){v.input_eof=1;break;}
            if(v.next.pts!=v.next.dts || v.source.reader.timescale%WILI_CLIP_FPS || v.next.dts!=(uint64_t)v.fed*v.source.reader.timescale/WILI_CLIP_FPS ||
               (uint64_t)v.next.duration*WILI_CLIP_FPS!=v.source.reader.timescale){v.log("MP4_VIDEO_TIMING_REJECTED");v.failed=1;break;}
            if(v.fed<2){char line[144];snprintf(line,sizeof(line),"VDEC_INPUT_AU index=%u bytes=%u pts=%llu first_nal=%u",v.fed,(unsigned)v.packet_bytes,(unsigned long long)v.next.pts,packet[4]&31);v.log(line);}
            v.packet_ready=1;
        }
        unsigned cache_line;
        for(cache_line=0;cache_line<v.packet_bytes;cache_line+=128)
            __asm__ volatile("dcbst 0,%0"::"r"(packet+cache_line):"memory");
        __sync_synchronize();
        vdecAU au;memset(&au,0,sizeof(au));au.packet_addr=(u32)(uintptr_t)packet;au.packet_size=(u32)v.packet_bytes;
        uint64_t pts=v.next.pts/v.source.reader.timescale*90000+(v.next.pts%v.source.reader.timescale)*90000/v.source.reader.timescale;
        au.pts.low=(u32)(pts>>32);au.pts.hi=(u32)pts;
        au.dts.low=au.dts.hi=VDEC_TS_INVALID;au.userdata=v.fed;
        s32 rc=vdecDecodeAu(v.handle,VDEC_DECODER_MODE_NORMAL,&au);
        if(!rc){v.fed++;v.packet_ready=0;}
        else if((u32)rc==VDEC_ERROR_BUSY)break;
        else {rc_log("VDEC_DECODE_AU",rc);v.failed=1;break;}
    }
    if(v.input_eof && !v.ending){
        s32 rc=vdecEndSequence(v.handle);
        if(!rc){v.ending=1;v.log("VDEC_END_SUBMITTED");}
        else if((u32)rc!=VDEC_ERROR_BUSY){rc_log("VDEC_END",rc);v.failed=1;}
    }
    if(v.ending && snapshot(&v.seq_done) && !snapshot(&v.pending)){
        v.complete=1;
        if(v.decoded==WILI_CLIP_FRAMES && snapshot(&v.au_done)==WILI_CLIP_FRAMES){
            uint64_t elapsed=now-v.started;
            char line[180];
            snprintf(line,sizeof(line),"VIDEO_DECODE_COMPLETE expected=%u actual=%u elapsed_us=%llu delivery_fps_milli=%llu",WILI_CLIP_FRAMES,v.decoded,(unsigned long long)elapsed,(unsigned long long)(elapsed ? (uint64_t)(v.decoded-1)*1000000000ULL/elapsed : 0));v.log(line);
        }
        else {v.failed=1;v.log("VIDEO_FRAME_COUNT_MISMATCH");}
    }
}
void wili_video_pause(uint64_t now)
{
    if(!v.sequence || v.failed || v.complete)return;
    if(v.paused){v.started+=now-v.pause_started;v.paused=0;wili_audio_pause(0);v.log("AV_RESUMED");}
    else {v.pause_started=now;v.paused=1;wili_audio_pause(1);v.log("AV_PAUSED");}
}
void wili_video_stop(void)
{
    if(!v.log)return;
    v.log("AUDIO_STOP_BEGIN");wili_audio_stop();v.log("AUDIO_STOP_RETURNED");
    if(v.opened){
        if(v.sequence && (!snapshot(&v.seq_done) || snapshot(&v.pending))){
            unsigned waited;
            for(waited=0;waited<2000;waited++){
                if(snapshot(&v.pending))if(pull_picture(1)<0)break;
                if(!v.ending){s32 rc=vdecEndSequence(v.handle);if(!rc)v.ending=1;else if((u32)rc!=VDEC_ERROR_BUSY)break;}
                if(snapshot(&v.seq_done) && !snapshot(&v.pending))break;
                sysUsleep(1000);
            }
            if(!snapshot(&v.seq_done) || snapshot(&v.pending)){
                v.log("VDEC_DRAIN_INCOMPLETE retain_buffers_for_kernel_teardown");return;
            }
        }
        v.log("VDEC_CLOSE_BEGIN");s32 rc=vdecClose(v.handle);rc_log("VDEC_CLOSE",rc);
        if(rc){v.log("VDEC_CLOSE_FAILED retain_buffers_for_kernel_teardown");return;}
        v.opened=0;
    }
    free(v.work);
    if(v.mapped)v.log("RSX_MAPPED_PIXELS_RETAINED until_process_teardown");
    else free(v.pixels);
    wili_mp4_file_close(&v.source);
    free(v.file);v.work=0;v.pixels=0;v.file=0;
    v.log("VIDEO_DECODER_RESOURCES_RELEASED");
}
const uint32_t *wili_video_pixels(unsigned *width,unsigned *height)
{
    *width=WILI_CLIP_WIDTH;*height=WILI_CLIP_HEIGHT;return v.has_pixels ? v.pixels : 0;
}
void wili_video_status(char *out,unsigned capacity)
{
    snprintf(out,capacity,"%s  frames %u/900",v.failed ? "ERROR: SEE LOG" : v.complete ? "COMPLETE" : v.paused ? "PAUSED" : "PLAYING",v.decoded);
}

unsigned wili_video_generation(void){return v.decoded;}
uint32_t wili_video_rsx_offset(void){return v.rsx_offset;}
int wili_video_presented(void)
{
    // Anchor audio launch after submitting the first actual video picture,
    // rather than while startup drawing and filesystem logging are pending.
    if(!v.failed && !v.paused && v.has_pixels && !v.audio_started){
        if(wili_audio_launch()){v.failed=1;return -1;}
        v.audio_started=1;
    }
    return 0;
}

unsigned wili_video_state(void){return (v.failed ? 4u : v.complete ? 3u : v.paused ? 2u : v.sequence ? 1u : 0u) | (wili_audio_state()<<4);}
