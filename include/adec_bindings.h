// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "adec_compat.h"
typedef struct {
    int32_t (*query)(const wili_adec_type*,wili_adec_attr*);
    int32_t (*open)(const wili_adec_type*,const wili_adec_resource*,const wili_adec_callback*,uint32_t*);
    int32_t (*close)(uint32_t);
    int32_t (*start)(uint32_t,const wili_aac_param*);
    int32_t (*end)(uint32_t);
    int32_t (*decode)(uint32_t,const wili_adec_au*);
    int32_t (*pcm)(uint32_t,void*);
    int32_t (*item)(uint32_t,uint32_t*);
} wili_adec_api;
const wili_adec_api *wili_resolve_adec(void (*log)(const char*));
