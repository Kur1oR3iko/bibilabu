// SPDX-License-Identifier: GPL-3.0-or-later
#include "yuv_frame.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    const unsigned shapes[][2]={{1280,720},{720,1280},{480,852},{480,864},{736,736},{864,480},{720,720},{1280,718},{864,478},{1920,1088},{320,240},{16,16}};
    for(unsigned k=0;k<sizeof(shapes)/sizeof(shapes[0]);k++){
        wili_yuv_layout l;assert(!wili_yuv_layout_init(&l,shapes[k][0],shapes[k][1]));
        assert(!(l.offset[1]%128) && !(l.offset[2]%128));
        unsigned char *p=malloc(l.bytes+128),*original=malloc(l.packed_bytes);assert(p && original);
        memset(p,0xa5,l.bytes+128);
        for(unsigned i=0;i<l.packed_bytes;i++)p[i]=original[i]=(unsigned char)(i*17u+i/23u);
        assert(!wili_yuv_repack(p,l.bytes,&l));
        size_t y=(size_t)l.width*l.height,uv=y/4;
        for(unsigned plane=0;plane<3;plane++){
            unsigned w=plane?l.width/2:l.width,h=plane?l.height/2:l.height;
            size_t start=plane==2?y+uv:plane==1?y:0;assert(!(l.pitch[plane]%128));
            for(unsigned row=0;row<h;row++)assert(!memcmp(p+l.offset[plane]+row*l.pitch[plane],original+start+row*w,w));
        }
        for(unsigned i=0;i<128;i++)assert(p[l.bytes+i]==0xa5);
        assert(wili_yuv_repack(p,l.bytes-1,&l)<0);free(p);free(original);
    }
    size_t stride,arena;assert(!wili_yuv_pool_plan(1280,720,10,&stride,&arena));assert(arena==14*1048576u && !(stride%128));
    assert(!wili_yuv_pool_plan(864,480,10,&stride,&arena));assert(arena==7*1048576u);
    assert(!wili_yuv_pool_plan(720,1280,10,&stride,&arena));assert(arena==15*1048576u);
    assert(wili_yuv_pool_plan(1281,720,10,&stride,&arena)<0);
    assert(wili_yuv_reference(16,128,128,1,0)==0);assert(wili_yuv_reference(235,128,128,1,0)==0xffffff);
    assert(wili_yuv_reference(0,128,128,0,1)==0);assert(wili_yuv_reference(255,128,128,0,1)==0xffffff);
    uint32_t red=wili_yuv_reference(81,90,240,0,0);assert((red>>16)>250 && ((red>>8)&255)<3 && (red&255)<3);
    puts("PASS YUV plane repack/alignment/guards, compact 14MiB 720p pool and full/limited reference colors");
}
