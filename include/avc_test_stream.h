// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

#define WILI_TEST_MAX_AUS 256
typedef struct {
    uint32_t offset, size;
} wili_test_au;
typedef struct {
    unsigned coded_width, coded_height, display_width, display_height;
    unsigned profile, level, reference_frames, count;
    wili_test_au aus[WILI_TEST_MAX_AUS];
} wili_test_stream;

// Deliberately limited diagnostic input: progressive baseline AVC, one slice
// per AUD-delimited access unit. Not a general MP4 or H.264 demuxer.
int wili_parse_test_avc(const uint8_t *data, size_t size, wili_test_stream *out);
uint32_t wili_test_crc32(const uint8_t *data, size_t size);
int wili_parse_avc_sps(const uint8_t *nal,size_t size,wili_test_stream *out);
uint32_t wili_crc32_update(uint32_t state,const uint8_t*,size_t);
