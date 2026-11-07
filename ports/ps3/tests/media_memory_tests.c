// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200112L
#include "media_memory.h"
#include <sys/memory.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
static struct {void *data;size_t size;} live[8];
static size_t used;static unsigned calls,fail_at;static int free_error;
int sysMemoryAllocate(size_t size,uint64_t flags,sys_mem_addr_t *address)
{
    calls++;if(calls==fail_at)return -12;
    size_t page=flags==SYS_MEMORY_PAGE_SIZE_1M?1048576u:65536u;assert(size && size%page==0);
    for(unsigned i=0;i<8;i++)if(!live[i].data){
        void *ptr=NULL;if(posix_memalign(&ptr,page,size))return -12;
        live[i].data=ptr;live[i].size=size;used+=size;*address=(uintptr_t)ptr;return 0;
    }
    assert(0);return -1;
}
int sysMemoryFree(sys_mem_addr_t address)
{
    if(free_error)return -1;
    for(unsigned i=0;i<8;i++)if(live[i].data==(void*)address){used-=live[i].size;free(live[i].data);live[i].data=NULL;return 0;}
    assert(0);return -1;
}
int main(void)
{
    // Repeated landscape/portrait/360p sessions, plus failure at each allocation.
    for(unsigned cycle=0;cycle<24;cycle++)for(unsigned failure=0;failure<=3;failure++){
        wili_media_memory blocks[3]={{0}};calls=0;fail_at=failure;
        size_t requests[]={cycle%2?25538941u:57299581u,cycle%3?15u*1048576:4u*1048576,16u*1048576};
        for(unsigned i=0;i<3;i++){
            int rc=wili_media_memory_alloc(&blocks[i],requests[i],i!=0);
            if(rc){assert(failure==i+1 && !blocks[i].data);break;}
            assert(blocks[i].data && blocks[i].bytes>=requests[i]);
            assert((uintptr_t)blocks[i].data%(i?1048576u:65536u)==0);
            assert(wili_media_memory_alloc(&blocks[i],requests[i],i!=0)<0);
        }
        for(unsigned i=0;i<3;i++){assert(!wili_media_memory_free(&blocks[i]));assert(!wili_media_memory_free(&blocks[i]));}
        assert(used==0);
    }
    fail_at=0;wili_media_memory b={0};assert(wili_media_memory_alloc(&b,SIZE_MAX,1)<0);
    assert(!wili_media_memory_alloc(&b,100,0));free_error=1;assert(wili_media_memory_free(&b)<0 && b.data && used);
    free_error=0;assert(!wili_media_memory_free(&b) && !used);
    puts("PASS LV2 allocation page rounding/alignment, repeated sessions, partial-init cleanup, double-free prevention and release-failure ownership");
}
