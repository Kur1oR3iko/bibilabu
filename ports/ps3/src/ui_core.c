// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_core.h"
static int alpha_target;
void ui_core_alpha_target(int enabled){alpha_target=enabled!=0;}
uint32_t ui_rgba_over(uint32_t dst,uint32_t src)
{
    unsigned a=src>>24,inv=255-a;
    unsigned r=(((src>>16)&255)*a+((dst>>16)&255)*inv+127)/255;
    unsigned g=(((src>>8)&255)*a+((dst>>8)&255)*inv+127)/255;
    unsigned b=((src&255)*a+(dst&255)*inv+127)/255;
    unsigned out_a=a+((dst>>24)*inv+127)/255;
    return (out_a<<24)|(r<<16)|(g<<8)|b;
}

void ui_draw_fill_rect(uint32_t *fb, unsigned fb_w, unsigned fb_h, int x, int y, int w, int h, uint32_t color)
{
    if(!fb || w <= 0 || h <= 0) return;
    int left = x, top = y;
    int right = x + w, bottom = y + h;
    if(left < 0) left = 0;
    if(top < 0) top = 0;
    if(right > (int)fb_w) right = (int)fb_w;
    if(bottom > (int)fb_h) bottom = (int)fb_h;

    for(int yy = top; yy < bottom; yy++) {
        uint32_t *row = fb + (unsigned)yy * fb_w;
        for(int xx = left; xx < right; xx++) {
            if(alpha_target)row[xx]=ui_rgba_over(row[xx],color&0xff000000u?color:color|0xff000000u);
            else row[xx] = color;
        }
    }
}

void ui_draw_stroke_rect(uint32_t *fb, unsigned fb_w, unsigned fb_h, int x, int y, int w, int h, int thickness, uint32_t color)
{
    if(!fb || w <= 0 || h <= 0 || thickness <= 0) return;
    ui_draw_fill_rect(fb, fb_w, fb_h, x, y, w, thickness, color);
    ui_draw_fill_rect(fb, fb_w, fb_h, x, y + h - thickness, w, thickness, color);
    ui_draw_fill_rect(fb, fb_w, fb_h, x, y + thickness, thickness, h - 2 * thickness, color);
    ui_draw_fill_rect(fb, fb_w, fb_h, x + w - thickness, y + thickness, thickness, h - 2 * thickness, color);
}

void ui_draw_card(uint32_t *fb, unsigned fb_w, unsigned fb_h, int x, int y, int w, int h, int is_focused)
{
    uint32_t bg = is_focused ? UI_COLOR_CARD_HOVER : UI_COLOR_CARD_DARK;
    ui_draw_fill_rect(fb, fb_w, fb_h, x, y, w, h, bg);
    if(is_focused) {
        ui_draw_stroke_rect(fb, fb_w, fb_h, x, y, w, h, 3, UI_COLOR_BILI_PINK);
    } else {
        ui_draw_stroke_rect(fb, fb_w, fb_h, x, y, w, h, 1, 0x001e293b);
    }
}
