// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Native AVC allocations support the existing landscape HD path and portrait
// 720p. These are application memory bounds, not claimed firmware limits.
static inline int wili_video_is_720(unsigned w,unsigned h)
{return w && h && ((w<=1280 && h<=720) || (w<=720 && h<=1280));}
static inline int wili_video_geometry(unsigned w,unsigned h)
{return w && h && ((w<=1920 && h<=1088) || (w<=720 && h<=1280));}
