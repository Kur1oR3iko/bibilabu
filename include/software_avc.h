// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct wili_software_avc wili_software_avc;
typedef int (*wili_software_frame)(void*,const uint8_t *const[3],const int[3],unsigned,unsigned,int64_t);
wili_software_avc *wili_software_avc_open(unsigned width,unsigned height,unsigned timescale);
// One complete Annex-B AU. data=NULL drains the decoder. emit may wait for a free display slot.
int wili_software_avc_decode(wili_software_avc*,const uint8_t *data,size_t bytes,int64_t pts,int64_t dts,wili_software_frame emit,void *opaque);
uint64_t wili_software_avc_cpu_us(const wili_software_avc*);
void wili_software_avc_close(wili_software_avc*);
