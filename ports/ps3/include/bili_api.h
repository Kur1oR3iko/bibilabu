// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct {int code,keys_ready;char mixin[33];} wili_bili_nav;
typedef struct {int code;uint64_t aid,cid;unsigned duration,page_duration;char bvid[16],title[256];} wili_bili_video;
typedef struct {
    int code,dash_ready;unsigned returned_quality,width,height,video_id,audio_id,advertised_fps;
    unsigned dash_duration_ms,available_mask;
    unsigned video_entries,avc_entries,hevc_entries,other_codec_entries;
    unsigned rejected_geometry,rejected_frame_rate,rejected_profile,rejected_bitrate;
    char video_codecs[128],audio_codecs[128];
    char video_url[4096],audio_url[4096];
    unsigned accept_qualities[8];
    char accept_descriptions[8][32];
    unsigned accept_count;
} wili_bili_play;
int wili_bili_valid_bvid(const char*);
int wili_bili_parse_nav(const char*,size_t,wili_bili_nav*);
int wili_bili_parse_video(const char*,size_t,wili_bili_video*);
int wili_bili_parse_play(const char*,size_t,wili_bili_play*);
int wili_bili_mixin(const char *image_key,const char *sub_key,char out[33]);
int wili_bili_play_query(const char *bvid,uint64_t cid,uint64_t epoch,const char mixin[33],char *out,size_t cap);
int wili_bili_play_query_qn(const char *bvid,uint64_t cid,uint64_t epoch,const char mixin[33],unsigned qn,char *out,size_t cap);

int wili_bili_parse_play_limit(const char *body,size_t n,unsigned max_qn,wili_bili_play *out);
struct json_object;
struct json_object *wili_bili_json_parse(const char *body,size_t bytes);
int wili_bili_sign_sorted(const char *query,const char mixin[33],char *out,size_t cap);
