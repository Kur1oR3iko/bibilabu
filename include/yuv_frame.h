// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct {
    unsigned width,height;
    uint32_t pitch[3],offset[3],bytes,packed_bytes;
} wili_yuv_layout;
typedef struct {
    wili_yuv_layout layout;
    uint32_t rsx_base;
    unsigned visible_width,visible_height,crop_x,crop_y;
    uint64_t aspect_num,aspect_den;
    unsigned bt709,full_range;
} wili_yuv_surface;
int wili_yuv_layout_init(wili_yuv_layout *layout,unsigned width,unsigned height);
int wili_yuv_repack(uint8_t *frame,size_t capacity,const wili_yuv_layout *layout);
int wili_yuv_pool_plan(unsigned width,unsigned height,unsigned count,size_t *stride,size_t *arena);
// Reference conversion for offline shader and range/color tests, not the player.
uint32_t wili_yuv_reference(unsigned y,unsigned u,unsigned v,int bt709,int full_range);
