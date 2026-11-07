// SPDX-License-Identifier: GPL-3.0-or-later
#include "avc_test_stream.h"
#include <string.h>

typedef struct {const uint8_t *data;size_t bits, pos;int error;} bits;
static uint32_t read_bits(bits *b,unsigned n)
{
    uint32_t result=0;
    if(n>32 || b->pos>b->bits || n>b->bits-b->pos){b->error=1;return 0;}
    while(n--){result=(result<<1)|((b->data[b->pos/8]>>(7-b->pos%8))&1);b->pos++;}
    return result;
}
static uint32_t ue(bits *b)
{
    unsigned zeros=0;
    while(!b->error && read_bits(b,1)==0){if(++zeros>30){b->error=1;return 0;}}
    if(b->error)return 0;
    return ((1u<<zeros)-1u)+read_bits(b,zeros);
}
int wili_parse_avc_sps(const uint8_t *p,size_t size,wili_test_stream *out)
{
    uint8_t rbsp[256];size_t i,n=0;unsigned zeros=0;
    if(!p || !out || size<5 || size>sizeof(rbsp) || (p[0]&0x9f)!=7)return -1;
    for(i=1;i<size;i++){
        if(zeros>=2 && p[i]==3){if(i+1==size || p[i+1]>3)return -1;zeros=0;continue;}
        rbsp[n++]=p[i];zeros=p[i]==0 ? zeros+1 : 0;
    }
    bits b={rbsp,n*8,0,0};
    unsigned profile=read_bits(&b,8),constraints=read_bits(&b,8),level=read_bits(&b,8);
    // Restrict the first hardware test to ordinary baseline 8-bit 4:2:0.
    if(profile!=66 || (constraints&3) || level<10 || level>31 || ue(&b)>31)return -1;
    if(ue(&b)>12)return -1; // log2_max_frame_num_minus4
    unsigned poc=ue(&b);
    if(poc==0){if(ue(&b)>12)return -1;}else if(poc!=2)return -1;
    unsigned refs=ue(&b);(void)read_bits(&b,1);
    unsigned wm=ue(&b),hm=ue(&b);
    if(wm>79 || hm>44 || refs>4 || read_bits(&b,1)!=1)return -1;
    (void)read_bits(&b,1); // direct_8x8_inference_flag
    unsigned left=0,right=0,top=0,bottom=0;
    if(read_bits(&b,1)){left=ue(&b);right=ue(&b);top=ue(&b);bottom=ue(&b);}
    (void)read_bits(&b,1); // VUI presence; this fixed test uses externally checked cadence.
    unsigned width=(wm+1)*16,height=(hm+1)*16;
    if(b.error || left>width/2 || right>width/2 || top>height/2 || bottom>height/2 ||
       left+right>=width/2 || top+bottom>=height/2)return -1;
    out->coded_width=width;out->coded_height=height;
    out->display_width=width-2*(left+right);out->display_height=height-2*(top+bottom);
    out->profile=profile;out->level=level;out->reference_frames=refs;
    return 0;
}
static size_t start_code(const uint8_t *p,size_t size,size_t at,unsigned *prefix)
{
    size_t i;
    for(i=at;i<size && size-i>=3;i++){
        if(p[i] || p[i+1])continue;
        if(p[i+2]==1){*prefix=3;return i;}
        if(size-i>=4 && !p[i+2] && p[i+3]==1){*prefix=4;return i;}
    }
    *prefix=0;return size;
}
int wili_parse_test_avc(const uint8_t *p,size_t size,wili_test_stream *out)
{
    size_t at=0,au=0,sps_at=0,sps_size=0,pps_at=0,pps_size=0;
    unsigned prefix=0,slices=0,aus=0;
    int has_aud=0;
    if(!p || !out || size<8 || size>4*1024*1024)return -1;
    memset(out,0,sizeof(*out));
    if(start_code(p,size,0,&prefix)!=0)return -1;
    while(at<size){
        size_t nal=at+prefix;
        unsigned next_prefix=0;
        size_t next=start_code(p,size,nal,&next_prefix);
        if(nal>=next || (p[nal]&0x80))return -1;
        unsigned type=p[nal]&31;
        if(type==9){
            if(has_aud){
                if(slices!=1 || aus>=WILI_TEST_MAX_AUS)return -1;
                out->aus[aus++]=(wili_test_au){(uint32_t)au,(uint32_t)(at-au)};
            }
            has_aud=1;au=at;slices=0;
        }else if(!has_aud)return -1;
        else if(type==7){
            if(sps_size){if(next-nal!=sps_size || memcmp(p+sps_at,p+nal,sps_size))return -1;}
            else {if(wili_parse_avc_sps(p+nal,next-nal,out))return -1;sps_at=nal;sps_size=next-nal;}
        }else if(type==8){
            if(pps_size){if(next-nal!=pps_size || memcmp(p+pps_at,p+nal,pps_size))return -1;}
            else {pps_at=nal;pps_size=next-nal;}
        }else if(type==1 || type==5){
            if(!sps_size || !pps_size || ++slices!=1 || (!aus && type!=5))return -1;
        }else if(type!=6)return -1;
        at=next;prefix=next_prefix;
    }
    if(!has_aud || slices!=1 || aus>=WILI_TEST_MAX_AUS)return -1;
    out->aus[aus++]=(wili_test_au){(uint32_t)au,(uint32_t)(size-au)};
    out->count=aus;
    return 0;
}
uint32_t wili_crc32_update(uint32_t crc,const uint8_t *p,size_t size)
{
    size_t i;unsigned bit;
    for(i=0;i<size;i++){crc^=p[i];for(bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));}
    return crc;
}
uint32_t wili_test_crc32(const uint8_t *p,size_t size){return ~wili_crc32_update(~0u,p,size);}
