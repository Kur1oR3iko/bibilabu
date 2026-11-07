// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
// Bounded streaming linear resampler: Bilibili AAC is 44100 Hz while the PS3
// audio port consumes 48000 Hz. Uses exact rational output positions
// (k * src_rate / dst_rate) so a 200-second stream never drifts, and keeps
// only one carry sample plus the current decoded frame.
typedef struct {
    uint32_t src_rate,dst_rate;
    uint64_t out_index,frame_base;
    float prev[2];
    int have_prev;
} wili_resampler;
int wili_resampler_init(wili_resampler*,uint32_t src_rate,uint32_t dst_rate);
int wili_resampler_feed(wili_resampler*,const float *in,uint32_t frames,float *out,uint32_t cap,uint32_t *produced);
uint64_t wili_resampler_output_for(uint32_t src_rate,uint32_t dst_rate,uint64_t source_samples);
