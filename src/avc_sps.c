// SPDX-License-Identifier: GPL-3.0-or-later
#include "avc_sps.h"
#include "video_geometry.h"
#include <string.h>

typedef struct {const uint8_t *p;size_t bits,pos;int error;} bits;

static uint32_t rb(bits *b,unsigned n)
{
    uint32_t v=0;
    if(n>32 || b->pos>b->bits || n>b->bits-b->pos){b->error=1;return 0;}
    while(n--){v=(v<<1)|((b->p[b->pos>>3]>>(7-(b->pos&7)))&1);b->pos++;}
    return v;
}
static uint32_t ue(bits *b)
{
    unsigned zeros=0;
    while(!b->error && rb(b,1)==0){if(++zeros>31){b->error=1;return 0;}}
    if(b->error)return 0;
    uint32_t tail=zeros ? rb(b,zeros) : 0;
    if(b->error)return 0;
    return ((uint32_t)1<<zeros)-1u+tail;
}
static int32_t se(bits *b)
{
    uint32_t v=ue(b);if(b->error)return 0;
    return (v&1) ? (int32_t)((v+1)/2) : -(int32_t)(v/2);
}
size_t wili_avc_rbsp(const uint8_t *nal,size_t size,uint8_t *out,size_t cap)
{
    if(!nal || !out || !size || size>cap)return 0;
    size_t at=1,n=0;unsigned zeros=0;
    for(;at<size;at++){
        unsigned byte=nal[at];
        if(zeros>=2 && byte==3){
            if(at+1==size || nal[at+1]>3)return 0;
            zeros=0;continue;
        }
        out[n++]=byte;zeros=byte ? 0 : zeros+1;
    }
    return n;
}
static void scaling_list(bits *b,unsigned size)
{
    unsigned last=8,next=8,i;
    for(i=0;i<size && !b->error;i++){
        if(next){int32_t delta=se(b);next=(unsigned)((last+delta+256)%256);}
        last=next ? next : last;
    }
}
static void hrd(bits *b)
{
    uint32_t count=ue(b);(void)rb(b,4);(void)rb(b,4);
    if(count>31){b->error=1;return;}
    uint32_t i;for(i=0;i<=count && !b->error;i++){(void)ue(b);(void)ue(b);(void)rb(b,1);}
    (void)rb(b,5);(void)rb(b,5);(void)rb(b,5);(void)rb(b,5);
}
static void vui(bits *b,wili_avc_sps *s)
{
    if(rb(b,1)){
        s->aspect_ratio_idc=rb(b,8);
        if(s->aspect_ratio_idc==255){s->sar_width=rb(b,16);s->sar_height=rb(b,16);}
    }
    if(rb(b,1))(void)rb(b,1);
    if(rb(b,1)){(void)rb(b,3);s->video_full_range=rb(b,1);if(rb(b,1)){(void)rb(b,8);(void)rb(b,8);s->matrix_coefficients=rb(b,8);}}
    if(rb(b,1)){(void)ue(b);(void)ue(b);}
    if(rb(b,1)){
        s->timing_present=1;s->num_units_in_tick=rb(b,32);s->time_scale=rb(b,32);s->fixed_frame_rate=rb(b,1);
    }
    unsigned nal_hrd=rb(b,1);
    if(nal_hrd)hrd(b);
    unsigned vcl_hrd=rb(b,1);
    if(vcl_hrd)hrd(b);
    s->hrd_present=nal_hrd || vcl_hrd;
    if(nal_hrd || vcl_hrd)(void)rb(b,1);
    (void)rb(b,1); // pic_struct_present_flag precedes bitstream_restriction_flag
    if(rb(b,1)){
        s->bitstream_restriction=1;(void)rb(b,1);(void)ue(b);(void)ue(b);(void)ue(b);(void)ue(b);
        s->max_num_reorder_frames=ue(b);s->max_dec_frame_buffering=ue(b);
    }
}
int wili_avc_sps_parse(const uint8_t *nal,size_t size,wili_avc_sps *out)
{
    if(!nal || !out || size<4 || size>256 || (nal[0]&0x80) || (nal[0]&31)!=7)return -1;
    uint8_t rbsp[256];size_t n=wili_avc_rbsp(nal,size,rbsp,sizeof(rbsp));
    if(n<3)return -1;
    memset(out,0,sizeof(*out));
    bits b={rbsp,n*8,0,0};
    out->profile=rb(&b,8);out->constraint_flags=rb(&b,8);out->level=rb(&b,8);out->sps_id=ue(&b);
    if(b.error || out->sps_id>31)return -1;
    if(out->profile!=66 && out->profile!=77 && out->profile!=88 && out->profile!=100)return -1;
    out->chroma_format_idc=1;out->bit_depth_luma=out->bit_depth_chroma=8;
    if(out->profile==100){
        out->chroma_format_idc=ue(&b);
        if(out->chroma_format_idc==3)out->separate_colour_plane=rb(&b,1);
        out->bit_depth_luma=8+ue(&b);out->bit_depth_chroma=8+ue(&b);(void)rb(&b,1);
        if(rb(&b,1)){
            unsigned count=out->chroma_format_idc!=3 ? 8 : 12,i;
            for(i=0;i<count && !b.error;i++)if(rb(&b,1))scaling_list(&b,i<6 ? 16 : 64);
        }
    }
    out->log2_max_frame_num_minus4=ue(&b);
    out->pic_order_cnt_type=ue(&b);
    if(out->pic_order_cnt_type==0)out->log2_max_poc_lsb_minus4=ue(&b);
    else if(out->pic_order_cnt_type==1){
        (void)rb(&b,1);(void)se(&b);(void)se(&b);uint32_t count=ue(&b);
        if(count>255){b.error=1;}else {uint32_t i;for(i=0;i<count && !b.error;i++)(void)se(&b);}
    }else if(out->pic_order_cnt_type!=2)b.error=1;
    out->max_num_ref_frames=ue(&b);out->gaps_in_frame_num_value_allowed=rb(&b,1);
    uint32_t wm=ue(&b),hm=ue(&b);
    out->frame_mbs_only=rb(&b,1);
    if(!out->frame_mbs_only)out->mb_adaptive_frame_field=rb(&b,1);
    out->direct_8x8_inference=rb(&b,1);
    out->frame_cropping=rb(&b,1);
    if(out->frame_cropping){
        out->crop_left=ue(&b);out->crop_right=ue(&b);out->crop_top=ue(&b);out->crop_bottom=ue(&b);
    }
    out->vui_present=rb(&b,1);
    if(out->vui_present)vui(&b,out);
    if(b.error)return -1;
    if(out->chroma_format_idc!=1 || out->separate_colour_plane || out->bit_depth_luma!=8 || out->bit_depth_chroma!=8)return -1;
    if(out->log2_max_frame_num_minus4>12 || out->log2_max_poc_lsb_minus4>12)return -1;
    if(out->pic_order_cnt_type!=0 && out->pic_order_cnt_type!=2)return -1;
    if(out->max_num_ref_frames>16 || wm>119 || hm>79 || !out->frame_mbs_only)return -1;
    if(out->max_num_reorder_frames>8 || out->max_dec_frame_buffering>16)return -1;
    uint32_t width=(wm+1)*16,height=(hm+1)*16;
    if(!wili_video_geometry(width,height))return -1;
    uint32_t crop_x=2*(out->crop_left+out->crop_right),crop_y=2*(out->crop_top+out->crop_bottom);
    if(crop_x>=width/2 || crop_y>=height/2)return -1;
    out->coded_width=width;out->coded_height=height;
    out->display_width=width-crop_x;out->display_height=height-crop_y;
    if(!out->display_width || !out->display_height)return -1;
    out->rbsp_bytes=(unsigned)n;out->bits_consumed=(unsigned)b.pos;
    return 0;
}

int wili_avc_ps3_level(const wili_avc_sps *s,unsigned *level)
{
    if(!s || !level || !s->coded_width || !s->coded_height ||
       !wili_video_geometry(s->coded_width,s->coded_height) || !s->frame_mbs_only ||
       s->chroma_format_idc!=1 || s->bit_depth_luma!=8 || s->bit_depth_chroma!=8)return -1;
    unsigned mbs=(s->coded_width/16)*(s->coded_height/16),dpb=s->max_num_ref_frames;
    if(s->max_dec_frame_buffering>dpb)dpb=s->max_dec_frame_buffering;
    if(s->max_num_reorder_frames+1>dpb)dpb=s->max_num_reorder_frames+1;
    if(mbs>8192 || (uint64_t)mbs*dpb>32768 || s->hrd_present)return -1;
    if(s->level<=42){
        static const unsigned valid[]={10,11,12,13,20,21,22,30,31,32,40,41,42};
        for(unsigned i=0;i<sizeof(valid)/sizeof(valid[0]);i++)if(s->level==valid[i]){*level=s->level;return 0;}
        return -1;
    }
    if(s->level!=50 && s->level!=51 && s->level!=52)return -1;
    // The container-aware preparation step adjusts this default according to
    // measured macroblocks/second. SPS dimensions alone do not imply 30fps.
    *level=41;return 0;
}
int wili_avc_normalize_annexb(uint8_t *data,size_t size,const wili_avc_sps *expected,unsigned level)
{
    size_t at=0;
    while(at+4<=size){
        size_t start=at;
        while(start+3<size && !(data[start]==0 && data[start+1]==0 &&
              (data[start+2]==1 || (data[start+2]==0 && data[start+3]==1))))start++;
        if(start+3>=size)break;
        size_t nal=start+(data[start+2]==1?3:4),end=nal;
        while(end+3<size && !(data[end]==0 && data[end+1]==0 &&
              (data[end+2]==1 || (data[end+2]==0 && data[end+3]==1))))end++;
        if(end+3>=size)end=size;
        if(nal>=end)return -1;
        if((data[nal]&31)==7){
            wili_avc_sps s;unsigned accepted;
            if(wili_avc_sps_parse(data+nal,end-nal,&s) || wili_avc_ps3_level(&s,&accepted) ||
               s.coded_width!=expected->coded_width || s.coded_height!=expected->coded_height ||
               s.profile!=expected->profile || accepted>level)return -1;
            if(s.level>42)data[nal+3]=(uint8_t)level;
        }
        at=end;
    }
    return 0;
}
