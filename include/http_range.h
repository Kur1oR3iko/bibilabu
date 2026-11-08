// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#define WILI_HTTP_CACHE (256u*1024u)
typedef struct {
    void *easy;uint8_t *cache;uint64_t size,offset,requests,downloaded;
    size_t valid,capacity;char url[2048],etag[160],error[256];
    int (*cancel)(void*);void *cancel_arg;void (*log)(const char*);
} wili_http_range;
int wili_http_range_open(wili_http_range*,const char*,uint64_t,const char *ca_file,int (*cancel)(void*),void*,void (*log)(const char*));
int wili_http_range_read(wili_http_range*,uint64_t,void*,size_t);
// Call only while the reader has no active transfer. Default remains 256KiB.
int wili_http_range_set_window(wili_http_range*,size_t bytes);
int wili_http_range_fetch_block(wili_http_range*,uint64_t start,void *dst,size_t *out_received);
void wili_http_range_close(wili_http_range*);
