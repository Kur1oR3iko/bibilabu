// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "avc_sps.h"

// Bounded single-file DASH fragmented MP4 reader for Bilibili's real streams.
// Verified against BV17x411w7KC anonymous DASH: ftyp/free/moov(mvex,trex,
// edts)/sidx + 40 moof(trun)/mdat pairs, avcC High@L3.0 and mp4a ASC 44100.
// Indexes are generated one sample at a time from the current moof, so a
// 216-sample fragment costs no more memory than a 1-sample one.
#define WILI_DASH_META (64u*1024u)
#define WILI_DASH_CONFIG 4096u
#define WILI_DASH_PACKET_CAP (512u*1024u)
#define WILI_DASH_MAX_TRUNS 8u
#define WILI_DASH_MAX_NALS 64u
#define WILI_DASH_MAX_INDEX 4096u
typedef struct {uint64_t offset,time;uint32_t bytes,duration,sap;} wili_dash_index;

typedef int (*wili_dash_read_at)(void*,uint64_t,void*,size_t);
typedef struct {uint64_t offset,dts,pts;uint32_t size,duration,flags;} wili_dash_sample;

typedef struct {
    int audio;
    uint32_t track_id, timescale, mvhd_timescale;
    uint32_t default_duration, default_size, default_flags;
    uint32_t nal_length_size;                 // from avcC, 1..4
    uint32_t config_size;
    uint8_t config[WILI_DASH_CONFIG];         // Annex B SPS+PPS, or raw ASC
    wili_avc_sps sps;                        // video only
    uint32_t sample_rate, sample_rate_index, channels, audio_object_type;
    uint32_t sar_width, sar_height;           // pasp or VUI
    uint64_t start_pts, empty_edit_ms;        // elst mapping in media timescale
    uint64_t sidx_duration_ticks, sidx_total_bytes, sidx_first_offset;
    uint32_t sidx_timescale, sidx_references;
    uint64_t earliest_pts;
    uint32_t max_cto_ticks;                   // observed composition offset (B-frame depth)
    uint32_t coded_width, coded_height, display_width, display_height, level, refs;
} wili_dash_info;

typedef struct {
    size_t fields;uint32_t count,flags,first_flags;int version;int32_t data_offset;int has_data_offset;
} wili_dash_trun;

typedef struct {
    wili_dash_read_at read;void *opaque;uint64_t size,scan;
    wili_dash_info info;
    uint8_t metadata[WILI_DASH_META];
    uint8_t scratch[WILI_DASH_PACKET_CAP];
    uint64_t moof_at,moof_base,mdat_begin,mdat_end,base_dts,cursor;
    uint32_t sequence,fragments,total_samples,delivered_samples;
    uint32_t frag_duration,frag_size,frag_flags;
    uint32_t trun_count,trun_index,sample_index;
    wili_dash_trun truns[WILI_DASH_MAX_TRUNS];
    uint64_t expected_dts;
    int audio,error,eof,stage;
    unsigned decoder_level;
    unsigned max_fps,rate_frames,measured_fps_num,measured_fps_den;
    uint64_t rate_ticks;
    wili_dash_index index[WILI_DASH_MAX_INDEX];
    unsigned index_count;
} wili_mp4_dash;

// Parses moov/sidx (bounded reads) and positions the reader at the first moof.
int wili_mp4_dash_open(wili_mp4_dash*,wili_dash_read_at,void*,uint64_t size,int audio);
// Produces one converted AU: Annex B (AUD/SPS/PPS/SEI/VCL) or ADTS AAC.
// Returns 1 on sample, 0 on clean EOF, -1 on malformed input.
int wili_mp4_dash_packet(wili_mp4_dash*,uint8_t *out,size_t cap,size_t *bytes,wili_dash_sample*);
const wili_dash_info *wili_mp4_dash_info(const wili_mp4_dash*);
int wili_mp4_dash_prepare_ps3(wili_mp4_dash *r,unsigned *level);
// Inspect up to 60 sample durations without consuming playback or media payload.
// max_fps=0 measures without imposing a frame-rate ceiling.
// A nonzero max_fps also enables a duration guard on later video samples.
int wili_mp4_dash_probe_rate(wili_mp4_dash *r,unsigned max_fps,unsigned *num,unsigned *den);
// Seek to a preceding independently decodable fragment. The player discards
// preroll to the requested media time; this never scans from the file start.
int wili_mp4_dash_seek(wili_mp4_dash *r,uint64_t presentation_ms,uint64_t *fragment_ms);
