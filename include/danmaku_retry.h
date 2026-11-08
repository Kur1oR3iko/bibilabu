// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
typedef struct {unsigned epoch,index,failures;uint64_t after;} wili_dm_retry;
static inline int wili_dm_retry_due(const wili_dm_retry *r,unsigned epoch,unsigned index,uint64_t now)
{return r->epoch!=epoch || r->index!=index || now>=r->after;}
static inline unsigned wili_dm_retry_fail(wili_dm_retry *r,unsigned epoch,unsigned index,uint64_t now,long http)
{
    if(r->epoch!=epoch || r->index!=index){r->failures=0;r->epoch=epoch;r->index=index;}
    if(r->failures<5)r->failures++;
    unsigned delay=5u<<(r->failures-1);if(delay>60 || http==403 || http==412)delay=60;
    r->after=now+(uint64_t)delay*1000000u;return delay;
}
