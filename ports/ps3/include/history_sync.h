// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
int history_sync_init(const char *ca,void (*log)(const char*));
void history_sync_account(const char *cookie);
void history_sync_begin(uint64_t aid,uint64_t cid,uint64_t duration_ms,const char *cookie);
void history_sync_tick(uint64_t position_ms,int paused,int ended,uint64_t now_us);
void history_sync_end(uint64_t position_ms,int ended);
void history_sync_close_begin(unsigned drain_ms);
void history_sync_shutdown(unsigned drain_ms);
// 0 idle, 1 pending, 2 acknowledged, -1 failed/awaiting retry.
int history_sync_status(void);
