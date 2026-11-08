// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#define WILI_BILI_DEFAULT_BVID "BV17x411w7KC"
int wili_bili_probe_prepare(const char*,void (*log)(const char*));
int wili_bili_probe_run(const char *ca,int (*cancel)(void*),void*);
void wili_bili_probe_network_error(void);
unsigned wili_bili_probe_revision(void);
const char *wili_bili_probe_bvid(void);
void wili_bili_probe_line(unsigned,char*,size_t);
