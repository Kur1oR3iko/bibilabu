// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
int wili_utf16_to_utf8(const uint16_t *src,size_t units,char *dst,size_t cap);
int wili_utf8_to_utf16(const char *src,uint16_t *dst,size_t cap);
int wili_url_encode(const char *src,char *dst,size_t cap);
