// SPDX-License-Identifier: GPL-3.0-or-later
// Host regression for the real Bilibili DASH format. Fixtures contain the real
// init/sidx/moof boxes plus deterministic dummy mdat payload (no media bytes).
#define _FILE_OFFSET_BITS 64
#include "mp4_dash.h"
#include "resampler.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {uint8_t *data;size_t size;} memory;
static int mem_read(void *arg,uint64_t at,void *out,size_t n)
{
    memory *m=arg;if(at>m->size || n>m->size-at)return -1;
    memcpy(out,m->data+(size_t)at,n);return 0;
}
static memory load(const char *path)
{
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n<8*1024*1024);rewind(f);
    uint8_t *p=malloc((size_t)n);assert(p);assert(fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);
    return (memory){p,(size_t)n};
}
static uint8_t *packet_buffer(void){uint8_t *p=malloc(WILI_DASH_PACKET_CAP);assert(p);return p;}
static unsigned hex(const char *s,uint8_t *out,size_t cap)
{
    size_t n=strlen(s);assert(n%2==0 && n/2<=cap);size_t i;
    for(i=0;i<n/2;i++){unsigned v;assert(sscanf(s+i*2,"%2x",&v)==1);out[i]=(uint8_t)v;}
    return (unsigned)(n/2);
}
static void sps_tests(void)
{
    uint8_t sps[64];const char *real="6764001eacca7020096ffc1144114040000003004000000783c58b29e0";
    unsigned n=hex(real,sps,sizeof(sps));wili_avc_sps s;
    assert(!wili_avc_sps_parse(sps,n,&s));
    assert(s.profile==100 && s.level==30 && s.coded_width==512 && s.coded_height==288);
    assert(s.display_width==512 && s.display_height==288 && s.frame_cropping==0);
    assert(s.chroma_format_idc==1 && s.bit_depth_luma==8 && s.bit_depth_chroma==8);
    // Independently decoded by FFmpeg trace_headers: pic_struct=0,
    // restriction=1, reorder=4, DPB=6. The old parser skipped pic_struct.
    assert(s.max_num_ref_frames==6 && s.max_num_reorder_frames==4 && s.bitstream_restriction==1 && s.max_dec_frame_buffering==6);
    assert(s.pic_order_cnt_type==0 && s.frame_mbs_only==1 && s.vui_present==1);
    assert(s.sar_width==1105 && s.sar_height==1104 && s.timing_present==1);
    assert(s.num_units_in_tick==1 && s.time_scale==30 && s.fixed_frame_rate==0);
    // Reject High10 / interlaced / truncated / forbidden bit.
    uint8_t bad[64];memcpy(bad,sps,n);bad[1]=110;assert(wili_avc_sps_parse(bad,n,&s)<0);
    memcpy(bad,sps,n);bad[0]|=0x80;assert(wili_avc_sps_parse(bad,n,&s)<0);
    for(unsigned i=0;i<n;i++){memcpy(bad,sps,n);bad[i]^=0x55;(void)wili_avc_sps_parse(bad,n-i/4,&s);}
    puts("PASS SPS: real Bilibili header agrees with FFmpeg: reorder=4 DPB=6, fixed-rate flag unset, rejected/mutated variants");
}
static void resampler_tests(void)
{
    wili_resampler r;assert(!wili_resampler_init(&r,44100,48000));
    float in[1024*2],out[2048*2];uint64_t expected=0;
    for(unsigned frame=0;frame<400;frame++){
        for(unsigned i=0;i<1024;i++){
            double x=(double)(frame*1024+i)/1000000.0;
            in[i*2]=(float)(x-1.0);in[i*2+1]=(float)(1.0-x);
        }
        uint32_t produced=0;assert(!wili_resampler_feed(&r,in,1024,out,2048,&produced));
        assert(produced>1000 && produced<1200);
        uint64_t base=expected;
        for(uint32_t k=0;k<produced;k++){
            double pos=(double)(base+k)*44100.0/48000.0;
            double want=pos/1000000.0-1.0;
            assert(out[k*2]>want-0.00001 && out[k*2]<want+0.00001);
            double want_r=1.0-pos/1000000.0;
            assert(out[k*2+1]>want_r-0.00001 && out[k*2+1]<want_r+0.00001);
        }
        expected+=produced;
    }
    uint64_t want=wili_resampler_output_for(44100,48000,400*1024);
    assert(expected>=want-2 && expected<=want+2);
    assert(wili_resampler_output_for(48000,48000,1000)==1000);
    wili_resampler p;assert(!wili_resampler_init(&p,48000,48000));
    uint32_t produced=0;assert(!wili_resampler_feed(&p,in,1024,out,2048,&produced));
    assert(produced==1023); // last source sample is carried for interpolation
    puts("PASS resampler: 44100->48000 exact rational positions over 400 frames, 48000 passthrough");
}
static void video_tests(void)
{
    memory m=load("tests/fixtures/bili_video_single.bin");
    wili_mp4_dash *r=malloc(sizeof(*r));assert(r);uint8_t *packet=packet_buffer();
    assert(!wili_mp4_dash_open(r,mem_read,&m,m.size,0));
    const wili_dash_info *i=wili_mp4_dash_info(r);
    assert(i->timescale==16000 && i->coded_width==512 && i->coded_height==288);
    assert(i->display_width==512 && i->display_height==288 && i->level==30 && i->refs==6);
    assert(i->nal_length_size==4 && i->config_size>30 && i->sar_width==1105 && i->sar_height==1104);
    assert(i->start_pts==3200 && i->empty_edit_ms==67);
    assert(i->sidx_references==1 && i->sidx_duration_ticks==80000 && i->sidx_timescale==16000);
    assert(i->sidx_total_bytes+1544==m.size);
    unsigned count=0,vcl=0,bframes=0;uint64_t expected_dts=0;
    for(;;){
        size_t bytes=0;wili_dash_sample s;int rc=wili_mp4_dash_packet(r,packet,WILI_DASH_PACKET_CAP,&bytes,&s);
        assert(rc>=0);if(!rc)break;
        assert(s.dts==expected_dts);expected_dts+=s.duration;
        assert(s.pts>=s.dts);if(s.pts!=s.dts)bframes++;
        assert(bytes>16 && bytes<=WILI_DASH_PACKET_CAP);
        assert(!memcmp(packet,"\0\0\0\1\x09",5)); // AUD first
        size_t at=6;
        if(count==0){
            assert(!memcmp(packet+at,"\0\0\0\1\x67",5)); // SPS injected before SEI/VCL
            at+=i->config_size;
        }
        while(at+4<bytes){
            assert(!memcmp(packet+at,"\0\0\0\1",4));
            unsigned type=packet[at+4]&31;
            if(type==1 || type==5)vcl++;
            at+=5;
            while(at<bytes && !(at+4<=bytes && !memcmp(packet+at,"\0\0\0\1",4)))at++;
        }
        count++;
    }
    assert(count==75 && vcl==75 && bframes==66 && r->fragments==1);
    printf("PASS video: %u samples, %u VCL, %u B-frames (cto!=0), AUD/SPS/PPS/SEI/VCL order, dts continuity\n",count,vcl,bframes);
    // Malformed variants: oversized init box, broken SPS, broken sidx size, truncated data.
    size_t avcc_at=0;for(size_t k=0;k+4<1032;k++)if(!memcmp(m.data+k,"avcC",4)){avcc_at=k;break;}
    assert(avcc_at>8);
    for(unsigned mode=0;mode<4;mode++){
        uint8_t *copy=malloc(m.size);assert(copy);memcpy(copy,m.data,m.size);memory bad={copy,m.size};
        if(mode==0)memset(copy,0xff,4);                                  // oversized first box
        else if(mode==1)copy[avcc_at+4+8]=0xff;                           // corrupt SPS NAL header
        else if(mode==2){copy[1032+30]=0xff;copy[1032+31]=0xff;}          // impossible sidx count
        else bad.size=m.size/2;                                            // truncated media
        assert(wili_mp4_dash_open(r,mem_read,&bad,bad.size,0)<0);
        free(copy);
    }
    free(r);free(packet);free(m.data);
}
static void audio_tests(void)
{
    memory m=load("tests/fixtures/bili_audio_single.bin");
    wili_mp4_dash *r=malloc(sizeof(*r));assert(r);uint8_t *packet=packet_buffer();
    assert(!wili_mp4_dash_open(r,mem_read,&m,m.size,1));
    const wili_dash_info *i=wili_mp4_dash_info(r);
    assert(i->timescale==44100 && i->sample_rate==44100 && i->sample_rate_index==4);
    assert(i->channels==2 && i->audio_object_type==2 && i->config_size==2);
    assert(i->config[0]==0x12 && i->config[1]==0x10 && i->start_pts==0);
    assert(i->sidx_references==1 && i->sidx_duration_ticks==220141 && i->sidx_total_bytes+1445==m.size);
    unsigned count=0;uint64_t expected_dts=0;
    for(;;){
        size_t bytes=0;wili_dash_sample s;int rc=wili_mp4_dash_packet(r,packet,WILI_DASH_PACKET_CAP,&bytes,&s);
        assert(rc>=0);if(!rc)break;
        assert(s.dts==expected_dts && s.pts==s.dts);expected_dts+=s.duration;
        assert(packet[0]==0xff && packet[1]==0xf1 && packet[2]==0x50); // 44100 index 4, stereo
        assert(packet[6]==0xfc);
        unsigned length=((packet[3]&3)<<11)|(packet[4]<<3)|(packet[5]>>5);
        assert(length==bytes && bytes==s.size+7);
        count++;
    }
    assert(count==215 && r->fragments==1);
    printf("PASS audio: %u AUs, ASC 1210 -> ADTS 44100 stereo, tfhd/trun defaults, timestamps\n",count);
    free(r);free(packet);free(m.data);
}
int main(void)
{
    sps_tests();resampler_tests();video_tests();audio_tests();
    puts("PASS real Bilibili DASH container/SPS/ASC/timestamp/resampler host regressions");
    return 0;
}
