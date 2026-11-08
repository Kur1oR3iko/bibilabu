// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
typedef struct {uint8_t *data;uint32_t capacity;volatile uint32_t head,tail;} wili_compressed_ring;
int wili_compressed_push(wili_compressed_ring*,const void*,uint32_t bytes,uint32_t *offset);
int wili_compressed_pop(wili_compressed_ring*,void*,uint32_t bytes,uint32_t offset);
