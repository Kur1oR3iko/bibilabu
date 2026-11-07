// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

// General H.264 SPS reader for the PS3 native decoder path. It parses the
// fields that decide decoder configuration and presentation, and rejects
// bitstream properties outside the bounded native AVC path. All bit reads
// are bounded; Exp-Golomb leading zeros are capped well below the shift width.
typedef struct {
    unsigned profile, constraint_flags, level, sps_id;
    unsigned chroma_format_idc, separate_colour_plane;
    unsigned bit_depth_luma, bit_depth_chroma;
    unsigned log2_max_frame_num_minus4, pic_order_cnt_type, log2_max_poc_lsb_minus4;
    unsigned max_num_ref_frames, gaps_in_frame_num_value_allowed;
    unsigned frame_mbs_only, mb_adaptive_frame_field, direct_8x8_inference;
    unsigned frame_cropping, crop_left, crop_right, crop_top, crop_bottom;
    unsigned vui_present, aspect_ratio_idc, sar_width, sar_height;
    unsigned matrix_coefficients,video_full_range,hrd_present;
    unsigned timing_present, num_units_in_tick, time_scale, fixed_frame_rate;
    unsigned bitstream_restriction, max_num_reorder_frames, max_dec_frame_buffering;
    unsigned coded_width, coded_height, display_width, display_height;
    unsigned rbsp_bytes, bits_consumed;
} wili_avc_sps;

// Accepts 8-bit 4:2:0 progressive Baseline/Main/High SPS for landscape HD and portrait 720x1280.
// Returns -1 for malformed, truncated or unsupported parameter sets.
int wili_avc_sps_parse(const uint8_t *nal, size_t size, wili_avc_sps *out);
// Remove emulation-prevention bytes from a NAL (payload-only, keeps nal header).
size_t wili_avc_rbsp(const uint8_t *nal, size_t size, uint8_t *out, size_t cap);

// CellVdec accepts AVC levels through 4.2, not raw 5.x level_idc values.
// Re-label over-declared streams only within the bounded 720p/DPB envelope.
int wili_avc_ps3_level(const wili_avc_sps *s,unsigned *decoder_level);
int wili_avc_normalize_annexb(uint8_t *data,size_t size,const wili_avc_sps *expected,unsigned level);
