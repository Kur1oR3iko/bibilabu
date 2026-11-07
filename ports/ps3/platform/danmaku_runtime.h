// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "danmaku.h"
int danmaku_init(const char *ca,void (*log)(const char*));
void danmaku_start(uint64_t cid);
void danmaku_stop(void);
void danmaku_shutdown(void);
void danmaku_density(unsigned value);
unsigned danmaku_get_density(void);
unsigned danmaku_tick(uint32_t video_ms,int allow_network);
const wili_dm_draw *danmaku_draw_list(void);
