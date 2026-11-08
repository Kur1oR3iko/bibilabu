// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fmp4_stream.h"
#ifdef WILI_NETWORK_TEST
#include "network_source.h"
#endif
typedef struct {int fd,opened;uint64_t size;wili_fmp4 reader;
#ifdef WILI_NETWORK_TEST
    wili_http_range network;
#endif
} wili_mp4_file;
int wili_mp4_file_open(wili_mp4_file*,const char*,uint64_t size,uint32_t crc,int audio,void (*log)(const char*));
void wili_mp4_file_close(wili_mp4_file*);
