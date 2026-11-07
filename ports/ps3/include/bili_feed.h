// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>

#define BILI_FEED_MAX_ITEMS 16

typedef struct {
    char bvid[16];
    char title[256];
    char pic_url[512];
    char author[64];
    uint32_t view_count;
    uint32_t duration_sec;
    int progress_sec;
    uint64_t viewed_at;
    uint64_t resource_id;
    int is_folder;
} bili_feed_item_t;

typedef enum {
    FEED_TAB_RECOMMEND = 0,
    FEED_TAB_POPULAR   = 1,
    FEED_TAB_HISTORY   = 2,
    FEED_TAB_FAVORITE  = 3,
    FEED_TAB_SEARCH    = 4,
    FEED_TAB_USER      = 5,
    FEED_TAB_COUNT     = 6
} bili_feed_tab_t;

typedef struct {
    bili_feed_tab_t current_tab;
    bili_feed_item_t items[BILI_FEED_MAX_ITEMS];
    int item_count;
    int loading;
    int network_error;
    unsigned page,req_page;
    uint64_t next_max,next_view_at,req_max,req_view_at;
    int has_more;
    uint64_t favorite_id,req_favorite_id;
    char favorite_title[128];
    char status_msg[128];
    char search_keyword[256];
    const char *cookie;
    const char *ca_path;
    void (*log)(const char*);

    // Async thread state
    volatile int running;
    volatile int req_pending;
    bili_feed_tab_t req_tab;
    char req_search[256];
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    uint64_t worker_thread;
#else
    uint64_t worker_thread;
#endif
} bili_feed_state_t;

int bili_feed_init(bili_feed_state_t *st, const char *cookie, const char *ca_path, void (*log)(const char*));
void bili_feed_shutdown(bili_feed_state_t *st);
void bili_feed_cancel(void);
int bili_feed_load_tab(bili_feed_state_t *st, bili_feed_tab_t tab);
int bili_feed_search(bili_feed_state_t *st, const char *keyword);
int bili_feed_copy_items_safe(bili_feed_state_t *st, bili_feed_item_t *dst, int max_count, int *out_count);

int bili_feed_pump(bili_feed_state_t *st);
void bili_feed_set_cookie(const char *cookie);

int bili_feed_next_page(bili_feed_state_t *st);
int bili_feed_parse_response(bili_feed_state_t *st,const char *body,int search);

int bili_feed_open_folder(bili_feed_state_t *st,int index);
int bili_feed_refresh(bili_feed_state_t *st);
