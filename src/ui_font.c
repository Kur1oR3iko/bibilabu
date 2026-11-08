// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb/stb_truetype.h"

#if defined(__PPU__) || defined(__CELLOS_LV2__)
#include <sys/file.h>
#endif

typedef struct {
    int valid;
    uint32_t codepoint;
    int size;
    int w, h;
    int xoff, yoff;
    int advance;
    uint8_t *bitmap;
    uint64_t last_use;
} glyph_cache_entry_t;

#define GLYPH_CACHE_SIZE 1024
static glyph_cache_entry_t s_cache[GLYPH_CACHE_SIZE];
static uint64_t s_use_counter = 0;

static unsigned char *s_font_data = NULL;
static size_t s_font_size = 0;
static stbtt_fontinfo s_font;
static int s_initialized = 0;
static int s_alpha_target;
void ui_font_alpha_target(int enabled){s_alpha_target=enabled!=0;}

uint32_t ui_utf8_next(const char **ptr)
{
    const unsigned char *s = (const unsigned char*)*ptr;
    if(!s || !*s) return 0;
    uint32_t cp = 0;
    if(s[0] < 0x80) {
        cp = s[0];
        *ptr += 1;
    } else if((s[0] & 0xe0) == 0xc0) {
        if((s[1] & 0xc0) == 0x80) {
            cp = ((s[0] & 0x1f) << 6) | (s[1] & 0x3f);
            *ptr += 2;
        } else { *ptr += 1; cp = '?'; }
    } else if((s[0] & 0xf0) == 0xe0) {
        if((s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) {
            cp = ((s[0] & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f);
            *ptr += 3;
        } else { *ptr += 1; cp = '?'; }
    } else if((s[0] & 0xf8) == 0xf0) {
        if((s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80 && (s[3] & 0xc0) == 0x80) {
            cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3f) << 12) | ((s[2] & 0x3f) << 6) | (s[3] & 0x3f);
            *ptr += 4;
        } else { *ptr += 1; cp = '?'; }
    } else {
        *ptr += 1; cp = '?';
    }
    return cp;
}

int ui_font_init(const char *font_path)
{
    ui_font_shutdown();

    const char *paths[] = {
        font_path,
        "/dev_hdd0/game/WILIPRB01/USRDIR/font.ttf",
        "/dev_usb000/font.ttf",
        "assets/switch_font.ttf",
        NULL
    };

    for(int i = 0; paths[i]; i++) {
        const char *p = paths[i];
        if(!p) continue;
#if defined(__PPU__) || defined(__CELLOS_LV2__)
        int fd = -1;
        if(sysLv2FsOpen(p, SYS_O_RDONLY, &fd, 0, 0, 0) == 0) {
            sysFSStat st;
            if(sysLv2FsFStat(fd, &st) == 0 && st.st_size > 0 && st.st_size < 32 * 1024 * 1024) {
                s_font_data = (unsigned char*)malloc(st.st_size);
                if(s_font_data) {
                    u64 got = 0;
                    if(sysLv2FsRead(fd, s_font_data, st.st_size, &got) == 0 && got == st.st_size) {
                        s_font_size = (size_t)st.st_size;
                        sysLv2FsClose(fd);
                        break;
                    }
                    free(s_font_data); s_font_data = NULL;
                }
            }
            sysLv2FsClose(fd);
        }
#else
        FILE *f = fopen(p, "rb");
        if(f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if(sz > 0 && sz < 32 * 1024 * 1024) {
                s_font_data = (unsigned char*)malloc(sz);
                if(s_font_data && fread(s_font_data, 1, sz, f) == (size_t)sz) {
                    s_font_size = (size_t)sz;
                    fclose(f);
                    break;
                }
                if(s_font_data) { free(s_font_data); s_font_data = NULL; }
            }
            fclose(f);
        }
#endif
    }

    if(!s_font_data) return -1;

    if(!stbtt_InitFont(&s_font, s_font_data, stbtt_GetFontOffsetForIndex(s_font_data, 0))) {
        free(s_font_data);
        s_font_data = NULL;
        return -1;
    }

    memset(s_cache, 0, sizeof(s_cache));
    s_initialized = 1;
    return 0;
}

void ui_font_shutdown(void)
{
    for(int i = 0; i < GLYPH_CACHE_SIZE; i++) {
        if(s_cache[i].bitmap) {
            free(s_cache[i].bitmap);
            s_cache[i].bitmap = NULL;
        }
    }
    memset(s_cache, 0, sizeof(s_cache));
    if(s_font_data) {
        free(s_font_data);
        s_font_data = NULL;
    }
    s_font_size = 0;
    s_initialized = 0;
}

static glyph_cache_entry_t *get_glyph(uint32_t cp, int size)
{
    if(!s_initialized) return NULL;
    s_use_counter++;

    uint32_t hash = ((cp * 31) + size) % GLYPH_CACHE_SIZE;
    for(int i = 0; i < 16; i++) {
        int idx = (hash + i) % GLYPH_CACHE_SIZE;
        if(s_cache[idx].codepoint == cp && s_cache[idx].size == size && s_cache[idx].valid) {
            s_cache[idx].last_use = s_use_counter;
            return &s_cache[idx];
        }
    }

    // Miss: find replacement slot (empty or LRU)
    int target_idx = -1;
    uint64_t oldest = UINT64_MAX;
    for(int i = 0; i < 16; i++) {
        int idx = (hash + i) % GLYPH_CACHE_SIZE;
        if(!s_cache[idx].valid) {
            target_idx = idx;
            break;
        }
        if(s_cache[idx].last_use < oldest) {
            oldest = s_cache[idx].last_use;
            target_idx = idx;
        }
    }

    glyph_cache_entry_t *entry = &s_cache[target_idx];
    if(entry->bitmap) {
        free(entry->bitmap);
        entry->bitmap = NULL;
    }

    entry->valid = 1;
    int glyph_idx = stbtt_FindGlyphIndex(&s_font, (int)cp);
    if(glyph_idx == 0) {
        // Character not found in font (e.g. Emoji / unsupported symbols); do not draw missing square
        entry->codepoint = cp;
        entry->size = size;
        entry->w = 0;
        entry->h = 0;
        entry->xoff = 0;
        entry->yoff = 0;
        entry->advance = (cp >= 0x1F300) ? size : (size / 2);
        entry->bitmap = NULL;
        entry->last_use = s_use_counter;
        return entry;
    }

    float scale = stbtt_ScaleForPixelHeight(&s_font, (float)size);
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&s_font, (int)cp, &adv, &lsb);

    int gw = 0, gh = 0, xoff = 0, yoff = 0;
    unsigned char *bmp = stbtt_GetCodepointBitmap(&s_font, 0, scale, (int)cp, &gw, &gh, &xoff, &yoff);

    entry->codepoint = cp;
    entry->size = size;
    entry->w = gw;
    entry->h = gh;
    entry->xoff = xoff;
    entry->yoff = yoff;
    entry->advance = (int)(adv * scale + 0.5f);
    entry->bitmap = bmp;
    entry->last_use = s_use_counter;

    return entry;
}

void ui_font_draw_text(uint32_t *fb, unsigned fb_w, unsigned fb_h, int x, int y, int size, const char *utf8_text, uint32_t color)
{
    if(!fb || !utf8_text || !s_initialized) return;

    float scale = stbtt_ScaleForPixelHeight(&s_font, (float)size);
    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&s_font, &ascent, &descent, &line_gap);
    int baseline = y + (int)(ascent * scale + 0.5f);

    int cur_x = x;
    const char *p = utf8_text;
    uint32_t cr = (color >> 16) & 0xff;
    uint32_t cg = (color >> 8) & 0xff;
    uint32_t cb = color & 0xff;

    while(*p) {
        uint32_t cp = ui_utf8_next(&p);
        if(!cp) break;
        if(cp == '\n') {
            cur_x = x;
            baseline += (int)((ascent - descent + line_gap) * scale + 0.5f);
            continue;
        }

        glyph_cache_entry_t *g = get_glyph(cp, size);
        if(g) {
            if(g->bitmap && g->w > 0 && g->h > 0) {
                int bx = cur_x + g->xoff;
                int by = baseline + g->yoff;

                for(int gy = 0; gy < g->h; gy++) {
                    int py = by + gy;
                    if(py < 0 || py >= (int)fb_h) continue;

                    for(int gx = 0; gx < g->w; gx++) {
                        int px = bx + gx;
                        if(px < 0 || px >= (int)fb_w) continue;

                        uint32_t alpha = g->bitmap[gy * g->w + gx];
                        if(!alpha) continue;

                        int idx = py * (int)fb_w + px;
                        if(s_alpha_target){
                            unsigned opacity=color>>24;if(!opacity)opacity=255;
                            alpha=(alpha*opacity+127)/255;
                            unsigned inv=255-alpha;uint32_t bg=fb[idx];
                            unsigned r=(cr*alpha+((bg>>16)&255)*inv+127)/255;
                            unsigned gg=(cg*alpha+((bg>>8)&255)*inv+127)/255;
                            unsigned bb=(cb*alpha+(bg&255)*inv+127)/255;
                            unsigned aa=alpha+((bg>>24)*inv+127)/255;
                            fb[idx]=(aa<<24)|(r<<16)|(gg<<8)|bb;
                        } else if(alpha >= 250) {
                            fb[idx] = (cr << 16) | (cg << 8) | cb;
                        } else {
                            uint32_t bg = fb[idx];
                            uint32_t inv_a = 255 - alpha;
                            uint32_t r = (cr * alpha + ((bg >> 16) & 0xff) * inv_a) >> 8;
                            uint32_t g_val = (cg * alpha + ((bg >> 8) & 0xff) * inv_a) >> 8;
                            uint32_t b = (cb * alpha + (bg & 0xff) * inv_a) >> 8;
                            fb[idx] = (r << 16) | (g_val << 8) | b;
                        }
                    }
                }
            }
            cur_x += g->advance > 0 ? g->advance : (size / 2);
        } else {
            cur_x += size / 2;
        }
        if(cur_x >= (int)fb_w) break;
    }
}

void ui_font_measure_text(int size, const char *utf8_text, int *out_w, int *out_h)
{
    if(out_w) *out_w = 0;
    if(out_h) *out_h = size;
    if(!utf8_text || !s_initialized) return;

    float scale = stbtt_ScaleForPixelHeight(&s_font, (float)size);
    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&s_font, &ascent, &descent, &line_gap);
    int line_h = (int)((ascent - descent + line_gap) * scale + 0.5f);

    int max_w = 0, cur_w = 0, lines = 1;
    const char *p = utf8_text;

    while(*p) {
        uint32_t cp = ui_utf8_next(&p);
        if(!cp) break;
        if(cp == '\n') {
            if(cur_w > max_w) max_w = cur_w;
            cur_w = 0;
            lines++;
            continue;
        }
        glyph_cache_entry_t *g = get_glyph(cp, size);
        if(g) {
            cur_w += g->advance > 0 ? g->advance : (size / 2);
        } else {
            cur_w += size / 2;
        }
    }
    if(cur_w > max_w) max_w = cur_w;
    if(out_w) *out_w = max_w;
    if(out_h) *out_h = lines * line_h;
}
