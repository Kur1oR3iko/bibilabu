// SPDX-License-Identifier: GPL-3.0-or-later
#define _FILE_OFFSET_BITS 64
#include "fmp4_stream.h"
#include "aac_test_stream.h"
#include "avc_test_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {FILE *file;uint64_t size;size_t max_read;} host_file;
static int file_read(void *arg,uint64_t at,void *out,size_t n)
{
    host_file *f=arg;
    if(at>f->size || n>f->size-at || fseeko(f->file,(off_t)at,SEEK_SET))return -1;
    if(n>f->max_read)f->max_read=n;
    return fread(out,1,n,f->file)==n ? 0 : -1;
}
typedef struct {uint8_t *data;size_t size;} memory_file;
static int memory_read(void *arg,uint64_t at,void *out,size_t n)
{
    memory_file *m=arg;if(at>m->size || n>m->size-at)return -1;
    memcpy(out,m->data+(size_t)at,n);return 0;
}
static void run(const char *name,int audio,const char *output)
{
    host_file f={fopen(name,"rb"),0,0};assert(f.file);
    fseeko(f.file,0,SEEK_END);f.size=(uint64_t)ftello(f.file);
    wili_fmp4 *r=malloc(sizeof(*r));uint8_t *packet=malloc(256*1024);assert(r && packet);
    assert(!wili_fmp4_open(r,file_read,&f,f.size,audio));
    assert(r->timescale==(audio ? 48000 : 15360));
    if(!audio)assert(r->width==848 && r->height==480 && r->level==31 && r->refs==1);
    FILE *out=fopen(output,"wb");assert(out);unsigned count=0;uint64_t end=0;
    for(;;){
        size_t bytes=0;wili_mp4_sample s;int rc=wili_fmp4_packet(r,packet,256*1024,&bytes,&s);
        assert(rc>=0);if(!rc)break;
        assert(s.dts==end && s.pts==s.dts && bytes>0);end+=s.duration;
        if(audio){wili_aac_stream adts;assert(!wili_parse_test_aac(packet,bytes,&adts));assert(adts.count==1);}
        else {
            assert(s.duration==512);
            assert(bytes>=6 && !memcmp(packet,"\0\0\0\1\x09",5));
            if(count==0){assert((packet[10]&31)==7);}
        }
        assert(fwrite(packet,1,bytes,out)==bytes);count++;
        assert(r->count<=WILI_MP4_SAMPLES);
    }
    fclose(out);
    assert(audio ? count>=1407 && count<=1410 : count==900);
    assert(r->fragments>=29 && f.max_read<=256*1024);
    printf("PASS %s: samples=%u fragments=%u timescale=%u max_read=%zu parser_bytes=%zu\n",audio ? "AAC" : "AVC",count,r->fragments,r->timescale,f.max_read,sizeof(*r));
    // Structured mutation/truncation checks exercise the real init + fragment code.
    uint8_t *data=malloc((size_t)f.size),*copy=malloc((size_t)f.size);assert(data && copy);
    assert(!file_read(&f,0,data,(size_t)f.size));memory_file m={copy,(size_t)f.size};
    uint32_t crc=~0u;size_t piece;
    for(piece=0;piece<f.size;){size_t n=f.size-piece>257 ? 257 : (size_t)(f.size-piece);crc=wili_crc32_update(crc,data+piece,n);piece+=n;}
    assert(~crc==wili_test_crc32(data,(size_t)f.size));
    assert(wili_test_crc32((const uint8_t*)"123456789",9)==0xcbf43926u);
    unsigned i;
    for(i=0;i<2048 && i<f.size;i++){
        memcpy(copy,data,(size_t)f.size);copy[i]^=255;
        if(!wili_fmp4_open(r,memory_read,&m,m.size,audio)){
            unsigned j;for(j=0;j<3;j++){size_t bytes;wili_mp4_sample s;if(wili_fmp4_packet(r,packet,256*1024,&bytes,&s)<=0)break;}
        }
    }
    memcpy(copy,data,(size_t)f.size);
    for(i=0;i<64;i++){m.size=i;assert(wili_fmp4_open(r,memory_read,&m,m.size,audio)<0);}
    m.size=(size_t)f.size;
    // Oversized first box length cannot escape file bounds.
    memset(copy,255,4);assert(wili_fmp4_open(r,memory_read,&m,m.size,audio)<0);
    free(data);free(copy);free(packet);free(r);fclose(f.file);
}
int main(int argc,char **argv)
{
    assert(argc==5);run(argv[1],0,argv[3]);run(argv[2],1,argv[4]);
    puts("PASS fragmented MP4 conversion, timestamp continuity, bounded fragment indexes and structured mutation cases");return 0;
}
