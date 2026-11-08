// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#define WILI_AAC_MAX_FRAMES 512
typedef struct {uint32_t offset,size;} wili_aac_frame;
typedef struct {unsigned count;wili_aac_frame frames[WILI_AAC_MAX_FRAMES];} wili_aac_stream;
// Diagnostic contract: MPEG4 AAC-LC, ADTS/no CRC, stereo 48kHz, one raw block.
int wili_parse_test_aac(const uint8_t*,size_t,wili_aac_stream*);
