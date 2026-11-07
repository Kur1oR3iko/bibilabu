// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "http_range.h"
int wili_network_open(wili_http_range*,const char *name,uint64_t size,void (*log)(const char*));
void wili_network_close(wili_http_range*);
void wili_network_shutdown(void (*log)(const char*));
int wili_network_prepare(void (*log)(const char*));

int wili_network_link_ready(void);
