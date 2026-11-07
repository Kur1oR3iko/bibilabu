// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream_policy.hpp"
#include <limits>

namespace wiliwili::ps3 {
bool safe_media_url(const std::string& url) {
    const std::size_t start = url.compare(0, 8, "https://") == 0 ? 8 :
                              url.compare(0, 7, "http://") == 0 ? 7 : 0;
    if (!start || start >= url.size()) return false;
    const auto end = url.find_first_of("/?#", start);
    if (end == start) return false;
    const auto host = url.substr(start, end - start);
    if (host.find('@') != std::string::npos) return false;
    for (unsigned char c : url) if (c <= 0x20 || c == 0x7f) return false;
    return true;
}

static bool video_supported(const Stream& v, const Limits& l) {
    if (v.codecid != 7 || !safe_media_url(v.url)) return false;
    if (!v.codecs.empty()) {
        if (v.codecs.size() != 11 || v.codecs.compare(0, 5, "avc1.") != 0) return false;
        unsigned bytes[3] = {};
        for (unsigned i = 0; i < 6; ++i) {
            const char c = v.codecs[5 + i];
            const int hex = c >= '0' && c <= '9' ? c - '0' :
                            c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                            c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (hex < 0) return false;
            bytes[i / 2] = bytes[i / 2] * 16 + static_cast<unsigned>(hex);
        }
        // Metadata rejection is conservative. Actual bit depth, coded size and
        // reference-frame constraints must still be validated from the SPS.
        if (bytes[0] != 66 && bytes[0] != 77 && bytes[0] != 88 && bytes[0] != 100) return false;
        if (!bytes[2] || bytes[2] > 42) return false;
    }
    if (!v.width || !v.height || v.width > l.max_width || v.height > l.max_height) return false;
    if (!v.bandwidth || v.bandwidth > l.max_video_bandwidth) return false;
    if ((v.fps_num == 0) != (v.fps_den == 0)) return false;
    if (v.fps_den && std::uint64_t(v.fps_num) > std::uint64_t(l.max_fps) * v.fps_den) return false;
    return true;
}

static bool audio_supported(const Stream& a, const Limits& l) {
    if (!safe_media_url(a.url) || !a.bandwidth || a.bandwidth > l.max_audio_bandwidth) return false;
    if (a.id != 30216 && a.id != 30232 && a.id != 30280) return false;
    return a.codecs.empty() || a.codecs == "mp4a.40.2"; // AAC-LC only for the first backend.
}

Selection select_dash_streams(const std::vector<Stream>& video,
                              const std::vector<Stream>& audio, const Limits& limits) {
    Selection result;
    // Return original indexes, so the caller keeps all original request headers
    // and backup URLs rather than reconstructing signed CDN URLs.
    const std::size_t max_count = static_cast<std::size_t>(std::numeric_limits<int>::max());
    for (std::size_t i = 0; i < video.size() && i <= max_count; ++i) {
        if (!video_supported(video[i], limits)) continue;
        if (result.video_index < 0) { result.video_index = static_cast<int>(i); continue; }
        const auto& previous = video[static_cast<std::size_t>(result.video_index)];
        const auto area = std::uint64_t(video[i].width) * video[i].height;
        const auto previous_area = std::uint64_t(previous.width) * previous.height;
        if (area > previous_area || (area == previous_area && video[i].bandwidth < previous.bandwidth))
            result.video_index = static_cast<int>(i);
    }
    if (result.video_index < 0) { result.error = SelectionError::no_compatible_video; return result; }
    for (std::size_t i = 0; i < audio.size() && i <= max_count; ++i) {
        if (!audio_supported(audio[i], limits)) continue;
        if (result.audio_index < 0 || audio[i].bandwidth > audio[static_cast<std::size_t>(result.audio_index)].bandwidth)
            result.audio_index = static_cast<int>(i);
    }
    if (result.audio_index < 0 && limits.require_audio) result.error = SelectionError::no_compatible_audio;
    return result;
}
} // namespace wiliwili::ps3
