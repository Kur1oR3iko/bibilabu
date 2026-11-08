// SPDX-License-Identifier: GPL-3.0-or-later
#include "media_memory.h"
#include <sys/memory.h>
#include <stdint.h>
int wili_media_memory_alloc(wili_media_memory *block,size_t bytes,int megabyte_pages)
{
    if(!block || block->data || !bytes || bytes>128u*1024u*1024u)return -1;
    size_t page=megabyte_pages?1048576u:65536u;
    size_t rounded=(bytes+page-1)&~(page-1);sys_mem_addr_t address=0;
    int rc=sysMemoryAllocate(rounded,megabyte_pages?SYS_MEMORY_PAGE_SIZE_1M:SYS_MEMORY_PAGE_SIZE_64K,&address);
    if(rc)return rc;
    block->data=(void*)(uintptr_t)address;block->bytes=rounded;return 0;
}
int wili_media_memory_free(wili_media_memory *block)
{
    if(!block)return -1;
    if(!block->data)return 0;
    int rc=sysMemoryFree((sys_mem_addr_t)(uintptr_t)block->data);
    if(rc)return rc;
    block->data=NULL;block->bytes=0;return 0;
}
