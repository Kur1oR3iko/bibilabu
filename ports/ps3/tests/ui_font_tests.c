// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_font.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    // 1. Test UTF-8 decoder
    const char *text = "哔哩哔哩 wiliwili 720P 高清";
    const char *p = text;
    uint32_t cp1 = ui_utf8_next(&p); // 哔
    uint32_t cp2 = ui_utf8_next(&p); // 哩
    uint32_t cp3 = ui_utf8_next(&p); // 哔
    uint32_t cp4 = ui_utf8_next(&p); // 哩
    uint32_t cp5 = ui_utf8_next(&p); // ' '
    uint32_t cp6 = ui_utf8_next(&p); // 'w'
    assert(cp1 == 0x54d4); // 哔 in unicode is U+54D4
    assert(cp2 == 0x54e9); // 哩 in unicode is U+54E9
    assert(cp3 == 0x54d4);
    assert(cp4 == 0x54e9);
    assert(cp5 == ' ');
    assert(cp6 == 'w');
    puts("PASS UTF-8 Chinese decoding test");

    // 2. Test Font loading
    int rc = ui_font_init("assets/switch_font.ttf");
    if(rc != 0) {
        rc = ui_font_init("assets/switch_font.ttf");
    }
    assert(rc == 0);
    puts("PASS switch_font.ttf loading test");

    // 3. Test Text Measurement
    int w = 0, h = 0;
    ui_font_measure_text(24, text, &w, &h);
    assert(w > 100);
    assert(h >= 24);
    printf("Measured '%s' at 24px: w=%d, h=%d\n", text, w, h);

    // 4. Test Text Rendering into Framebuffer
    #define FB_W 400
    #define FB_H 100
    uint32_t fb[FB_W * FB_H];
    memset(fb, 0, sizeof(fb));

    ui_font_draw_text(fb, FB_W, FB_H, 10, 10, 24, text, 0x00fb7299);

    // Verify non-zero pixels exist in fb
    int drawn_pixels = 0;
    for(int i = 0; i < FB_W * FB_H; i++) {
        if(fb[i] != 0) drawn_pixels++;
    }
    assert(drawn_pixels > 200);
    printf("Rendered '%s': drawn_pixels=%d\n", text, drawn_pixels);

    ui_font_shutdown();
    puts("PASS full Chinese font rasterization and framebuffer blending test");
    return 0;
}
