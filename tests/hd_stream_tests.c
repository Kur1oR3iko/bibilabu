// SPDX-License-Identifier: GPL-3.0-or-later
#define _FILE_OFFSET_BITS 64
#include "mp4_dash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static int read_at(void *arg,uint64_t at,void *out,size_t n)
{
    FILE *f=arg;return fseek(f,(long)at,SEEK_SET) || fread(out,1,n,f)!=n?-1:0;
}
int main(int argc,char **argv)
{
    assert(argc==3 || argc==4);FILE *f=fopen(argv[1],"rb"),*out=fopen(argv[2],"wb");assert(f && out);
    assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0);
    wili_mp4_dash *reader=calloc(1,sizeof(*reader));unsigned char *packet=malloc(WILI_DASH_PACKET_CAP);assert(reader && packet);
    int opened=wili_mp4_dash_open(reader,read_at,f,(uint64_t)size,0);
    if(opened)fprintf(stderr,"HD open failed stage=%d\n",reader->stage);
    assert(!opened);
    const wili_dash_info *info=wili_mp4_dash_info(reader);
    assert(info->coded_width==1280 && info->coded_height==720 && info->sps.profile==100);
    assert(info->sps.matrix_coefficients==1);
    unsigned source_level=argc==4?(unsigned)strtoul(argv[3],NULL,10):31,decoder_level=0;
    assert(info->level==source_level);
    if(source_level==51){
        wili_avc_sps bad=info->sps;bad.max_num_ref_frames=16;assert(wili_avc_ps3_level(&bad,&decoder_level)<0);
        bad=info->sps;bad.hrd_present=1;assert(wili_avc_ps3_level(&bad,&decoder_level)<0);
    }
    assert(!wili_mp4_dash_prepare_ps3(reader,&decoder_level));
    assert(decoder_level==(source_level==51?41:31));
    printf("PASS PS3 AVC level policy source=%u decoder=%u\n",source_level,decoder_level);
    unsigned count=0;size_t max_packet=0;int rc;
    do {
        size_t bytes=0;wili_dash_sample sample;
        rc=wili_mp4_dash_packet(reader,packet,WILI_DASH_PACKET_CAP,&bytes,&sample);assert(rc>=0);
        if(rc){assert(fwrite(packet,1,bytes,out)==bytes);count++;if(bytes>max_packet)max_packet=bytes;}
    }while(rc);
    assert(count==90);printf("PASS actual 720p30 High-profile fMP4: %u AUs, largest=%zu, BT709, bounded EOF\n",count,max_packet);
    fclose(out);fclose(f);free(reader);free(packet);return 0;
}
