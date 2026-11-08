// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
// Hysteresis avoids rapid stop/start around a single late packet.
static inline int wili_video_should_buffer(int buffering,uint64_t queued_ms,int64_t ahead_us,int eof)
{
    if(!buffering)return !eof && ((queued_ms<150 && ahead_us<80000) || ahead_us< -200000);
    return !((queued_ms>=2500 || eof) && (ahead_us>=100000 || eof));
}

// Software decode can fall behind with compressed packets still available.
// Gate audio on decoded presentation headroom rather than download EOF.
static inline int wili_software_prefill_ready(unsigned ready_frames,int decode_eof)
{return ready_frames>=6 || decode_eof;}
static inline int wili_software_should_buffer(int buffering,int64_t ahead_us,int decode_eof,unsigned ready_frames)
{
    if(decode_eof)return 0;
    if(!buffering)return ahead_us< -80000;
    // A frame-count escape also permits 60/120fps to refill within ten slots.
    return !(ahead_us>=150000 || (ahead_us>=0 && wili_software_prefill_ready(ready_frames,0)));
}
