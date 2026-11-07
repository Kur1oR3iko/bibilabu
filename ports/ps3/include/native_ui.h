// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bili_feed.h"
#include "qr_login.h"
#include "bili_detail.h"
#include "detail_layout.h"
#define UI_WIDTH 1280u
#define UI_HEIGHT 720u
void native_ui_background(uint32_t *fb);
void native_ui_feed(uint32_t *fb,const uint32_t *background,const bili_feed_state_t *feed,
                    const wili_qr_login *qr,int selected,int tab_focus,const char *message);
void native_ui_player(uint32_t *fb,const char *title,unsigned quality,uint64_t elapsed,
                      uint64_t duration,unsigned state,int quality_menu,int quality_selected,unsigned quality_mask);

int native_ui_animate(const bili_feed_state_t *feed,int selected,uint64_t now);

#define UI_SEARCH_KEYS "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_"
void native_ui_keyboard(uint32_t *fb,const char *value,int selected,int lowercase);

void native_ui_exit(uint32_t *fb,int choice);

void native_ui_alpha_target(int enabled);
void native_ui_release_player_cache(void);
void native_ui_dm_density(unsigned density);
void native_ui_detail(uint32_t *fb,const bili_detail_state*,const char *fallback_title,unsigned focus,int comments,int creator,unsigned density,uint64_t elapsed,uint64_t duration,unsigned state);
void native_ui_confirm(uint32_t *fb,const char *title,const char *body,int choice);
void native_ui_comment_view(uint32_t *fb,const char *author,const char *message);
void native_ui_reset_detail_scroll(void);
int native_ui_detail_animate(unsigned focus,unsigned comments,int creator,uint64_t now);
unsigned native_ui_detail_scroll_offset(void);
void native_ui_focus_comments(unsigned comments,uint64_t now,int immediate);

void native_ui_frost_exit(uint32_t *fb);
void native_ui_loading(uint32_t *fb,const uint32_t *background,uint64_t now,int canceling);
