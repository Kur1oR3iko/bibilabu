// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
int search_osk_open(const char *initial,void (*log)(const char *));
int search_osk_open_text(const char *initial,const char *title,int multiline,void (*log)(const char*));
void search_osk_event(uint64_t event);
int search_osk_active(void);
int search_osk_tick(char *out,unsigned capacity);
void search_osk_abort(void);
