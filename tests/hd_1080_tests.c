// SPDX-License-Identifier: GPL-3.0-or-later
#include "mp4_dash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int read_at(void *arg,uint64_t at,void *out,size_t n)
{return fseek(arg,(long)at,SEEK_SET) || fread(out,1,n,arg)!=n?-1:0;}
int main(int argc,char **argv)
{
    assert(argc==4);FILE *f=fopen(argv[1],"rb");assert(f);
    fseek(f,0,SEEK_END);long size=ftell(f);assert(size>0);
    wili_mp4_dash *r=calloc(1,sizeof(*r));uint8_t *packet=malloc(WILI_DASH_PACKET_CAP);assert(r && packet);
    assert(!wili_mp4_dash_open(r,read_at,f,(uint64_t)size,0));
    assert(r->info.coded_width==1920 && r->info.coded_height==1088);
    assert(r->info.sps.fixed_frame_rate==0); // normal x264 MP4, formerly rejected
    uint64_t scan=r->scan;unsigned num=0,den=0,level=0;
    int rate=wili_mp4_dash_probe_rate(r,30,&num,&den);
    assert(r->scan==scan && !r->delivered_samples && !r->eof && !r->error);
    if(!strcmp(argv[3],"reject-rate")){
        assert(rate<0);r->max_fps=30;
        size_t n;wili_dash_sample sample;
        assert(wili_mp4_dash_packet(r,packet,WILI_DASH_PACKET_CAP,&n,&sample)<0 && r->stage==81);
        puts("PASS 1080p60 rejected by actual MP4 duration, including per-sample runtime guard");
    }else if(!strcmp(argv[3],"reject-dpb")){
        assert(!rate);assert(wili_mp4_dash_prepare_ps3(r,&level)<0);
        puts("PASS excessive 1080p DPB rejected after correct VUI parsing");
    }else{
        assert(!rate && num && den && (uint64_t)num<=30ULL*den);
        assert(!wili_mp4_dash_prepare_ps3(r,&level));assert(level<=41);
        FILE *out=fopen(argv[2],"wb");assert(out);unsigned count=0;int rc;
        do{size_t n=0;wili_dash_sample sample;rc=wili_mp4_dash_packet(r,packet,WILI_DASH_PACKET_CAP,&n,&sample);
            assert(rc>=0);if(rc){assert(fwrite(packet,1,n,out)==n);count++;}}while(rc);
        assert(count>0);fclose(out);
        assert(!wili_mp4_dash_seek(r,0,NULL) && r->max_fps==30);
        printf("PASS 1080p fps=%u/%u source_level=%u decoder_level=%u refs=%u reorder=%u packets=%u fixed_flag=0\n",
            num,den,r->info.level,level,r->info.refs,r->info.sps.max_num_reorder_frames,count);
    }
    fclose(f);free(packet);free(r);return 0;
}
