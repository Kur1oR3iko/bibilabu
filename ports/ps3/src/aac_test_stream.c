// SPDX-License-Identifier: GPL-3.0-or-later
#include "aac_test_stream.h"
int wili_parse_test_aac(const uint8_t *p,size_t size,wili_aac_stream *out)
{
    if(!p || !out || size<7 || size>4*1024*1024)return -1;
    size_t at=0;out->count=0;
    while(at<size){
        if(size-at<7 || out->count==WILI_AAC_MAX_FRAMES)return -1;
        const uint8_t *h=p+at;
        if(h[0]!=255 || (h[1]&0xfe)!=0xf0 || !(h[1]&1))return -1;
        unsigned profile=h[2]>>6,frequency=(h[2]>>2)&15,channels=((h[2]&1)<<2)|(h[3]>>6);
        unsigned length=((h[3]&3)<<11)|(h[4]<<3)|(h[5]>>5);
        if(profile!=1 || frequency!=3 || channels!=2 || (h[6]&3) || length<=7 || length>size-at)return -1;
        out->frames[out->count++]=(wili_aac_frame){(uint32_t)at,length};at+=length;
    }
    return out->count ? 0 : -1;
}
