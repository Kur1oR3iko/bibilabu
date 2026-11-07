// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "network_limits.h"
#define WILI_DM_EVENTS 16384u
#define WILI_DM_LANES 64u
#define WILI_DM_RESPONSE_BYTES WILI_HTTPS_MAX_RESPONSE_BYTES
#define WILI_DM_ATLAS_WIDTH 2048u
#define WILI_DM_ATLAS_HEIGHT 1280u
#define WILI_DM_SLOT_WIDTH 1024u
#define WILI_DM_SLOT_HEIGHT 40u
#define WILI_DM_ATLAS_BYTES (WILI_DM_ATLAS_WIDTH*WILI_DM_ATLAS_HEIGHT*4u)
typedef struct {uint32_t ms,color,mode,size,order,truncated;char text[512];} wili_dm_event;
// Zero-initialize before parsing. Release with wili_dm_segment_free.
typedef struct {unsigned index,count,unsupported,overflow,truncated;wili_dm_event *events;} wili_dm_segment;
typedef struct {wili_dm_event event;unsigned serial,width;uint32_t end;} wili_dm_lane;
typedef struct {
    wili_dm_lane lanes[WILI_DM_LANES];unsigned segment,cursor,serial,initialized,density,spawn_ready;
    uint32_t last_ms,last_spawn;unsigned expired,filtered;
} wili_dm_player;
typedef struct {float x,y,w,h,u0,v0,u1,v1;} wili_dm_quad;
typedef struct {uint32_t atlas_offset;unsigned count;wili_dm_quad quads[WILI_DM_LANES];} wili_dm_draw;
int wili_dm_parse(const void *data,size_t bytes,unsigned segment,wili_dm_segment *out);
void wili_dm_segment_free(wili_dm_segment *segment);
int wili_dm_segment_copy(wili_dm_segment *dst,const wili_dm_segment *src);
unsigned wili_dm_limit(unsigned density);
void wili_dm_step(wili_dm_player*,const wili_dm_segment*,uint32_t ms,unsigned density);
void wili_dm_geometry(const wili_dm_player*,uint32_t ms,wili_dm_draw*);
