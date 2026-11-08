// SPDX-License-Identifier: GPL-3.0-or-later
#include "fmp4_stream.h"
#include "avc_test_stream.h"
#include <string.h>
#include <limits.h>
#define FCC(a,b,c,d) ((uint32_t)(a)<<24|(uint32_t)(b)<<16|(uint32_t)(c)<<8|(uint32_t)(d))
typedef struct {size_t payload,end;uint32_t type;} box;
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t be64(const uint8_t *p){return (uint64_t)be32(p)<<32|be32(p+4);}
static unsigned be16(const uint8_t *p){return (unsigned)p[0]<<8|p[1];}
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
static int top(wili_fmp4 *r,uint64_t at,uint64_t *payload,uint64_t *end,uint32_t *type)
{
    uint8_t h[16];if(at>r->size || r->size-at<8 || r->read(r->opaque,at,h,8))return -1;
    uint64_t size=be32(h);unsigned header=8;*type=be32(h+4);
    if(size==1){if(r->size-at<16 || r->read(r->opaque,at+8,h+8,8))return -1;size=be64(h+8);header=16;}
    if(!size)size=r->size-at;
    if(size<header || size>r->size-at)return -1;
    *payload=at+header;*end=at+size;return 0;
}
static int avcc(wili_fmp4 *r,const uint8_t *p,size_t n)
{
    if(n<11 || p[0]!=1 || p[1]!=66 || (p[4]&3)!=3 || (p[5]&31)!=1)return -1;
    size_t at=6;unsigned length=be16(p+at);at+=2;
    if(length>n-at || length+4>WILI_MP4_CONFIG)return -1;
    wili_test_stream s;
    if(wili_parse_avc_sps(p+at,length,&s) || s.profile!=p[1] || s.level!=p[3])return -1;
    memcpy(r->config,"\0\0\0\1",4);memcpy(r->config+4,p+at,length);r->config_size=length+4;r->sps_size=length;at+=length;
    if(at>=n || p[at++]!=1 || n-at<2)return -1;
    length=be16(p+at);at+=2;
    if(!length || length>n-at || length+4>WILI_MP4_CONFIG-r->config_size || (p[at]&0x9f)!=8)return -1;
    memcpy(r->config+r->config_size,"\0\0\0\1",4);memcpy(r->config+r->config_size+4,p+at,length);r->config_size+=length+4;
    r->width=s.coded_width;r->height=s.coded_height;r->level=s.level;r->refs=s.reference_frames;
    if(s.display_width!=s.coded_width || s.display_height!=s.coded_height)return -1;
    return 0;
}
static int esds(wili_fmp4 *r,const uint8_t *p,size_t n,unsigned depth)
{
    if(depth>4)return -1;
    size_t at=0;
    while(at<n){
        unsigned tag=p[at++],i;uint32_t length=0;int terminated=0;
        for(i=0;i<4 && at<n;i++){unsigned value=p[at++];length=(length<<7)|(value&127);if(!(value&128)){terminated=1;break;}}
        if(!terminated || length>n-at)return -1;
        if(tag==5){
            if(length<2 || p[at]>>3!=2 || (((p[at]&7)<<1)|(p[at+1]>>7))!=3 || ((p[at+1]>>3)&15)!=2 || (p[at+1]&7))return -1;
            r->config_size=2;r->config[0]=p[at];r->config[1]=p[at+1];
        }else if(tag==3){
            if(length<3 || p[at+2] || esds(r,p+at+3,length-3,depth+1))return -1;
        }else if(tag==4){
            if(length<13 || p[at]!=0x40 || esds(r,p+at+13,length-13,depth+1))return -1;
        }
        at+=length;
    }
    return 0;
}
static int parse_moov(wili_fmp4 *r,size_t size)
{
    const uint8_t *p=r->metadata;box moov,trak,tkhd,mdia,mdhd,hdlr,minf,stbl,stsd,entry,conf,mvex,trex;
    if(memory_box(p,size,0,&moov) || moov.type!=FCC('m','o','o','v') ||
       child(p,moov.payload,moov.end,FCC('t','r','a','k'),&trak) ||
       child(p,trak.payload,trak.end,FCC('t','k','h','d'),&tkhd) ||
       child(p,trak.payload,trak.end,FCC('m','d','i','a'),&mdia) ||
       child(p,mdia.payload,mdia.end,FCC('m','d','h','d'),&mdhd) ||
       child(p,mdia.payload,mdia.end,FCC('h','d','l','r'),&hdlr) ||
       child(p,mdia.payload,mdia.end,FCC('m','i','n','f'),&minf) ||
       child(p,minf.payload,minf.end,FCC('s','t','b','l'),&stbl) ||
       child(p,stbl.payload,stbl.end,FCC('s','t','s','d'),&stsd) ||
       child(p,moov.payload,moov.end,FCC('m','v','e','x'),&mvex) ||
       child(p,mvex.payload,mvex.end,FCC('t','r','e','x'),&trex))return -1;
    if(tkhd.end-tkhd.payload<24 || mdhd.end-mdhd.payload<24 || hdlr.end-hdlr.payload<12 ||
       p[tkhd.payload]>1 || p[mdhd.payload]>1)return -1;
    size_t track_at=tkhd.payload+(p[tkhd.payload] ? 20 : 12),scale_at=mdhd.payload+(p[mdhd.payload] ? 20 : 12);
    if(tkhd.end-track_at<4 || mdhd.end-scale_at<4)return -1;
    r->track=be32(p+track_at);r->timescale=be32(p+scale_at);
    if(!r->track || !r->timescale || r->timescale>1000000000u || be32(p+hdlr.payload+8)!=(r->audio ? FCC('s','o','u','n') : FCC('v','i','d','e')))return -1;
    if(stsd.end-stsd.payload<8 || be32(p+stsd.payload+4)!=1 || memory_box(p,stsd.end,stsd.payload+8,&entry))return -1;
    if(r->audio){
        if(entry.type!=FCC('m','p','4','a') || entry.end-entry.payload<28 || be16(p+entry.payload+8) ||
           be16(p+entry.payload+16)!=2 || be32(p+entry.payload+24)!=(48000u<<16) ||
           child(p,entry.payload+28,entry.end,FCC('e','s','d','s'),&conf) || conf.end-conf.payload<4 ||
           esds(r,p+conf.payload+4,conf.end-conf.payload-4,0) || r->config_size!=2)return -1;
    }else if(entry.type!=FCC('a','v','c','1') || entry.end-entry.payload<78 ||
             child(p,entry.payload+78,entry.end,FCC('a','v','c','C'),&conf) || avcc(r,p+conf.payload,conf.end-conf.payload))return -1;
    if(trex.end-trex.payload!=24 || be32(p+trex.payload+4)!=r->track)return -1;
    r->default_duration=be32(p+trex.payload+12);r->default_size=be32(p+trex.payload+16);r->default_flags=be32(p+trex.payload+20);
    return 0;
}
int wili_fmp4_open(wili_fmp4 *r,wili_read_at read,void *opaque,uint64_t size,int audio)
{
    if(!r || !read || size<16)return -1;
    memset(r,0,sizeof(*r));r->read=read;r->opaque=opaque;r->size=size;r->audio=audio ? 1 : 0;
    uint64_t at=0,payload,end;uint32_t type;
    while(at<size){
        if(top(r,at,&payload,&end,&type))return -1;
        if(type==FCC('m','o','o','v')){
            if(end-at>WILI_MP4_META || r->read(opaque,at,r->metadata,(size_t)(end-at)) || parse_moov(r,(size_t)(end-at)))return -1;
            r->scan=end;return 0;
        }
        if(type==FCC('m','o','o','f') || type==FCC('m','d','a','t'))return -1;
        at=end;
    }
    return -1;
}
static int add_signed(uint64_t base,int32_t delta,uint64_t *out)
{
    if(delta<0){uint64_t minus=(uint64_t)(-(int64_t)delta);if(minus>base)return -1;*out=base-minus;}
    else {if((uint64_t)delta>UINT64_MAX-base)return -1;*out=base+(uint32_t)delta;}
    return 0;
}
static int parse_fragment(wili_fmp4 *r,size_t size,uint64_t moof_at,uint64_t data_begin,uint64_t data_end)
{
    const uint8_t *p=r->metadata;box moof,mfhd,traf,tfhd,tfdt,b;
    if(memory_box(p,size,0,&moof) || child(p,moof.payload,moof.end,FCC('m','f','h','d'),&mfhd) ||
       child(p,moof.payload,moof.end,FCC('t','r','a','f'),&traf) ||
       child(p,traf.payload,traf.end,FCC('t','f','h','d'),&tfhd) ||
       child(p,traf.payload,traf.end,FCC('t','f','d','t'),&tfdt))return -1;
    if(mfhd.end-mfhd.payload<8 || tfhd.end-tfhd.payload<8 || tfdt.end-tfdt.payload<8)return -1;
    uint32_t sequence=be32(p+mfhd.payload+4),flags=be32(p+tfhd.payload)&0xffffff;
    if(!sequence || sequence<=r->sequence || be32(p+mfhd.payload) || p[tfhd.payload] || be32(p+tfhd.payload+4)!=r->track || (flags&~0x2003bu))return -1;
    size_t at=tfhd.payload+8;uint64_t base=moof_at;
    if(flags&1){if(tfhd.end-at<8)return -1;base=be64(p+at);at+=8;}
    else if(!(flags&0x20000))return -1;
    if(flags&2){if(tfhd.end-at<4)return -1;at+=4;}
    uint32_t duration=r->default_duration,bytes=r->default_size,default_flags=r->default_flags;
    if(flags&8){if(tfhd.end-at<4)return -1;duration=be32(p+at);at+=4;}
    if(flags&16){if(tfhd.end-at<4)return -1;bytes=be32(p+at);at+=4;}
    if(flags&32){if(tfhd.end-at<4)return -1;default_flags=be32(p+at);at+=4;}
    unsigned version=p[tfdt.payload];if(version>1 || (be32(p+tfdt.payload)&0xffffff) || (version && tfdt.end-tfdt.payload<12))return -1;
    uint64_t dts=version ? be64(p+tfdt.payload+4) : be32(p+tfdt.payload+4);
    if(r->fragments && dts!=r->expected_dts)return -1;
    uint64_t next_data=base;int has_run=0;r->count=0;r->cursor=0;
    for(at=traf.payload;at<traf.end;at=b.end){
        if(memory_box(p,traf.end,at,&b))return -1;
        if(b.type!=FCC('t','r','u','n'))continue;
        if(b.end-b.payload<8 || p[b.payload]>1)return -1;
        uint32_t f=be32(p+b.payload)&0xffffff,count=be32(p+b.payload+4);
        if(!count || count>WILI_MP4_SAMPLES-r->count || (f&~0xf05u) || ((f&4) && (f&0x400)))return -1;
        size_t pos=b.payload+8;uint64_t cursor=next_data;uint32_t first_flags=default_flags;
        if(f&1){if(b.end-pos<4 || add_signed(base,(int32_t)be32(p+pos),&cursor))return -1;pos+=4;}
        else if(!has_run && !(flags&1))return -1;
        if(f&4){if(b.end-pos<4)return -1;first_flags=be32(p+pos);pos+=4;}
        unsigned i;
        for(i=0;i<count;i++){
            uint32_t dur=duration,n=bytes,sample_flags=(i==0 ? first_flags : default_flags);int64_t cto=0;
            if(f&0x100){if(b.end-pos<4)return -1;dur=be32(p+pos);pos+=4;}
            if(f&0x200){if(b.end-pos<4)return -1;n=be32(p+pos);pos+=4;}
            if(f&0x400){if(b.end-pos<4)return -1;sample_flags=be32(p+pos);pos+=4;}
            if(f&0x800){if(b.end-pos<4)return -1;uint32_t raw=be32(p+pos);cto=p[b.payload] ? (int64_t)(int32_t)raw : (int64_t)raw;pos+=4;}
            if(!dur || !n || cursor<data_begin || cursor>data_end || n>data_end-cursor || dur>UINT64_MAX-dts)return -1;
            uint64_t pts;
            if(cto<0){if((uint64_t)-cto>dts)return -1;pts=dts-(uint64_t)-cto;}
            else {if((uint64_t)cto>UINT64_MAX-dts)return -1;pts=dts+(uint64_t)cto;}
            r->samples[r->count++]=(wili_mp4_sample){cursor,dts,pts,n,dur,sample_flags};cursor+=n;dts+=dur;
        }
        if(pos!=b.end)return -1;
        next_data=cursor;has_run=1;
    }
    if(!has_run)return -1;
    r->expected_dts=dts;r->sequence=sequence;r->fragments++;return 0;
}
static int next_fragment(wili_fmp4 *r)
{
    uint64_t at=r->scan,payload,end;uint32_t type;
    while(at<r->size){
        if(top(r,at,&payload,&end,&type))return -1;
        if(type==FCC('m','o','o','f')){
            uint64_t moof_end=end;
            if(end-at>WILI_MP4_META || r->read(r->opaque,at,r->metadata,(size_t)(end-at)))return -1;
            uint64_t next=end,data_begin=0,data_end=0;
            while(next<r->size){
                if(top(r,next,&payload,&end,&type))return -1;
                if(type==FCC('m','d','a','t')){data_begin=payload;data_end=end;break;}
                if(type!=FCC('f','r','e','e') && type!=FCC('s','k','i','p'))return -1;
                next=end;
            }
            if(!data_end || parse_fragment(r,(size_t)(moof_end-at),at,data_begin,data_end))return -1;
            r->scan=data_end;return 1;
        }
        if(type==FCC('m','d','a','t'))return -1;
        at=end;
    }
    r->eof=1;return 0;
}
int wili_fmp4_packet(wili_fmp4 *r,uint8_t *out,size_t cap,size_t *size,wili_mp4_sample *sample)
{
    if(!r || !out || !size || !sample || r->error)return -1;
    if(r->cursor==r->count){int rc=next_fragment(r);if(rc<=0){if(rc<0)r->error=1;return rc;}}
    wili_mp4_sample s=r->samples[r->cursor];
    size_t reserve=r->audio ? 7 : r->config_size+64;
    if(cap<reserve || s.size>cap-reserve || (r->audio && s.size>8184))goto bad;
    uint8_t *raw=out+reserve;if(r->read(r->opaque,s.offset,raw,s.size))goto bad;
    if(r->audio){
        unsigned length=s.size+7;
        out[0]=255;out[1]=0xf1;out[2]=0x4c;out[3]=(uint8_t)(0x80|(length>>11));
        out[4]=(uint8_t)(length>>3);out[5]=(uint8_t)((length<<5)|31);out[6]=0xfc;*size=length;
    }else{
        size_t at=0,aud_at=0,aud_size=0;int idr=0;unsigned slices=0;
        uint8_t aud[64]={0,0,0,1,9,0xf0};
        while(at<s.size){
            if(s.size-at<5)goto bad;
            uint32_t n=be32(raw+at);if(!n || n>s.size-at-4 || (raw[at+4]&0x80))goto bad;
            unsigned type=raw[at+4]&31;if(type==5)idr=1;
            if(type==1 || type==5)slices++;
            else if(type!=6 && type!=7 && type!=8 && type!=9)goto bad;
            if(type==7 && (n!=r->sps_size || memcmp(raw+at+4,r->config+4,n)))goto bad;
            if(type==8 && (n!=r->config_size-r->sps_size-8 || memcmp(raw+at+4,r->config+r->sps_size+8,n)))goto bad;
            if(type==9){if(aud_size || n+4>sizeof(aud))goto bad;aud_at=at;aud_size=n+4;}
            memcpy(raw+at,"\0\0\0\1",4);at+=n+4;
        }
        if(slices!=1 || (!r->total_samples && !idr))goto bad;
        size_t raw_size=s.size;
        if(aud_size){
            memcpy(aud,raw+aud_at,aud_size);
            memmove(raw+aud_at,raw+aud_at+aud_size,raw_size-aud_at-aud_size);
            raw_size-=aud_size;
        }else aud_size=6;
        // Some MP4 encoders place prefix SEI before AUD. The native decoder
        // takes explicit access units: publish AUD first, then parameter sets,
        // then SEI/VCL. A permissive host decoder alone does not prove this ABI.
        size_t prefix=aud_size+(idr ? r->config_size : 0);
        memmove(out+prefix,raw,raw_size);memcpy(out,aud,aud_size);
        if(idr)memcpy(out+aud_size,r->config,r->config_size);
        *size=prefix+raw_size;
    }
    *sample=s;r->cursor++;r->total_samples++;return 1;
bad:
    r->error=1;return -1;
}
