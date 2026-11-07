// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
typedef struct {void *data;size_t bytes;} wili_media_memory;
int wili_media_memory_alloc(wili_media_memory *block,size_t bytes,int megabyte_pages);
int wili_media_memory_free(wili_media_memory *block);
