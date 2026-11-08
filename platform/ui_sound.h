// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
int ui_sound_start(void (*log)(const char*));
void ui_sound_click(void);
void ui_sound_set_muted(int muted);
// 0: closed. Nonzero: retain resources for process teardown.
int ui_sound_stop(void);
