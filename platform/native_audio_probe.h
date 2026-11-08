// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
int wili_audio_prepare(void (*log)(const char*));
int wili_audio_launch(void);
uint32_t wili_audio_clock_samples(void);
void wili_audio_pause(int pause);
int wili_audio_failed(void);
void wili_audio_stop(void);
void wili_audio_status(char *out,unsigned capacity);
unsigned wili_audio_state(void);

int wili_audio_pump(void);
