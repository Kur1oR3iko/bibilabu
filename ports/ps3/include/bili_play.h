// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bili_session.h"
// Runtime session shared with the diagnostic page; opened by the app before
// wili_video_start. The player owns decoders, input pools and the audio clock.
wili_bili_session *wili_bili_play_session(void);
unsigned wili_bili_play_revision(void);
