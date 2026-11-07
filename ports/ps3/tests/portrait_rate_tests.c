// SPDX-License-Identifier: GPL-3.0-or-later
#include "mp4_dash.h"
#include "yuv_frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
static int read_at(void *arg,uint64_t at,void *out,size_t n)
{return fseek(arg,(long)at,SEEK_SET) || fread(out,1,n,arg)!=n?-1:0;}
int main(int argc,char **argv)
{
    assert(argc==7);FILE *f=fopen(argv[1],"rb"),*out=fopen(argv[2],"wb");assert(f && out);
    fseek(f,0,SEEK_END);long size=ftell(f);assert(size>0);
    unsigned w=atoi(argv[3]),h=atoi(argv[4]),fps=atoi(argv[5]),expected=atoi(argv[6]);
    wili_mp4_dash *r=calloc(1,sizeof(*r));uint8_t *packet=malloc(WILI_DASH_PACKET_CAP);assert(r && packet);
    assert(!wili_mp4_dash_open(r,read_at,f,(uint64_t)size,0));
    assert(r->info.coded_width==w && r->info.coded_height==h);
    unsigned num=0,den=0,level=0;uint64_t scan=r->scan;
    assert(!wili_mp4_dash_probe_rate(r,0,&num,&den));assert(num==(uint64_t)fps*den);
    assert(!r->max_fps && r->scan==scan && !r->delivered_samples);
    assert(!wili_mp4_dash_prepare_ps3(r,&level));assert(level==expected);
    size_t stride,arena;assert(!wili_yuv_pool_plan(w,h,10,&stride,&arena));assert(arena<=15*1048576u);
    unsigned frames=0;int rc;do{size_t n=0;wili_dash_sample sample;
        rc=wili_mp4_dash_packet(r,packet,WILI_DASH_PACKET_CAP,&n,&sample);assert(rc>=0);
        if(rc){assert(fwrite(packet,1,n,out)==n);frames++;}
    }while(rc);assert(frames==fps);
    assert(!wili_mp4_dash_seek(r,0,NULL) && !r->max_fps);
    printf("PASS %ux%u %ufps: SPS/MP4, %u AUs, decoder level %u, YUV arena %zu bytes\n",w,h,fps,frames,level,arena);
    free(r);free(packet);fclose(f);fclose(out);
}
