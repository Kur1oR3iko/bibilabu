// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#define WILI_DEFAULT_DANMAKU_DENSITY 6u
#define WILI_SETTINGS_PATH "/dev_hdd0/tmp/bibilabu_settings.cfg"
unsigned wili_settings_load_density(const char *path);
int wili_settings_save_density(const char *path,unsigned density);
