// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WILI_UI_IMAGE_H
#define WILI_UI_IMAGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int ui_image_init(const char *ca_path, void (*log)(const char*));
void ui_image_shutdown(void);
void ui_image_cancel(void);
void ui_image_end_session(void);

// Returns 1 if drawn, 0 if still loading / placeholder needed
int ui_image_draw(uint32_t *fb, unsigned fb_w, unsigned fb_h, int dx, int dy, int dw, int dh, const char *url);

#ifdef __cplusplus
}
#endif

#endif

void ui_image_pause(int value);
unsigned ui_image_revision(void);

int ui_image_decode_wallpaper(const unsigned char *data,size_t bytes,uint32_t *out);
void ui_image_request_wallpaper(const char *path);
int ui_image_take_wallpaper(uint32_t *out);
