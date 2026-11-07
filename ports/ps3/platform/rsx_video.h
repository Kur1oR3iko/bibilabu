// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <rsx/rsx.h>
#include "yuv_frame.h"
#include "danmaku.h"
int rsx_video_init(unsigned width,unsigned height,void (*log)(const char*));
int rsx_video_draw(gcmContextData *ctx,uint32_t target,uint32_t pitch,
                   const wili_yuv_surface *frame,uint32_t overlay_offset,float opacity,const wili_dm_draw *dm);
void rsx_video_set_box(unsigned x,unsigned y,unsigned width,unsigned height);
void rsx_video_set_background(uint32_t offset,int enabled);
void rsx_video_shutdown(void);

void rsx_video_set_glass(int controls,int quality,int exit_dialog);
