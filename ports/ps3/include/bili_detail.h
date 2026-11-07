// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bili_feed.h"
#define BILI_DETAIL_VIDEOS 8
#define BILI_DETAIL_COMMENTS 6
typedef struct {uint64_t id,mid;char author[64],message[768];unsigned likes;} bili_comment;
typedef struct {
    char bvid[16],title[256],description[1024],up_name[128],up_face[512],status[160],comment_status[128],creator_status[128];
    uint64_t aid,cid,mid;unsigned views,likes,replies,fans;
    int busy,ready,following,liked,comments_more,creator_ready;
    unsigned comment_page,creator_page,creator_total,related_page,related_pages;
    int creator_more;
    bili_feed_item_t related[BILI_DETAIL_VIDEOS],creator[BILI_DETAIL_VIDEOS];int related_count,creator_count;
    bili_comment comments[BILI_DETAIL_COMMENTS];int comment_count;
} bili_detail_state;
enum {BILI_DETAIL_COMMENTS_NEXT=2,BILI_DETAIL_COMMENTS_FIRST=3,BILI_DETAIL_CREATOR=4,BILI_DETAIL_FOLLOW=5,BILI_DETAIL_LIKE=6,BILI_DETAIL_COMMENT=7,BILI_DETAIL_CREATOR_NEXT=8,BILI_DETAIL_CREATOR_PREV=9,BILI_DETAIL_RELATED_NEXT=10,BILI_DETAIL_RELATED_PREV=11};
int bili_detail_init(const char *ca,void (*log)(const char*));
void bili_detail_open(const char *bvid,const char *cookie);
void bili_detail_cancel(void);
void bili_detail_shutdown(void);
void bili_detail_allow_network(int allow);
int bili_detail_pump(bili_detail_state*);
// Write actions are invoked only after an explicit user confirmation in the UI.
int bili_detail_action(int action,const char *message,uint64_t reply_to);
int bili_detail_cookie_value(const char *cookie,const char *name,char *out,size_t cap);
int bili_detail_parse_view(const char*,size_t,bili_detail_state*);
int bili_detail_parse_comments(const char*,size_t,bili_detail_state*);
