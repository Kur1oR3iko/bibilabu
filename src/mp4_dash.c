// SPDX-License-Identifier: GPL-3.0-or-later
// Bounded Bilibili DASH fMP4 reader. Structure verified from a fresh anonymous
// playurl: see evidence/REAL_MEDIA_1.0.md. No whole-file allocation, no stbl
// table assumption, no fixed frame count/rate/size.
#include "mp4_dash.h"
#include "video_geometry.h"
#include <string.h>
#include <stdlib.h>
#define FCC(a,b,c,d) ((uint32_t)(a)<<24|(uint32_t)(b)<<16|(uint32_t)(c)<<8|(uint32_t)(d))
typedef struct {size_t payload,end;uint32_t type;} box;
typedef struct {size_t at;uint32_t size;unsigned type;} nal_desc;

static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t be64(const uint8_t *p){return (uint64_t)be32(p)<<32|be32(p+4);}
static unsigned be16(const uint8_t *p){return (unsigned)p[0]<<8|p[1];}
static uint32_t beN(const uint8_t *p,unsigned n)
{
    uint32_t v=0;unsigned i;for(i=0;i<n;i++)v=(v<<8)|p[i];return v;
}
static int memory_box(const uint8_t *p,size_t end,size_t at,box *b)
{
    if(at>end || end-at<8)return -1;
    uint64_t size=be32(p+at);size_t header=8;
    if(size==1){if(end-at<16)return -1;size=be64(p+at+8);header=16;}
    if(!size)size=end-at;
    if(size<header || size>end-at)return -1;
    b->payload=at+header;b->end=at+(size_t)size;b->type=be32(p+at+4);return 0;
}
static int child(const uint8_t *p,size_t begin,size_t end,uint32_t type,box *out)
{
    size_t at=begin;int found=0;box b;
    while(at<end){if(memory_box(p,end,at,&b))return -1;if(b.type==type){if(found)return -1;*out=b;found=1;}at=b.end;}
    return found ? 0 : -1;
}
static int top(wili_mp4_dash *r,uint64_t at,uint64_t *payload,uint64_t *end,uint32_t *type)
{
    uint8_t h[16];if(at>r->size || r->size-at<8 || r->read(r->opaque,at,h,8))return -1;
    uint64_t size=be32(h);unsigned header=8;*type=be32(h+4);
    if(size==1){if(r->size-at<16 || r->read(r->opaque,at+8,h+8,8))return -1;size=be64(h+8);header=16;}
    if(!size)size=r->size-at;
    if(size<header || size>r->size-at)return -1;
    *payload=at+header;*end=at+size;return 0;
}
static int meta(wili_mp4_dash *r,uint64_t at,uint64_t size)
{
    if(!size || size>WILI_DASH_META)return -1;
    return r->read(r->opaque,at,r->metadata,(size_t)size);
}

// --- AudioSpecificConfig / avcC ------------------------------------------------
static int asc(wili_dash_info *i,const uint8_t *p,size_t n)
{
    if(!n || n>32)return -1;
    size_t bit=0;unsigned k;size_t bits=n*8;
#define GET(N,OUT) do { if(bit+(N)>bits)return -1; (OUT)=0; for(k=0;k<(N);k++){(OUT)=((OUT)<<1)|((p[bit>>3]>>(7-(bit&7)))&1);bit++;} } while(0)
    uint32_t aot,freq,ch;GET(5,aot);
    if(aot==31){uint32_t ext;GET(6,ext);aot=32+ext;}
    GET(4,freq);
    uint32_t rate;
    static const uint32_t rates[13]={96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};
    if(freq==15){GET(24,rate);}else if(freq<13)rate=rates[freq];else return -1;
    GET(4,ch);
    if(aot==5 || aot==29)return -1; // SBR/PS needs explicit extension support
    if(aot!=2 || !rate || (ch!=1 && ch!=2) || rate>48000)return -1;
    // GASpecificConfig: frameLengthFlag / dependsOnCoreCoder / extensionFlag.
    uint32_t flag;GET(1,flag);GET(1,flag);if(flag){uint32_t delay;GET(14,delay);}
    GET(1,flag);
    i->sample_rate=rate;i->sample_rate_index=freq;i->channels=ch;i->audio_object_type=aot;
    if(i->config_size+(n>sizeof(i->config)?sizeof(i->config):n)>sizeof(i->config))return -1;
    memcpy(i->config,p,n);i->config_size=(uint32_t)n;
#undef GET
    return 0;
}
static int esds(wili_dash_info *i,const uint8_t *p,size_t n,unsigned depth)
{
    if(depth>4)return -1;
    size_t at=0;
    while(at<n){
        unsigned tag=p[at++],k;uint32_t length=0;int terminated=0;
        for(k=0;k<4 && at<n;k++){unsigned value=p[at++];length=(length<<7)|(value&127);if(!(value&128)){terminated=1;break;}}
        if(!terminated || length>n-at)return -1;
        if(tag==5){if(asc(i,p+at,length))return -1;}
        else if(tag==3){if(length<3 || p[at+2] || esds(i,p+at+3,length-3,depth+1))return -1;}
        else if(tag==4){if(length<13 || p[at]!=0x40 || esds(i,p+at+13,length-13,depth+1))return -1;}
        at+=length;
    }
    return 0;
}
static int avcc(wili_dash_info *i,const uint8_t *p,size_t n)
{
    if(n<7 || p[0]!=1)return -1;
    i->nal_length_size=(uint32_t)(p[4]&3)+1u;
    size_t at=6;unsigned sps_count=p[5]&31,pps_count;
    if(!sps_count || sps_count>4 || i->nal_length_size<1 || i->nal_length_size>4)return -1;
    const uint8_t *sps[4];uint32_t sps_size[4];unsigned k;
    for(k=0;k<sps_count;k++){
        if(n-at<2)return -1;
        uint32_t len=be16(p+at);at+=2;
        if(!len || len>256 || len>n-at)return -1;
        sps[k]=p+at;sps_size[k]=len;at+=len;
    }
    if(at>=n)return -1;
    pps_count=p[at++];
    if(!pps_count || pps_count>4)return -1;
    const uint8_t *pps[4];uint32_t pps_size[4];
    for(k=0;k<pps_count;k++){
        if(n-at<2)return -1;
        uint32_t len=be16(p+at);at+=2;
        if(!len || len>256 || len>n-at)return -1;
        pps[k]=p+at;pps_size[k]=len;at+=len;
    }
    // High profiles append 4 bytes (chroma_format, luma/chroma bit depth minus 8,
    // SPS-ext count) to avcC. Validate rather than reject: the real 512x288
    // stream has 44 bytes of parameter sets + 4 extension bytes.
    if(n-at>=4 && (p[1]==100 || p[1]==110 || p[1]==122 || p[1]==144)){
        // Reserved high bits are all ones in the encoder's avcC extension.
        if((p[at]&3)!=1 || (p[at+1]&7) || (p[at+2]&7) || p[at+3] || at+4!=n)return -1;
    }else if(at!=n)return -1;
    // Only the first SPS drives decoder configuration; all parameter sets are
    // still emitted so in-band changes are not silently discarded.
    if(wili_avc_sps_parse(sps[0],sps_size[0],&i->sps))return -1;
    if(i->sps.profile!=p[1] || i->sps.level!=p[3])return -1;
    i->coded_width=i->sps.coded_width;i->coded_height=i->sps.coded_height;
    i->display_width=i->sps.display_width;i->display_height=i->sps.display_height;
    i->level=i->sps.level;i->refs=i->sps.max_num_ref_frames;
    if(i->sps.sar_width && i->sps.sar_height){i->sar_width=i->sps.sar_width;i->sar_height=i->sps.sar_height;}
    size_t out=0;
    for(k=0;k<sps_count;k++){
        if(out+4+sps_size[k]>sizeof(i->config))return -1;
        memcpy(i->config+out,"\0\0\0\1",4);memcpy(i->config+out+4,sps[k],sps_size[k]);out+=4+sps_size[k];
    }
    for(k=0;k<pps_count;k++){
        if(out+4+pps_size[k]>sizeof(i->config))return -1;
        memcpy(i->config+out,"\0\0\0\1",4);memcpy(i->config+out+4,pps[k],pps_size[k]);out+=4+pps_size[k];
    }
    i->config_size=(uint32_t)out;return 0;
}

// --- moov / sidx ---------------------------------------------------------------
static int parse_moov(wili_mp4_dash *r,size_t size)
{
    const uint8_t *p=r->metadata;wili_dash_info *i=&r->info;
    box moov,mvhd,mvex,trex,trak,tkhd,edts,elst,mdia,mdhd,hdlr,minf,stbl,stsd;
    if(memory_box(p,size,0,&moov) || moov.type!=FCC('m','o','o','v') ||
       child(p,moov.payload,moov.end,FCC('m','v','h','d'),&mvhd) ||
       child(p,moov.payload,moov.end,FCC('m','v','e','x'),&mvex) ||
       child(p,mvex.payload,mvex.end,FCC('t','r','e','x'),&trex) ||
       child(p,moov.payload,moov.end,FCC('t','r','a','k'),&trak) ||
       child(p,trak.payload,trak.end,FCC('t','k','h','d'),&tkhd) ||
       child(p,trak.payload,trak.end,FCC('m','d','i','a'),&mdia) ||
       child(p,mdia.payload,mdia.end,FCC('m','d','h','d'),&mdhd) ||
       child(p,mdia.payload,mdia.end,FCC('h','d','l','r'),&hdlr) ||
       child(p,mdia.payload,mdia.end,FCC('m','i','n','f'),&minf) ||
       child(p,minf.payload,minf.end,FCC('s','t','b','l'),&stbl) ||
       child(p,stbl.payload,stbl.end,FCC('s','t','s','d'),&stsd))return -1;
    r->stage=30;
    if(mvhd.end-mvhd.payload<24 || mdhd.end-mdhd.payload<24 || hdlr.end-hdlr.payload<12 ||
       tkhd.end-tkhd.payload<24 || trex.end-trex.payload!=24 || stsd.end-stsd.payload<8)return -1;
    unsigned mvhd_v=p[mvhd.payload],mdhd_v=p[mdhd.payload],tkhd_v=p[tkhd.payload];
    if(mvhd_v>1 || mdhd_v>1 || tkhd_v>1)return -1;
    size_t mvhd_at=mvhd.payload+(mvhd_v ? 20 : 12),mdhd_at=mdhd.payload+(mdhd_v ? 20 : 12);
    if(mvhd.end-mvhd_at<4 || mdhd.end-mdhd_at<8)return -1;
    i->mvhd_timescale=be32(p+mvhd_at);i->timescale=be32(p+mdhd_at);
    if(!i->mvhd_timescale || !i->timescale || i->timescale>1000000000u)return -1;
    i->track_id=be32(p+tkhd.payload+(tkhd_v ? 20 : 12));
    r->stage=31;
    if(!i->track_id || be32(p+trex.payload+4)!=i->track_id)return -1;
    i->default_duration=be32(p+trex.payload+12);i->default_size=be32(p+trex.payload+16);
    i->default_flags=be32(p+trex.payload+20);
    r->stage=32;
    if(be32(p+hdlr.payload+8)!=(r->audio ? FCC('s','o','u','n') : FCC('v','i','d','e')))return -1;
    if(be32(p+stsd.payload+4)!=1)return -1;
    r->stage=33;
    box entry;if(memory_box(p,stsd.end,stsd.payload+8,&entry))return -1;
    if(r->audio){
        if(entry.type!=FCC('m','p','4','a') || entry.end-entry.payload<28)return -1;
        uint32_t rate=be32(p+entry.payload+24)>>16;
        size_t at=entry.payload+28;box b;int found=0;
        while(at<entry.end){
            if(memory_box(p,entry.end,at,&b))return -1;
            if(b.type==FCC('e','s','d','s')){
                if(found || b.end-b.payload<4 || esds(i,p+b.payload+4,b.end-b.payload-4,0) || !i->config_size)return -1;
                found=1;
            }else if(b.type==FCC('p','a','s','p')){
                if(b.end-b.payload>=8){i->sar_width=be32(p+b.payload);i->sar_height=be32(p+b.payload+4);}
            }
            at=b.end;
        }
        if(!found || i->sample_rate!=rate || i->sample_rate!=i->timescale)return -1;
    }else{
        r->stage=34;
        if((entry.type!=FCC('a','v','c','1') && entry.type!=FCC('a','v','c','3')) || entry.end-entry.payload<78)return -1;
        r->stage=35;
        size_t at=entry.payload+78;box b;int found=0;
        while(at<entry.end){
            if(memory_box(p,entry.end,at,&b))return -1;
            if(b.type==FCC('a','v','c','C')){
                r->stage=36;
                if(found || avcc(i,p+b.payload,b.end-b.payload))return -1;
                r->stage=37;
                found=1;
            }else if(b.type==FCC('p','a','s','p')){
                if(b.end-b.payload>=8){i->sar_width=be32(p+b.payload);i->sar_height=be32(p+b.payload+4);}
            }
            at=b.end;
        }
        if(!found || !i->config_size || !i->coded_width || !i->coded_height)return -1;
        if(!wili_video_geometry(i->coded_width,i->coded_height) || i->coded_width*i->coded_height*4>8*1024*1024)return -1;
    }
    // Edit list: empty edits delay presentation; media_time is the composition start.
    if(!child(p,trak.payload,trak.end,FCC('e','d','t','s'),&edts) && !child(p,edts.payload,edts.end,FCC('e','l','s','t'),&elst)){
        if(elst.end-elst.payload>=8){
            unsigned v=p[elst.payload];uint32_t count=be32(p+elst.payload+4);size_t at=elst.payload+8;
            if(v>1 || count>8)return -1;
            uint32_t k;uint64_t empty_ms=0;int started=0;
            for(k=0;k<count;k++){
                uint64_t segment;int64_t media;size_t step=v?16:8;
                if(elst.end-at<step+4)return -1;
                if(v){segment=be64(p+at);media=(int64_t)be64(p+at+8);}else {segment=be32(p+at);media=(int32_t)be32(p+at+4);}
                at+=step+4;
                if(media<0)empty_ms+=segment*1000/i->mvhd_timescale;
                else if(!started){i->start_pts=(uint64_t)media;started=1;}
            }
            i->empty_edit_ms=empty_ms;
        }
    }
    return 0;
}
static int parse_sidx(wili_mp4_dash *r,size_t size,uint64_t sidx_end)
{
    const uint8_t *p=r->metadata;wili_dash_info *i=&r->info;
    if(size<20 || size>WILI_DASH_META)return -1;
    box b;if(memory_box(p,size,0,&b) || b.type!=FCC('s','i','d','x'))return -1;
    const uint8_t *q=p+b.payload;size_t n=(size_t)(b.end-b.payload),at=0;
    unsigned version=n? p[b.payload] : 0;
    if(n<24)return -1;
    (void)be32(q+4); // reference_ID
    i->sidx_timescale=be32(q+8);
    if(!i->sidx_timescale)return -1;
    at=12;
    if(version){if(n<24)return -1;i->earliest_pts=be64(q+at);i->sidx_first_offset=be64(q+at+8);at+=16;}
    else {if(n<16)return -1;i->earliest_pts=be32(q+at);i->sidx_first_offset=be32(q+at+4);at+=8;}
    if(n-at<4)return -1;
    uint32_t count=be16(q+at+2);at+=4;
    if(!count || count>4096 || n-at<count*12u)return -1;
    uint64_t bytes=i->sidx_first_offset,ticks=0;uint32_t k;
    for(k=0;k<count;k++){
        uint32_t word=be32(q+at),duration=be32(q+at+4),sap=be32(q+at+8);at+=12;
        if(word>>31)return -1; // TID/ref-index style not used by this profile
        uint64_t length=word&0x7fffffffu;
        if(!length || length>r->size || bytes>r->size-length)return -1;
        if(sidx_end>r->size || bytes>r->size-sidx_end || i->earliest_pts>UINT64_MAX-ticks)return -1;
        r->index[k]=(wili_dash_index){sidx_end+bytes,i->earliest_pts+ticks,(uint32_t)length,duration,sap};
        bytes+=length;ticks+=duration;
    }
    i->sidx_references=count;i->sidx_total_bytes=bytes;i->sidx_duration_ticks=ticks;
    r->index_count=count;
    if(sidx_end+bytes>r->size || r->size-(sidx_end+bytes)>4096)return -1;
    return 0;
}
int wili_mp4_dash_seek(wili_mp4_dash *r,uint64_t presentation_ms,uint64_t *fragment_ms)
{
    if(!r || !r->index_count || !r->info.timescale || !r->info.sidx_timescale || presentation_ms>86400000ULL || r->info.start_pts/r->info.timescale>86400)return -1;
    uint64_t ms=presentation_ms>r->info.empty_edit_ms?presentation_ms-r->info.empty_edit_ms:0;
    uint64_t target=r->info.start_pts*r->info.sidx_timescale/r->info.timescale+ms*r->info.sidx_timescale/1000;
    unsigned selected=0;
    for(unsigned i=1;i<r->index_count && r->index[i].time<=target;i++)selected=i;
    if(r->audio && selected)selected--; // AAC filterbank warm-up, discarded by PCM timestamp trim.
    if(!r->audio){
        while(selected && !(r->index[selected].sap&0x80000000u))selected--;
        if(selected && ((r->index[selected].sap>>28)&7)>2)return -1;
    }
    r->scan=r->index[selected].offset;r->moof_base=r->scan;
    r->moof_at=r->mdat_begin=r->mdat_end=r->base_dts=r->cursor=r->expected_dts=0;
    r->sequence=r->fragments=r->total_samples=r->delivered_samples=0;
    r->trun_count=r->trun_index=r->sample_index=0;r->error=r->eof=0;
    r->rate_frames=0;r->rate_ticks=0;
    if(fragment_ms){
        uint64_t start_ms=r->info.start_pts*1000/r->info.timescale;
        uint64_t indexed_ms=r->index[selected].time*1000/r->info.sidx_timescale;
        *fragment_ms=r->info.empty_edit_ms+(indexed_ms>start_ms?indexed_ms-start_ms:0);
    }
    return 0;
}
int wili_mp4_dash_open(wili_mp4_dash *r,wili_dash_read_at read,void *opaque,uint64_t size,int audio)
{
    if(!r || !read || size<64 || size>0xffffffffULL)return -1;
    memset(r,0,sizeof(*r));r->read=read;r->opaque=opaque;r->size=size;r->audio=audio?1:0;r->info.audio=r->audio;
    uint64_t at=0,first_moof=0;int have_moov=0;
    while(at<size){
        uint64_t payload,end;uint32_t type;
        r->stage=1;
        if(top(r,at,&payload,&end,&type))return -1;
        if(type==FCC('m','o','o','v')){
            r->stage=2;
            if(meta(r,at,end-at))return -1;
            r->stage=3;
            if(parse_moov(r,(size_t)(end-at)))return -1;
            have_moov=1;
        }else if(type==FCC('s','i','d','x')){
            r->stage=4;
            if(meta(r,at,end-at))return -1;
            r->stage=5;
            if(parse_sidx(r,(size_t)(end-at),end))return -1;
        }else if(type==FCC('m','o','o','f')){first_moof=at;break;}
        else if(type!=FCC('f','t','y','p') && type!=FCC('f','r','e','e') && type!=FCC('s','k','i','p') && type!=FCC('u','d','t','a'))return -1;
        at=end;
    }
    r->stage=6;
    if(!have_moov || !first_moof || !r->info.config_size || !r->info.sidx_total_bytes)return -1;
    r->stage=7;
    if(!r->audio && (!r->info.timescale || !r->info.coded_width))return -1;
    if(r->audio && r->info.sample_rate!=r->info.timescale)return -1;
    r->stage=8;
    r->scan=first_moof;r->moof_base=first_moof;return 0;
}

// --- fragments -----------------------------------------------------------------
static unsigned stride(uint32_t flags)
{
    unsigned n=0;
    if(flags&0x100)n+=4;
    if(flags&0x200)n+=4;
    if(flags&0x400)n+=4;
    if(flags&0x800)n+=4;
    return n;
}
static int parse_fragment(wili_mp4_dash *r,uint64_t moof_at,size_t moof_size,uint64_t data_begin,uint64_t data_end)
{
    const uint8_t *p=r->metadata;box moof,mfhd,traf,tfhd,tfdt,b;
    if(memory_box(p,moof_size,0,&moof) || moof.type!=FCC('m','o','o','f') ||
       child(p,moof.payload,moof.end,FCC('m','f','h','d'),&mfhd) ||
       child(p,moof.payload,moof.end,FCC('t','r','a','f'),&traf) ||
       child(p,traf.payload,traf.end,FCC('t','f','h','d'),&tfhd) ||
       child(p,traf.payload,traf.end,FCC('t','f','d','t'),&tfdt))return -1;
    if(mfhd.end-mfhd.payload<8 || tfhd.end-tfhd.payload<8 || tfdt.end-tfdt.payload<8)return -1;
    uint32_t seq=be32(p+mfhd.payload+4),tf_flags=be32(p+tfhd.payload)&0xffffff;
    if(!seq || (r->fragments && seq<=r->sequence) || p[tfhd.payload] ||
       be32(p+tfhd.payload+4)!=r->info.track_id || (tf_flags&~0x3003bu))return -1;
    size_t at=tfhd.payload+8;uint64_t base=0;int have_base=0;
    if(tf_flags&1){if(tfhd.end-at<8)return -1;base=be64(p+at);at+=8;have_base=1;}
    else if(tf_flags&0x20000){base=moof_at;have_base=1;}
    if(!have_base)return -1;
    if(tf_flags&2){if(tfhd.end-at<4)return -1;at+=4;}
    uint32_t duration=r->info.default_duration,bytes=r->info.default_size,def_flags=r->info.default_flags;
    if(tf_flags&8){if(tfhd.end-at<4)return -1;duration=be32(p+at);at+=4;}
    if(tf_flags&16){if(tfhd.end-at<4)return -1;bytes=be32(p+at);at+=4;}
    if(tf_flags&32){if(tfhd.end-at<4)return -1;def_flags=be32(p+at);at+=4;}
    unsigned tfdt_v=p[tfdt.payload];if(tfdt_v>1 || (be32(p+tfdt.payload)&0xffffff))return -1;
    uint64_t dts=tfdt_v ? be64(p+tfdt.payload+4) : be32(p+tfdt.payload+4);
    if(r->fragments && dts!=r->expected_dts)return -1;
    r->moof_at=moof_at;r->moof_base=base;r->mdat_begin=data_begin;r->mdat_end=data_end;
    r->base_dts=dts;r->cursor=data_begin;r->trun_count=0;r->trun_index=0;r->sample_index=0;
    for(at=traf.payload;at<traf.end;at=b.end){
        if(memory_box(p,traf.end,at,&b))return -1;
        if(b.type!=FCC('t','r','u','n'))continue;
        if(r->trun_count>=WILI_DASH_MAX_TRUNS || b.end-b.payload<8)return -1;
        unsigned version=p[b.payload];uint32_t flags=be32(p+b.payload)&0xffffff,count=be32(p+b.payload+4);
        if(version>1 || !count || count>8192 || (flags&~0xf05u))return -1;
        size_t pos=b.payload+8;wili_dash_trun t;memset(&t,0,sizeof(t));
        t.version=(int)version;t.flags=flags;t.count=count;t.fields=0;
        if(flags&1){if(b.end-pos<4)return -1;t.data_offset=(int32_t)be32(p+pos);pos+=4;t.has_data_offset=1;}
        if(flags&4){if(b.end-pos<4)return -1;t.first_flags=be32(p+pos);pos+=4;}
        t.fields=pos;
        uint64_t need=(uint64_t)count*stride(flags);
        if(need>b.end-pos || pos+need>b.end)return -1;
        r->truns[r->trun_count++]=t;
        if((flags&0x100)==0 && !duration)return -1;
        if((flags&0x200)==0 && !bytes)return -1;
    }
    if(!r->trun_count)return -1;
    r->frag_duration=duration;r->frag_size=bytes;r->frag_flags=def_flags;
    r->sequence=seq;r->fragments++;
    return 0;
}
static int next_fragment(wili_mp4_dash *r)
{
    uint64_t at=r->scan,payload,end;uint32_t type;
    while(at<r->size){
        if(top(r,at,&payload,&end,&type))return -1;
        if(type==FCC('m','o','o','f')){
            uint64_t moof_end=end;
            if(end-at>WILI_DASH_META || meta(r,at,end-at))return -1;
            uint64_t next=end,data_begin=0,data_end=0;
            while(next<r->size){
                if(top(r,next,&payload,&end,&type))return -1;
                if(type==FCC('m','d','a','t')){data_begin=payload;data_end=end;break;}
                if(type!=FCC('f','r','e','e') && type!=FCC('s','k','i','p'))return -1;
                next=end;
            }
            if(!data_end || data_end>data_begin+0xffffffffULL ||
               parse_fragment(r,at,(size_t)(moof_end-at),data_begin,data_end))return -1;
            r->scan=data_end;return 1;
        }
        if(type!=FCC('f','r','e','e') && type!=FCC('s','k','i','p') && type!=FCC('s','i','d','x'))return -1;
        at=end;
    }
    r->eof=1;return 0;
}
static int next_sample(wili_mp4_dash *r,wili_dash_sample *s)
{
    for(;;){
        if(r->trun_index>=r->trun_count){
            r->expected_dts=r->base_dts;
            int rc=next_fragment(r);if(rc<=0){if(rc<0)r->error=1;return rc;}
        }
        wili_dash_trun *t=&r->truns[r->trun_index];
        if(r->sample_index>=t->count){r->trun_index++;r->sample_index=0;continue;}
        size_t pos=t->fields+(size_t)r->sample_index*stride(t->flags);
        uint32_t duration=(t->flags&0x100) ? be32(r->metadata+pos) : r->frag_duration;
        if(t->flags&0x100)pos+=4;
        uint32_t size=(t->flags&0x200) ? be32(r->metadata+pos) : r->frag_size;
        if(t->flags&0x200)pos+=4;
        uint32_t flags=(t->flags&0x400) ? be32(r->metadata+pos) : r->frag_flags;
        if(t->flags&0x400)pos+=4;
        if(r->sample_index==0 && (t->flags&4))flags=t->first_flags;
        int64_t cto=0;
        if(t->flags&0x800){
            uint32_t raw=be32(r->metadata+pos);
            cto=t->version ? (int64_t)(int32_t)raw : (int64_t)raw;
        }
        if(!duration || !size || size>WILI_DASH_PACKET_CAP-64)goto bad;
        if(!r->audio && r->max_fps){
            // Allow one track tick of quantization (e.g. 533/534 at 16000Hz),
            // enforce an explicitly requested legacy ceiling across 30-frame windows.
            r->rate_ticks+=duration;r->rate_frames++;
            if(((uint64_t)duration+1)*r->max_fps<r->info.timescale ||
               (r->rate_frames>=30 && (uint64_t)r->rate_frames*r->info.timescale>
                (r->rate_ticks+1)*r->max_fps)){r->stage=81;goto bad;}
            if(r->rate_frames>=30){r->rate_frames=0;r->rate_ticks=0;}
        }
        uint64_t offset;
        if(r->sample_index==0 && t->has_data_offset){
            int64_t signed_offset=t->data_offset;
            if(signed_offset<0 && (uint64_t)(-signed_offset)>r->moof_base)goto bad;
            offset=signed_offset<0 ? r->moof_base-(uint64_t)(-signed_offset) : r->moof_base+(uint64_t)signed_offset;
        }else offset=r->cursor;
        if(offset<r->mdat_begin || offset>r->mdat_end || size>r->mdat_end-offset)goto bad;
        uint64_t dts=r->base_dts,pts;
        if(cto<0){if((uint64_t)(-cto)>dts)goto bad;pts=dts-(uint64_t)(-cto);}
        else {if((uint64_t)cto>UINT64_MAX-dts)goto bad;pts=dts+(uint64_t)cto;}
        if(dts>UINT64_MAX-duration)goto bad;
        s->offset=offset;s->dts=dts;s->pts=pts;s->size=size;s->duration=duration;s->flags=flags;
        if(cto>0 && (uint64_t)cto>r->info.max_cto_ticks)r->info.max_cto_ticks=(uint32_t)cto;
        r->base_dts=dts+duration;r->cursor=offset+size;r->sample_index++;r->total_samples++;r->delivered_samples++;
        return 1;
    }
bad:r->error=1;return -1;
}

int wili_mp4_dash_probe_rate(wili_mp4_dash *r,unsigned limit,unsigned *num,unsigned *den)
{
    if(!r || r->audio || !num || !den || !r->info.timescale)return -1;
    wili_mp4_dash *probe=malloc(sizeof(*probe));if(!probe)return -1;
    memcpy(probe,r,sizeof(*probe));probe->max_fps=limit;probe->rate_frames=0;probe->rate_ticks=0;
    unsigned count=0;uint64_t ticks=0;int rc=0;wili_dash_sample sample;
    while(count<60 && (rc=next_sample(probe,&sample))>0){count++;ticks+=sample.duration;}
    free(probe);
    if(rc<0 || !count || !ticks || ticks>UINT32_MAX ||
       (limit && (uint64_t)count*r->info.timescale>(ticks+1)*limit))return -1;
    // Reduce before converting the numerator to 32 bits.
    uint64_t n=(uint64_t)count*r->info.timescale,d=ticks,a=n,b=d;
    while(b){uint64_t t=a%b;a=b;b=t;}n/=a;d/=a;
    if(n>UINT32_MAX || d>UINT32_MAX)return -1;
    *num=(unsigned)n;*den=(unsigned)d;r->measured_fps_num=*num;r->measured_fps_den=*den;
    r->max_fps=limit;r->rate_frames=0;r->rate_ticks=0;return 0;
}

// --- AU conversion -------------------------------------------------------------
static int video_packet(wili_mp4_dash *r,uint8_t *out,size_t cap,size_t *out_bytes,wili_dash_sample *sample)
{
    wili_dash_sample s;int rc=next_sample(r,&s);if(rc<=0)return rc;
    if(s.size>WILI_DASH_PACKET_CAP || r->read(r->opaque,s.offset,r->scratch,s.size)){r->error=1;return -1;}
    nal_desc nals[WILI_DASH_MAX_NALS];unsigned count=0,at=0,vcl=0,idr=0,have_aud=0,aud_index=0,have_sps=0,have_pps=0;
    unsigned nal_len=r->info.nal_length_size;
    while(at<s.size){
        if(count>=WILI_DASH_MAX_NALS || s.size-at<nal_len)goto bad;
        uint32_t n=beN(r->scratch+at,nal_len);at+=nal_len;
        if(!n || n>s.size-at || (r->scratch[at]&0x80))goto bad;
        unsigned type=r->scratch[at]&31;
        if(type==9){if(have_aud)goto bad;have_aud=1;aud_index=count;}
        else if(type==7)have_sps=1;
        else if(type==8)have_pps=1;
        else if(type==1 || type==5){vcl++;if(type==5)idr=1;}
        else if(type!=6 && type!=10 && type!=11 && type!=12)goto bad;
        nals[count++]=(nal_desc){at,n,type};at+=n;
    }
    if(!vcl || at!=s.size)goto bad;
    int sync=!(s.flags&0x10000u) || idr;
    if(r->delivered_samples==1 && !sync)goto bad;
    size_t total=6+r->info.config_size+(size_t)s.size+count*4u+64u;
    if(total>cap)goto bad;
    size_t o=0;
    if(have_aud){memcpy(out,"\0\0\0\1",4);memcpy(out+4,r->scratch+nals[aud_index].at,nals[aud_index].size);o=4+nals[aud_index].size;}
    else {memcpy(out,"\0\0\0\1\x09\xf0",6);o=6;}
    int inject=sync && !(have_sps && have_pps);
    if(inject){memcpy(out+o,r->info.config,r->info.config_size);o+=r->info.config_size;}
    unsigned k;
    for(k=0;k<count;k++){
        if(have_aud && k==aud_index)continue;
        if(inject && (nals[k].type==7 || nals[k].type==8))continue;
        memcpy(out+o,"\0\0\0\1",4);memcpy(out+o+4,r->scratch+nals[k].at,nals[k].size);o+=4+nals[k].size;
    }
    if(o>cap)goto bad;
    if(r->decoder_level && wili_avc_normalize_annexb(out,o,&r->info.sps,r->decoder_level))goto bad;
    *out_bytes=o;*sample=s;return 1;
bad:r->error=1;return -1;
}

int wili_mp4_dash_prepare_ps3(wili_mp4_dash *r,unsigned *level)
{
    if(!r || r->audio || wili_avc_ps3_level(&r->info.sps,level))return -1;
    int is720=wili_video_is_720(r->info.coded_width,r->info.coded_height);
    if(!is720 && r->max_fps!=30)return -1;
    if(is720 && r->info.sps.level>42 && r->measured_fps_num && r->measured_fps_den){
        uint64_t mbps=(uint64_t)(r->info.coded_width/16)*(r->info.coded_height/16)*r->measured_fps_num;
        if(mbps>522240ULL*r->measured_fps_den)*level=r->info.sps.level;
        else if(mbps>245760ULL*r->measured_fps_den)*level=42;
        // Above Level 4.2 throughput retain the source level: do not make a
        // false SPS declaration to force firmware to accept an extreme stream.
    }
    if(wili_avc_normalize_annexb(r->info.config,r->info.config_size,&r->info.sps,*level))return -1;
    r->decoder_level=*level;return 0;
}
static int audio_packet(wili_mp4_dash *r,uint8_t *out,size_t cap,size_t *out_bytes,wili_dash_sample *sample)
{
    wili_dash_sample s;int rc=next_sample(r,&s);if(rc<=0)return rc;
    if(cap<(size_t)s.size+7 || r->read(r->opaque,s.offset,out+7,s.size)){r->error=1;return -1;}
    uint32_t length=(uint32_t)s.size+7u;
    unsigned freq=r->info.sample_rate_index,ch=r->info.channels;
    out[0]=0xff;out[1]=0xf1;
    out[2]=(uint8_t)(((r->info.audio_object_type-1)<<6)|(freq<<2)|((ch>>2)&1));
    out[3]=(uint8_t)(((ch&3)<<6)|((length>>11)&3));
    out[4]=(uint8_t)(length>>3);
    out[5]=(uint8_t)(((length&7)<<5)|31);
    out[6]=0xfc;*out_bytes=length;*sample=s;return 1;
}
int wili_mp4_dash_packet(wili_mp4_dash *r,uint8_t *out,size_t cap,size_t *bytes,wili_dash_sample *sample)
{
    if(!r || !out || !bytes || !sample || r->error)return -1;
    if(r->eof)return 0;
    return r->audio ? audio_packet(r,out,cap,bytes,sample) : video_packet(r,out,cap,bytes,sample);
}
const wili_dash_info *wili_mp4_dash_info(const wili_mp4_dash *r){return r ? &r->info : 0;}
