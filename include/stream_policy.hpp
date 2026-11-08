// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bibilabu::ps3 {

// Metadata from wiliwili's DASH response. Hardware configuration must still
// inspect the actual AVC SPS; display width/height are not coded dimensions.
struct Stream {
    int id = 0;
    int codecid = 0;
    unsigned width = 0, height = 0;
    std::uint64_t bandwidth = 0;
    unsigned fps_num = 0, fps_den = 0; // 0/0 means unknown, not zero fps.
    std::string codecs;
    std::string url;
};

struct Limits {
    unsigned max_width = 1280, max_height = 720;
    unsigned max_fps = 30;
    std::uint64_t max_video_bandwidth = 6000000;
    std::uint64_t max_audio_bandwidth = 256000;
    bool require_audio = true;
};

enum class SelectionError { none, no_compatible_video, no_compatible_audio };
struct Selection {
    int video_index = -1, audio_index = -1;
    SelectionError error = SelectionError::none;
    // A successful metadata choice never implies validated codec profile,
    // reference count, coded size, or working firmware decoder support.
    bool requires_avc_bitstream_probe = true;
    bool ok() const { return error == SelectionError::none && video_index >= 0; }
};

bool safe_media_url(const std::string& url);
Selection select_dash_streams(const std::vector<Stream>& video,
                              const std::vector<Stream>& audio,
                              const Limits& limits = {});
} // namespace bibilabu::ps3
