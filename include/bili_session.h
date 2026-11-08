// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "http_range.h"
#include "mp4_dash.h"
#include "bili_api.h"
#include "https_request.h"

// Runtime Bilibili playback session: fresh signed URLs -> CDN total sizes ->
// real MP4 initialization/index parsing -> bounded player configuration.
// Signed URLs stay in memory only; they are never written to logs or evidence.
typedef struct {
    void (*log)(const char*);
    char bvid[16],video_host[128],audio_host[128],title[256];
    uint64_t aid,cid,video_size,audio_size,duration_ms;
    unsigned view_duration,page_duration,dash_duration_ms,returned_qn,video_id,audio_id,advertised_fps;
    unsigned width,height,quality_mask;
    uint64_t seek_ms;
    char video_codecs[128],audio_codecs[128];
    char video_url[4096],audio_url[4096];
    wili_http_range video_range,audio_range;
    wili_mp4_dash video,audio;
    unsigned accept_qualities[8];
    char accept_descriptions[8][32];
    unsigned accept_count;
    char lines[8][128];
    unsigned revision;
    int network_error;
    int ready,video_opened,audio_opened,network_ready;
} wili_bili_session;

int wili_bili_cdn_probe_ok(const wili_https_response*,int curl_rc);
int wili_bili_session_open(wili_bili_session*,const char *bvid,const char *ca,
                           int (*cancel)(void*),void*,void (*log)(const char*));
int wili_bili_session_open_ex(wili_bili_session*,const char *bvid,unsigned qn,const char *cookie,const char *ca,
                              int (*cancel)(void*),void*,void (*log)(const char*));
void wili_bili_session_close(wili_bili_session*,void (*log)(const char*));
unsigned wili_bili_session_revision(const wili_bili_session*);
void wili_bili_session_line(const wili_bili_session*,unsigned row,char*,size_t);
