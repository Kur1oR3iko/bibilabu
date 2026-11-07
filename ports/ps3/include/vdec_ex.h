// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "avc_sps.h"
typedef struct {uint32_t size;uint16_t width,height;uint8_t disable_deblock,buffers,reserved[2];} wili_vdec_avc_spec;
typedef struct {uint32_t codec,level,specific_info;} wili_vdec_type_ex;
typedef struct {uint32_t memory;uint8_t depth,reserved[3];uint32_t version_upper,version_lower;} wili_vdec_attr;
typedef struct {uint32_t address,memory,ppu_priority,stack_size,spu_priority,spus,spurs;} wili_vdec_resource_ex;
typedef struct {uint32_t function,argument;} wili_vdec_callback;
_Static_assert(sizeof(wili_vdec_avc_spec)==12 && offsetof(wili_vdec_avc_spec,buffers)==9,"AVC specific-info wire ABI");
_Static_assert(sizeof(wili_vdec_type_ex)==12 && offsetof(wili_vdec_type_ex,specific_info)==8,"VDEC TypeEx wire ABI");
_Static_assert(sizeof(wili_vdec_attr)==16 && sizeof(wili_vdec_resource_ex)==28 && sizeof(wili_vdec_callback)==8,"VDEC extended resource/attribute ABI");
int wili_vdec_ex_spec(const wili_avc_sps *s,wili_vdec_avc_spec *out);
int wili_vdec_ex_available(void (*log)(const char*));
int32_t wiliVdecQueryAttrEx(const wili_vdec_type_ex*,wili_vdec_attr*);
int32_t wiliVdecOpenEx(const wili_vdec_type_ex*,const wili_vdec_resource_ex*,const wili_vdec_callback*,uint32_t*);
