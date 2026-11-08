// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "network_limits.h"
typedef struct {
    char *body;size_t bytes,capacity;long status,verify_result;int curl_code,overflow,tls_verified;
    uint64_t range_start,range_end,range_total,server_epoch;int has_range;
    char host[256],error[256],set_cookie[1024];
} wili_https_response;
int wili_https_get(const char *url,const char *ca,const char *range,size_t cap,int (*cancel)(void*),void*,wili_https_response*,void (*log)(const char*));
int wili_https_get_cookie(const char *url,const char *ca,const char *range,const char *cookie,size_t cap,int (*cancel)(void*),void*,wili_https_response*,void (*log)(const char*));
void wili_https_response_free(wili_https_response*);
int wili_https_post_form(const char *url,const char *ca,const char *cookie,const char *form,size_t cap,int (*cancel)(void*),void*,wili_https_response*,void (*log)(const char*));
