// SPDX-License-Identifier: GPL-3.0-or-later
#include "yuv_frame.h"
#include "video_geometry.h"
#include <string.h>
#include <math.h>
static uint32_t align128(uint32_t n){return (n+127u)&~127u;}
int wili_yuv_layout_init(wili_yuv_layout *l,unsigned w,unsigned h)
{
    if(!l || !w || !h || (w&1) || (h&1) || !wili_video_geometry(w,h))return -1;
    memset(l,0,sizeof(*l));l->width=w;l->height=h;
    l->pitch[0]=align128(w);l->pitch[1]=l->pitch[2]=align128(w/2);
    uint32_t y=l->pitch[0]*h,uv=l->pitch[1]*(h/2);
    l->offset[1]=align128(y);l->offset[2]=align128(l->offset[1]+uv);
    l->bytes=align128(l->offset[2]+uv);l->packed_bytes=w*h*3/2;
    return 0;
}
int wili_yuv_repack(uint8_t *frame,size_t cap,const wili_yuv_layout *l)
{
    if(!frame || !l || cap<l->bytes)return -1;
    size_t y=(size_t)l->width*l->height,uv=y/4;
    // Expand backwards so aligned RSX rows never overwrite unread V/U/Y data.
    for(int plane=2;plane>=0;plane--){
        unsigned width=plane?l->width/2:l->width,rows=plane?l->height/2:l->height;
        size_t source=plane==2?y+uv:plane==1?y:0;
        for(unsigned row=rows;row;row--){
            size_t src=source+(row-1)*width,dst=l->offset[plane]+(row-1)*l->pitch[plane];
            if(src!=dst)memmove(frame+dst,frame+src,width);
        }
    }
    return 0;
}
int wili_yuv_pool_plan(unsigned w,unsigned h,unsigned count,size_t *stride,size_t *arena)
{
    wili_yuv_layout layout;
    if(!stride || !arena || !count || count>32 || wili_yuv_layout_init(&layout,w,(h+31u)&~31u))return -1;
    *stride=align128(layout.bytes+256u);
    *arena=(*stride*count+1048575u)&~(size_t)1048575u;
    return 0;
}
static unsigned byte(float f){if(f<0)f=0;if(f>255)f=255;return (unsigned)floorf(f+0.5f);}
uint32_t wili_yuv_reference(unsigned y,unsigned u,unsigned v,int bt709,int full)
{
    float yy=full?(float)y:((float)y-16)*255.0f/219.0f;
    float cb=((float)u-128)*(full?1.0f:255.0f/224.0f),cr=((float)v-128)*(full?1.0f:255.0f/224.0f);
    float kr=bt709?0.2126f:0.299f,kb=bt709?0.0722f:0.114f,kg=1-kr-kb;
    unsigned r=byte(yy+2*(1-kr)*cr),b=byte(yy+2*(1-kb)*cb);
    unsigned g=byte(yy-2*kb*(1-kb)/kg*cb-2*kr*(1-kr)/kg*cr);
    return (r<<16)|(g<<8)|b;
}
