// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "yuv_frame.h"

void wili_video_start(uint64_t now, void (*log)(const char *));
void wili_video_tick(uint64_t now);
void wili_video_pause(uint64_t now);
void wili_video_stop(void);
const uint32_t *wili_video_pixels(unsigned *width,unsigned *height);
void wili_video_status(char *out,unsigned capacity);

unsigned wili_video_generation(void);
uint32_t wili_video_rsx_offset(void);
int wili_video_presented(void);

unsigned wili_video_state(void);

uint64_t wili_video_playback_time_us(void);
uint64_t wili_video_duration_ms(void);
void wili_video_get_buffer_status(size_t *v_buf, size_t *a_buf);
int wili_video_is_paused(void);

int wili_video_stop_safe(void);
void wili_video_cancel_workers(void);
void wili_video_log_clock(void);
unsigned wili_video_buffer_ms(void);

unsigned wili_video_pitch(void);

int wili_video_seek(uint64_t target_ms,uint64_t now);

int wili_video_surface(wili_yuv_surface *out);
