// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#define WILI_DEFAULT_DANMAKU_DENSITY 6u
#define WILI_DEFAULT_QUALITY_QN 64u
#define WILI_DEFAULT_VOLUME 80u
#define WILI_SETTINGS_PATH "/dev_hdd0/tmp/bibilabu_settings.cfg"

typedef struct {
    unsigned danmaku_density;   /* 0..6 */
    unsigned default_qn;        /* 16 / 32 / 64 */
    unsigned volume;            /* 0..100 */
} wili_settings;

void wili_settings_defaults(wili_settings *s);
wili_settings wili_settings_load(const char *path);
int wili_settings_save(const char *path, const wili_settings *s);
/* Legacy helpers kept for the danmaku worker; they preserve the other fields. */
unsigned wili_settings_load_density(const char *path);
int wili_settings_save_density(const char *path, unsigned density);
