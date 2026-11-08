// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fast color helper (ARGB: 0x00RRGGBB or 0xAARRGGBB)
#define UI_COLOR(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define UI_COLOR_ARGB(a, r, g, b) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

#define UI_COLOR_BILI_PINK    0x00fb7299
#define UI_COLOR_BILI_BLUE    0x0000a6ff
#define UI_COLOR_BG_DARK      0x000a0e17
#define UI_COLOR_CARD_DARK    0x00131c2e
#define UI_COLOR_CARD_HOVER   0x001d2a42
#define UI_COLOR_TEXT_MAIN    0x00f1f5f9
#define UI_COLOR_TEXT_MUTED   0x0094a3b8
#define UI_COLOR_ACCENT_GREEN 0x0000e699

void ui_draw_fill_rect(uint32_t *fb, unsigned fb_w, unsigned fb_h, int x, int y, int w, int h, uint32_t color);
void ui_draw_stroke_rect(uint32_t *fb, unsigned fb_w, unsigned fb_h, int x, int y, int w, int h, int thickness, uint32_t color);
void ui_draw_card(uint32_t *fb, unsigned fb_w, unsigned fb_h, int x, int y, int w, int h, int is_focused);

#ifdef __cplusplus
}
#endif

void ui_core_alpha_target(int enabled);
// Straight ARGB source over a premultiplied ARGB destination.
uint32_t ui_rgba_over(uint32_t dst,uint32_t straight_argb);
