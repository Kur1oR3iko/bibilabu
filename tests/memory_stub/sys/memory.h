#pragma once
#include <stddef.h>
#include <stdint.h>
typedef uintptr_t sys_mem_addr_t;
#define SYS_MEMORY_PAGE_SIZE_1M 0x400ULL
#define SYS_MEMORY_PAGE_SIZE_64K 0x200ULL
int sysMemoryAllocate(size_t size,uint64_t flags,sys_mem_addr_t *address);
int sysMemoryFree(sys_mem_addr_t address);
