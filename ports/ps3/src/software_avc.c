// SPDX-License-Identifier: GPL-3.0-or-later
#include "software_avc.h"
#include "video_geometry.h"
#include <libavcodec/avcodec.h>
#include <libavutil/cpu.h>
#include <libavutil/time.h>
#include <libavutil/error.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
struct wili_software_avc {AVCodecContext *codec;AVPacket *packet;AVFrame *frame;unsigned width,height;uint64_t cpu_us;};
wili_software_avc *wili_software_avc_open(unsigned width,unsigned height,unsigned timescale)
{
    if(!wili_video_is_720(width,height) || !timescale || timescale>INT_MAX)return NULL;
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    av_force_cpu_flags(AV_CPU_FLAG_ALTIVEC);
#endif
    const AVCodec *codec=avcodec_find_decoder(AV_CODEC_ID_H264);if(!codec)return NULL;
    wili_software_avc *s=calloc(1,sizeof(*s));if(!s)return NULL;
    s->width=width;s->height=height;s->codec=avcodec_alloc_context3(codec);s->packet=av_packet_alloc();s->frame=av_frame_alloc();
    if(!s->codec || !s->packet || !s->frame)goto fail;
    s->codec->thread_count=1;s->codec->thread_type=0;s->codec->pkt_timebase=(AVRational){1,(int)timescale};
    // FFmpeg validates its internally aligned buffer width against max_pixels.
    s->codec->max_pixels=(int64_t)((width+127u)&~127u)*((height+127u)&~127u);s->codec->apply_cropping=1;
    if(avcodec_open2(s->codec,codec,NULL)<0)goto fail;
    return s;
fail:wili_software_avc_close(s);return NULL;
}
int wili_software_avc_decode(wili_software_avc *s,const uint8_t *data,size_t bytes,int64_t pts,int64_t dts,wili_software_frame emit,void *opaque)
{
    if(!s || !emit || bytes>512u*1024u || (data && !bytes))return -1;
    if(data){
        av_packet_unref(s->packet);if(av_new_packet(s->packet,(int)bytes)<0)return -1;
        memcpy(s->packet->data,data,bytes);s->packet->pts=pts;s->packet->dts=dts;
    }
    int64_t begin=av_gettime_relative();int rc=avcodec_send_packet(s->codec,data?s->packet:NULL);s->cpu_us+=(uint64_t)(av_gettime_relative()-begin);
    if(rc<0 && rc!=AVERROR_EOF)return rc;
    for(;;){
        begin=av_gettime_relative();rc=avcodec_receive_frame(s->codec,s->frame);s->cpu_us+=(uint64_t)(av_gettime_relative()-begin);
        if(rc==AVERROR(EAGAIN) || rc==AVERROR_EOF)return 0;
        if(rc<0)return rc;
        AVFrame *f=s->frame;
        if(f->format!=AV_PIX_FMT_YUV420P && f->format!=AV_PIX_FMT_YUVJ420P){av_frame_unref(f);return -1;}
        if(f->width<=0 || f->height<=0 || (unsigned)f->width>s->width || (unsigned)f->height>s->height || (f->width&1) || (f->height&1) || f->pts==AV_NOPTS_VALUE){av_frame_unref(f);return -1;}
        const uint8_t *planes[3]={f->data[0],f->data[1],f->data[2]};
        for(unsigned i=0;i<3;i++)if(!planes[i] || f->linesize[i]<(i?f->width/2:f->width)){av_frame_unref(f);return -1;}
        rc=emit(opaque,planes,f->linesize,(unsigned)f->width,(unsigned)f->height,f->pts);av_frame_unref(f);
        if(rc)return rc;
    }
}
uint64_t wili_software_avc_cpu_us(const wili_software_avc *s){return s?s->cpu_us:0;}
void wili_software_avc_close(wili_software_avc *s)
{if(s){av_frame_free(&s->frame);av_packet_free(&s->packet);avcodec_free_context(&s->codec);free(s);}}
