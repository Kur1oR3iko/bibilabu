// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
int ui_audio_runtime_acquire(void (*log)(const char*));
int ui_audio_runtime_release(void (*log)(const char*));
