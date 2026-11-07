// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream_policy.hpp"
#include <iostream>
#include <stdexcept>
using namespace wiliwili::ps3;
static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static Stream video(unsigned width=1280, unsigned height=720, int codec=7) {
    Stream s; s.id=64; s.codecid=codec; s.width=width; s.height=height;
    s.bandwidth=1800000; s.url="https://media.example/video.m4s"; return s;
}
static Stream audio(int id=30280) {
    Stream s; s.id=id; s.bandwidth=192000; s.codecs="mp4a.40.2";
    s.url="https://media.example/audio.m4s"; return s;
}
int main() {
    try {
        auto result=select_dash_streams({video(1920,1080),video(1280,720,12),video()}, {audio()});
        check(result.ok() && result.video_index==2,"Must choose AVC 720p, not HEVC or 1080p");
        check(result.requires_avc_bitstream_probe,"Metadata must not bypass SPS validation");
        check(!select_dash_streams({video(1280,720,13)}, {audio()}).ok(),"AV1 must be rejected");
        auto fast=video(); fast.fps_num=60000; fast.fps_den=1001;
        check(!select_dash_streams({fast}, {audio()}).ok(),"60fps must be rejected");
        fast.fps_num=30000;
        check(select_dash_streams({fast}, {audio()}).ok(),"29.97fps should fit");
        fast.fps_den=0;
        check(!select_dash_streams({fast}, {audio()}).ok(),"Malformed FPS must be rejected");
        check(select_dash_streams({video()},{}).error==SelectionError::no_compatible_audio,"No silent fallback by default");
        Limits silent; silent.require_audio=false;
        check(select_dash_streams({video()},{},silent).ok(),"Explicit silent playback should be permitted");
        check(!select_dash_streams({video()}, {audio(30250)}).ok(),"Dolby stream must not be selected as AAC");
        auto bad=video(); bad.url="https://host/video\r\nCookie: secret";
        check(!select_dash_streams({bad},{audio()}).ok(),"Control characters in URLs must be rejected");
        check(!safe_media_url("https://user:password@host/file"),"URL credentials must be rejected");
        auto lower=video();lower.bandwidth=1200000;
        check(select_dash_streams({video(),lower},{audio()}).video_index==1,"Prefer lower bandwidth at equal dimensions");
        auto unsupported=audio();unsupported.codecs="ec-3";
        check(!select_dash_streams({video()},{unsupported}).ok(),"Codec contradicting AAC ID must be rejected");
        auto high10=video();high10.codecs="avc1.6e0029";
        check(!select_dash_streams({high10},{audio()}).ok(),"High10 metadata must be rejected");
        auto avc=video();avc.codecs="avc1.640028";
        check(select_dash_streams({avc},{audio()}).ok(),"High profile level 4.0 metadata should pass to SPS probing");
        avc.codecs="avc1.nothex";
        check(!select_dash_streams({avc},{audio()}).ok(),"Malformed AVC metadata must be rejected");
        std::cout<<"PS3 stream policy checks passed\n";
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
