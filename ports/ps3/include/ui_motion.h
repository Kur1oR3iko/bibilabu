// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
typedef struct {float from,to,value;uint64_t start;} ui_tween;
typedef struct {ui_tween category,selection;int initialized,tab,count;unsigned page;uint64_t context;} ui_motion;
int ui_motion_step(ui_motion *m,int tab,int selected,unsigned page,uint64_t context,int count,uint64_t now);

typedef struct {uint64_t last;int active;} ui_loading_clock;
int ui_loading_redraw(ui_loading_clock *clock,int active,uint64_t now);

typedef struct {uint64_t since,hold_until,last;int buffering;} ui_buffer_notice;
int ui_buffer_notice_step(ui_buffer_notice*,int buffering,uint64_t now);
