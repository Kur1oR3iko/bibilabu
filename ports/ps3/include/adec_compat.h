// SPDX-License-Identifier: GPL-3.0-or-later
// Public firmware wire ABI facts; no Cell SDK or emulator implementation.
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct {uint32_t codec;} wili_adec_type;
typedef struct {uint32_t memory,version_upper,version_lower;} wili_adec_attr;
typedef struct {uint32_t memory_size,address,ppu_priority,spu_priority,stack_size;} wili_adec_resource;
typedef struct {uint32_t function,argument;} wili_adec_callback;
typedef struct {uint32_t upper,lower;} wili_codec_timestamp;
typedef struct {uint32_t address,size;wili_codec_timestamp pts;uint64_t userdata;} wili_adec_au;
typedef struct {
    int32_t handle;uint32_t status,address,size,bsi;
    wili_adec_au au;
} wili_adec_pcm;
typedef struct {uint32_t config,config_words[2],downmix;} wili_aac_param;
typedef struct {
    uint32_t rate,channels,front,front_mono,side,back,lfe,sbr,sbr_factor,valid,config;
} wili_aac_info_prefix;
_Static_assert(sizeof(wili_adec_type)==4 && sizeof(wili_adec_attr)==12,"Adec type/attr ABI");
_Static_assert(sizeof(wili_adec_resource)==20 && sizeof(wili_adec_callback)==8,"Adec resource/callback ABI");
_Static_assert(sizeof(wili_adec_au)==24 && offsetof(wili_adec_au,userdata)==16,"Adec AU ABI");
_Static_assert(sizeof(wili_adec_pcm)==48 && offsetof(wili_adec_pcm,au)==24,"Adec PCM item ABI");
_Static_assert(sizeof(wili_aac_param)==16 && offsetof(wili_aac_param,downmix)==12,"AAC params ABI");
_Static_assert(offsetof(wili_aac_info_prefix,sbr)==28,"AAC BSI prefix ABI");
#define WILI_ADEC_BUSY 0x80610004u
#define WILI_ADEC_EMPTY 0x80610005u
#define WILI_MODULE_M4AAC 0xf01du
#define WILI_CODEC_M4AAC 16u
int32_t wiliAdecQueryAttr(const wili_adec_type*,wili_adec_attr*);
int32_t wiliAdecOpen(const wili_adec_type*,const wili_adec_resource*,const wili_adec_callback*,uint32_t*);
int32_t wiliAdecClose(uint32_t);
int32_t wiliAdecStartSeq(uint32_t,const wili_aac_param*);
int32_t wiliAdecEndSeq(uint32_t);
int32_t wiliAdecDecodeAu(uint32_t,const wili_adec_au*);
int32_t wiliAdecGetPcm(uint32_t,void*);
int32_t wiliAdecGetPcmItem(uint32_t,uint32_t*);

int32_t wiliCellAdecQueryAttr(const wili_adec_type*,wili_adec_attr*);
int32_t wiliCellAdecOpen(const wili_adec_type*,const wili_adec_resource*,const wili_adec_callback*,uint32_t*);
int32_t wiliCellAdecClose(uint32_t);
int32_t wiliCellAdecStartSeq(uint32_t,const wili_aac_param*);
int32_t wiliCellAdecEndSeq(uint32_t);
int32_t wiliCellAdecDecodeAu(uint32_t,const wili_adec_au*);
int32_t wiliCellAdecGetPcm(uint32_t,void*);
int32_t wiliCellAdecGetPcmItem(uint32_t,uint32_t*);
