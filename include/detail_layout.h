// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#define DETAIL_VIDEO_X 32u
#define DETAIL_VIDEO_Y 48u
#define DETAIL_VIDEO_W 800u
#define DETAIL_VIDEO_H 450u
#define DETAIL_BODY_X 32u
#define DETAIL_BODY_Y 510u
#define DETAIL_BODY_W 800u
#define DETAIL_BODY_H 172u
#define DETAIL_COMMENT_Y 132u
#define DETAIL_COMMENT_ROW 80u
#define DETAIL_RELATED_FOCUS 7u
#define DETAIL_COMMENT_FOCUS 15u
enum {DETAIL_NAV_LEFT,DETAIL_NAV_RIGHT,DETAIL_NAV_UP,DETAIL_NAV_DOWN};
typedef struct {float value,from,target;uint64_t begin;int initialized;} detail_scroll;
unsigned detail_content_height(unsigned comments);
int detail_scroll_step(detail_scroll*,unsigned focus,unsigned comments,uint64_t now);
unsigned detail_navigate(unsigned focus,unsigned direction,unsigned related,unsigned comments);
