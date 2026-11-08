// SPDX-License-Identifier: GPL-3.0-or-later
#include "mp4_dash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static int read_at(void *arg,uint64_t at,void *out,size_t size)
{
    FILE *f=arg;return fseek(f,(long)at,SEEK_SET) || fread(out,1,size,f)!=size?-1:0;
}
static void track(const char *path,const char *output,int audio)
{
    FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);assert(size>0);
    wili_mp4_dash *r=calloc(1,sizeof(*r));assert(r);
    assert(!wili_mp4_dash_open(r,read_at,f,(uint64_t)size,audio));assert(r->index_count>=3);
    unsigned level;if(!audio)assert(!wili_mp4_dash_prepare_ps3(r,&level));
    unsigned char *packet=malloc(WILI_DASH_PACKET_CAP);assert(packet);
    uint64_t fragment=0;size_t n=0;wili_dash_sample sample;
    assert(!wili_mp4_dash_seek(r,2200,&fragment));assert(fragment<=2200);
    assert(wili_mp4_dash_packet(r,packet,WILI_DASH_PACKET_CAP,&n,&sample)==1);
    assert(!wili_mp4_dash_seek(r,250,&fragment));assert(fragment<=250);
    assert(wili_mp4_dash_packet(r,packet,WILI_DASH_PACKET_CAP,&n,&sample)==1);
    assert(wili_mp4_dash_seek(r,UINT64_MAX,&fragment)<0);
    assert(!wili_mp4_dash_seek(r,1500,&fragment));assert(fragment<=1500);
    uint64_t extracted_start=fragment;
    FILE *out=fopen(output,"wb");assert(out);unsigned count=0;
    int rc;
    while((rc=wili_mp4_dash_packet(r,packet,WILI_DASH_PACKET_CAP,&n,&sample))>0){
        assert(fwrite(packet,1,n,out)==n);count++;
    }
    assert(rc==0 && count>0);if(!audio)assert(count==60);
    assert(!wili_mp4_dash_seek(r,0,&fragment));assert(wili_mp4_dash_packet(r,packet,WILI_DASH_PACKET_CAP,&n,&sample)==1);
    printf("PASS indexed %s forward/back/EOF restart: preroll_ms=%llu packets=%u\n",audio?"AAC":"AVC",(unsigned long long)extracted_start,count);
    fclose(out);fclose(f);free(packet);free(r);
}
int main(int argc,char **argv)
{
    assert(argc==5);track(argv[1],argv[3],0);track(argv[2],argv[4],1);return 0;
}
