// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#define UI_CHIME_FRAMES 3840u
void ui_chime_generate(float *mono);
void ui_chime_mix(float *stereo,unsigned frames,const float *mono,unsigned offset);
