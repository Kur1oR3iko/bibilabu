// SPDX-License-Identifier: GPL-3.0-or-later
#include "resampler.h"
#include <string.h>
int wili_resampler_init(wili_resampler *r,uint32_t src_rate,uint32_t dst_rate)
{
    if(!r || !src_rate || !dst_rate || src_rate>192000 || dst_rate>192000)return -1;
    memset(r,0,sizeof(*r));r->src_rate=src_rate;r->dst_rate=dst_rate;return 0;
}
uint64_t wili_resampler_output_for(uint32_t src_rate,uint32_t dst_rate,uint64_t source_samples)
{
    if(!src_rate || !dst_rate)return 0;
    return (uint64_t)((double)source_samples*dst_rate/src_rate);
}
static int source(const wili_resampler *r,const float *in,uint32_t frames,uint64_t index,float *out)
{
    if(index==r->frame_base-1 && r->have_prev){out[0]=r->prev[0];out[1]=r->prev[1];return 0;}
    if(index<r->frame_base || index>=r->frame_base+frames)return -1;
    uint32_t at=(uint32_t)(index-r->frame_base);
    out[0]=in[at*2];out[1]=in[at*2+1];return 0;
}
int wili_resampler_feed(wili_resampler *r,const float *in,uint32_t frames,float *out,uint32_t cap,uint32_t *produced)
{
    if(!r || !in || !out || !produced || !frames || frames>8192 || !cap || !r->src_rate || !r->dst_rate)return -1;
    *produced=0;
    uint64_t limit=r->frame_base+frames-1; // last source index whose +1 is available
    while(*produced<cap){
        uint64_t num=r->out_index*r->src_rate;
        uint64_t q=num/r->dst_rate,rem=num%r->dst_rate;
        if(q+1>limit)break;
        float a[2],b[2];
        if(source(r,in,frames,q,a) || source(r,in,frames,q+1,b))return -1;
        double f=(double)rem/(double)r->dst_rate;
        uint32_t at=*produced;
        out[at*2]=(float)(a[0]+(b[0]-a[0])*f);out[at*2+1]=(float)(a[1]+(b[1]-a[1])*f);
        r->out_index++;(*produced)++;
    }
    r->prev[0]=in[(frames-1)*2];r->prev[1]=in[(frames-1)*2+1];r->have_prev=1;
    r->frame_base+=frames;
    return 0;
}
