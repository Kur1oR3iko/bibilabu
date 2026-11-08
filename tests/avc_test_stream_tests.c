// SPDX-License-Identifier: GPL-3.0-or-later
#include "avc_test_stream.h"
#include "test_clip_identity.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv)
{
    assert(argc==2);
    FILE *file=fopen(argv[1],"rb");assert(file);
    assert(!fseek(file,0,SEEK_END));long length=ftell(file);assert(length>0 && length<4*1024*1024);
    rewind(file);uint8_t *data=malloc((size_t)length);assert(data);
    assert(fread(data,1,(size_t)length,file)==(size_t)length);fclose(file);
    wili_test_stream stream;
    assert(!wili_parse_test_avc(data,(size_t)length,&stream));
    assert(stream.count==WILI_CLIP_FRAMES && stream.profile==66 && stream.level==WILI_CLIP_LEVEL);
    assert(stream.coded_width==WILI_CLIP_WIDTH && stream.coded_height==WILI_CLIP_HEIGHT && stream.reference_frames==1);
    unsigned i;size_t offset=0;
    for(i=0;i<stream.count;i++){
        assert(stream.aus[i].offset==offset && stream.aus[i].size>0);
        offset+=stream.aus[i].size;assert(offset<=(size_t)length);
    }
    assert(offset==(size_t)length);
    uint8_t *copy=malloc((size_t)length*2);assert(copy);
    memcpy(copy,data,(size_t)length);memcpy(copy+length,data,(size_t)length);
    assert(wili_parse_test_avc(copy,(size_t)length*2,&stream)<0); // AU index capacity
    size_t sps_index=0,j;
    for(j=0;j+4<(size_t)length;j++)if(!data[j] && !data[j+1] && data[j+2]==1 && (data[j+3]&31)==7){sps_index=j+3;break;}
    assert(sps_index && sps_index+4<(size_t)length);
    memcpy(copy,data,(size_t)length);copy[sps_index+1]=110; // High10 is outside this diagnostic's ABI contract
    assert(wili_parse_test_avc(copy,(size_t)length,&stream)<0);
    memcpy(copy,data,(size_t)length);copy[sps_index]|=0x80; // forbidden_zero_bit
    assert(wili_parse_test_avc(copy,(size_t)length,&stream)<0);
    // Target mutations at actual headers so fuzzing exercises more than start-code rejection.
    for(j=0;j<256 && j<(size_t)length;j++){
        memcpy(copy,data,(size_t)length);copy[j]^=0xff;
        (void)wili_parse_test_avc(copy,(size_t)length,&stream);
    }
    free(copy);
    assert(wili_test_crc32((const uint8_t*)"123456789",9)==0xcbf43926u);
    assert(wili_parse_test_avc(0,8,&stream)<0);
    assert(wili_parse_test_avc(data,(size_t)length,0)<0);
    uint8_t malicious[256]={0,0,0,1,9,0xf0,0,0,0,1,0x67,66,0,20};
    // A missing Exp-Golomb terminator must not shift by >=32 or walk past EOF.
    assert(wili_parse_test_avc(malicious,sizeof(malicious),&stream)<0);
    // Truncate every byte of SPS/header prefix, then fuzz bounded small inputs.
    for(i=0;i<40;i++)(void)wili_parse_test_avc(data,i,&stream);
    uint8_t fuzz[1024];uint32_t random=1;
    for(i=0;i<20000;i++){
        size_t j,size=i%sizeof(fuzz);
        for(j=0;j<size;j++){random=random*1664525u+1013904223u;fuzz[j]=(uint8_t)(random>>24);}
        (void)wili_parse_test_avc(fuzz,size,&stream);
    }
    free(data);puts("PASS: real SPS/AU boundaries, CRC vector, truncated and malformed inputs, 20000 bounded fuzz cases");
    return 0;
}
