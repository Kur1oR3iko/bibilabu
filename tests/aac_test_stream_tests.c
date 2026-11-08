// SPDX-License-Identifier: GPL-3.0-or-later
#include "aac_test_stream.h"
#include "test_audio_identity.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv)
{
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    fseek(file,0,SEEK_END);long n=ftell(file);rewind(file);assert(n>7 && n<4*1024*1024);
    uint8_t *p=malloc((size_t)n*2);assert(p);assert(fread(p,1,(size_t)n,file)==(size_t)n);fclose(file);
    wili_aac_stream stream;assert(!wili_parse_test_aac(p,(size_t)n,&stream));assert(stream.count==WILI_AUDIO_AUS);
    unsigned i;size_t end=0;
    for(i=0;i<stream.count;i++){assert(stream.frames[i].offset==end);end+=stream.frames[i].size;assert(end<=(size_t)n);}
    assert(end==(size_t)n);
    for(i=0;i<7;i++)assert(wili_parse_test_aac(p,i,&stream)<0);
    assert(wili_parse_test_aac(p,(size_t)n-1,&stream)<0);
    memcpy(p+n,p,(size_t)n);assert(wili_parse_test_aac(p,(size_t)n*2,&stream)<0);
    uint8_t old=p[2];p[2]^=0x40;assert(wili_parse_test_aac(p,(size_t)n,&stream)<0);p[2]=old;
    uint8_t fuzz[1024];uint32_t random=1;
    for(i=0;i<20000;i++){
        unsigned j,size=i%sizeof(fuzz);
        for(j=0;j<size;j++){random=random*1664525u+1013904223u;fuzz[j]=(uint8_t)(random>>24);}
        (void)wili_parse_test_aac(fuzz,size,&stream);
    }
    free(p);puts("PASS: actual ADTS boundaries/count, truncated frames, wrong profile, AU capacity and 20000 fuzz cases");return 0;
}
