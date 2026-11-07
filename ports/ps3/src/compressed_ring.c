// SPDX-License-Identifier: GPL-3.0-or-later
#include "compressed_ring.h"
#include <string.h>
static uint32_t snap(volatile uint32_t *p){return __sync_add_and_fetch(p,0);}
int wili_compressed_push(wili_compressed_ring *r,const void *src,uint32_t n,uint32_t *offset)
{
    if(!r || !r->data || !r->capacity || (r->capacity&(r->capacity-1)) || !src || !offset || !n || n>r->capacity)return -1;
    uint32_t h=snap(&r->head),t=snap(&r->tail),used=h-t;
    if(used>r->capacity)return -1;
    if(n>r->capacity-used)return 0;
    uint32_t at=h&(r->capacity-1),first=r->capacity-at;if(first>n)first=n;
    memcpy(r->data+at,src,first);if(first<n)memcpy(r->data,(const uint8_t*)src+first,n-first);
    *offset=h;__sync_lock_test_and_set(&r->head,h+n);return 1;
}
int wili_compressed_pop(wili_compressed_ring *r,void *dst,uint32_t n,uint32_t offset)
{
    if(!r || !r->data || !dst || !n || !r->capacity || (r->capacity&(r->capacity-1)))return -1;
    uint32_t t=snap(&r->tail),used=snap(&r->head)-t;
    if(offset!=t || used>r->capacity || n>used)return -1;
    uint32_t at=t&(r->capacity-1),first=r->capacity-at;if(first>n)first=n;
    memcpy(dst,r->data+at,first);if(first<n)memcpy((uint8_t*)dst+first,r->data,n-first);
    __sync_lock_test_and_set(&r->tail,t+n);return 0;
}
