// SPDX-License-Identifier: GPL-3.0-or-later
#include "software_avc.h"
#include "mp4_dash.h"
#include "video_buffer_policy.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned frames,visible_w,visible_h,cancel_after;static int64_t last_pts=-1;
static int read_at(void *arg,uint64_t at,void *out,size_t n){return fseek(arg,(long)at,SEEK_SET) || fread(out,1,n,arg)!=n?-1:0;}
static int emit(void *arg,const uint8_t *const planes[3],const int stride[3],unsigned w,unsigned h,int64_t pts)
{
    if(cancel_after && frames==cancel_after)return -1;
    assert(w==visible_w && h==visible_h && pts>last_pts);last_pts=pts;
    for(unsigned c=0;c<3;c++)for(unsigned y=0;y<(c?h/2:h);y++)assert(fwrite(planes[c]+y*stride[c],1,c?w/2:w,arg)==(c?w/2:w));
    frames++;return 0;
}
int main(int argc,char **argv)
{
    assert(argc==6);visible_w=atoi(argv[3]);visible_h=atoi(argv[4]);unsigned wanted=atoi(argv[5]);
    FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);
    wili_mp4_dash *r=calloc(1,sizeof(*r));uint8_t *packet=malloc(WILI_DASH_PACKET_CAP);assert(r && packet);
    for(unsigned cycle=0;cycle<2;cycle++){
        assert(!wili_mp4_dash_open(r,read_at,f,size,0));assert(r->info.refs>=7);
        wili_software_avc *codec=wili_software_avc_open(r->info.coded_width,r->info.coded_height,r->info.timescale);assert(codec);
        FILE *out=fopen(argv[2],"wb");assert(out);frames=0;last_pts=-1;int rc;
        do{size_t n=0;wili_dash_sample sample;rc=wili_mp4_dash_packet(r,packet,WILI_DASH_PACKET_CAP,&n,&sample);assert(rc>=0);
            if(rc)assert(!wili_software_avc_decode(codec,packet,n,sample.pts,sample.dts,emit,out));
        }while(rc);
        assert(!wili_software_avc_decode(codec,NULL,0,0,0,emit,out));assert(frames==wanted);
        fclose(out);printf("PASS software AVC %ux%u refs=%u frames=%u cycle=%u cpu_us=%llu\n",visible_w,visible_h,r->info.refs,frames,cycle,(unsigned long long)wili_software_avc_cpu_us(codec));wili_software_avc_close(codec);
    }
    assert(!wili_mp4_dash_open(r,read_at,f,size,0));
    wili_software_avc *cancel_codec=wili_software_avc_open(r->info.coded_width,r->info.coded_height,r->info.timescale);assert(cancel_codec);
    frames=0;last_pts=-1;cancel_after=5;FILE *sink=fopen("/dev/null","wb");assert(sink);int canceled=0;
    while(!canceled){size_t n=0;wili_dash_sample sample;assert(wili_mp4_dash_packet(r,packet,WILI_DASH_PACKET_CAP,&n,&sample)>0);
        canceled=wili_software_avc_decode(cancel_codec,packet,n,sample.pts,sample.dts,emit,sink);
    }
    assert(frames==5);fclose(sink);wili_software_avc_close(cancel_codec);
    puts("PASS frame-delivery cancellation with decoder buffers still live, clean context destruction");
    assert(!wili_software_should_buffer(0,37000,0,1)); // old log's repeated false alarms
    assert(!wili_software_should_buffer(0,-33000,0,0));
    assert(wili_software_should_buffer(0,-80001,0,0));
    assert(wili_software_should_buffer(1,140000,0,4));
    assert(!wili_software_should_buffer(1,160000,0,4));
    assert(!wili_software_should_buffer(1,50000,0,6)); // 120fps: six frames fit a ten-slot pool
    assert(wili_software_should_buffer(1,-100000,0,6));
    assert(!wili_software_should_buffer(1,-1000,1,0));
    assert(!wili_software_prefill_ready(5,0));assert(wili_software_prefill_ready(6,0));assert(wili_software_prefill_ready(1,1));
    fclose(f);free(r);free(packet);puts("PASS decoded-frame headroom controls audio even with compressed data available");
}
