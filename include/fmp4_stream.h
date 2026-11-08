// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#define WILI_MP4_META 65536u
#define WILI_MP4_SAMPLES 128u
#define WILI_MP4_CONFIG 4096u
typedef int (*wili_read_at)(void*,uint64_t,void*,size_t);
typedef struct {uint64_t offset,dts,pts;uint32_t size,duration,flags;} wili_mp4_sample;
typedef struct {
    wili_read_at read;void *opaque;uint64_t size,scan,expected_dts;
    uint32_t track,timescale,default_duration,default_size,default_flags;
    uint32_t sequence,fragments,total_samples,count,cursor;
    unsigned audio,width,height,level,refs,config_size,sps_size;
    uint8_t config[WILI_MP4_CONFIG];
    uint8_t metadata[WILI_MP4_META];
    wili_mp4_sample samples[WILI_MP4_SAMPLES];
    int error,eof;
} wili_fmp4;
// One track per fragmented file (the DASH-style split track case).
// Supports AVC baseline and AAC-LC 48kHz stereo; ordinary stbl MP4 is rejected.
int wili_fmp4_open(wili_fmp4*,wili_read_at,void*,uint64_t size,int audio);
// Converts a sample to Annex B (AVC) or ADTS (AAC), retaining only one fragment
// index. Caller owns output storage until its decoder's AUDONE callback.
int wili_fmp4_packet(wili_fmp4*,uint8_t *out,size_t capacity,size_t *bytes,wili_mp4_sample*);
