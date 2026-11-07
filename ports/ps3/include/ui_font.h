// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize TrueType font subsystem from a .ttf file in memory or file
int ui_font_init(const char *font_path);
void ui_font_shutdown(void);

// Render anti-aliased UTF-8 text (including Chinese, English, punctuation) into ARGB32 framebuffer
void ui_font_draw_text(uint32_t *fb, unsigned fb_w, unsigned fb_h, int x, int y, int size, const char *utf8_text, uint32_t color);

// Measure text width and height in pixels for layout calculations
void ui_font_measure_text(int size, const char *utf8_text, int *out_w, int *out_h);

// UTF-8 helper: decode next codepoint from string
uint32_t ui_utf8_next(const char **ptr);

#ifdef __cplusplus
}
#endif

void ui_font_alpha_target(int enabled);
