// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual runtime selector without executing any firmware call.
#define _GNU_SOURCE
#include "adec_bindings.h"
#include <assert.h>
#include <sys/mman.h>
#include <stdio.h>

#define NAMES(X,P) X(P,QueryAttr) X(P,Open) X(P,Close) X(P,StartSeq) X(P,EndSeq) X(P,DecodeAu) X(P,GetPcm) X(P,GetPcmItem)
#define SLOT(P,N) volatile uint32_t P##N##_stub; const uint32_t __##P##N=0x60000000;
NAMES(SLOT,wiliAdec)
NAMES(SLOT,wiliCellAdec)
static unsigned calls,logs;
#define MOCK(P) \
int32_t P##QueryAttr(const wili_adec_type*t,wili_adec_attr*a){(void)t;(void)a;calls++;return -99;} \
int32_t P##Open(const wili_adec_type*t,const wili_adec_resource*r,const wili_adec_callback*c,uint32_t*h){(void)t;(void)r;(void)c;(void)h;calls++;return -99;} \
int32_t P##Close(uint32_t h){(void)h;calls++;return -99;} \
int32_t P##StartSeq(uint32_t h,const wili_aac_param*p){(void)h;(void)p;calls++;return -99;} \
int32_t P##EndSeq(uint32_t h){(void)h;calls++;return -99;} \
int32_t P##DecodeAu(uint32_t h,const wili_adec_au*a){(void)h;(void)a;calls++;return -99;} \
int32_t P##GetPcm(uint32_t h,void*p){(void)h;(void)p;calls++;return -99;} \
int32_t P##GetPcmItem(uint32_t h,uint32_t*p){(void)h;(void)p;calls++;return -99;}
MOCK(wiliAdec)
MOCK(wiliCellAdec)
static void log_line(const char *text){assert(text && *text);logs++;}
#define UNRESOLVED(P,N) P##N##_stub=(uint32_t)(uintptr_t)&__##P##N;
#define RESOLVED(P,N) P##N##_stub=(uint32_t)(uintptr_t)opd;
int main(void)
{
    NAMES(UNRESOLVED,wiliAdec) NAMES(UNRESOLVED,wiliCellAdec)
    assert(!wili_resolve_adec(log_line));assert(calls==0 && logs==17);
    uint32_t *opd=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    assert(opd!=MAP_FAILED && (uintptr_t)opd<0xe0000000u);
    opd[0]=0x10000;opd[1]=0x20000;
    NAMES(RESOLVED,wiliCellAdec)
    const wili_adec_api *api=wili_resolve_adec(log_line);assert(api && api->query==wiliCellAdecQueryAttr && calls==0);
    UNRESOLVED(wiliCellAdec,QueryAttr)
    assert(!wili_resolve_adec(log_line) && calls==0); // partial library never callable
    NAMES(RESOLVED,wiliAdec)
    api=wili_resolve_adec(log_line);assert(api && api->query==wiliAdecQueryAttr && calls==0);
    NAMES(RESOLVED,wiliCellAdec)
    api=wili_resolve_adec(log_line);assert(api && api->query==wiliCellAdecQueryAttr && calls==0);
    opd[0]=0x7c0802a6;opd[1]=0xf8010010; // instruction words cannot be a compact OPD
    assert(!wili_resolve_adec(log_line) && calls==0);
    NAMES(UNRESOLVED,wiliAdec) NAMES(UNRESOLVED,wiliCellAdec)
    wiliAdecQueryAttr_stub=0;wiliCellAdecQueryAttr_stub=0xffffffffu;
    assert(!wili_resolve_adec(log_line) && calls==0);
    assert(!munmap(opd,4096));
    puts("PASS: unresolved/partial/null/invalid OPDs rejected; both aliases selected only when complete; zero firmware calls");return 0;
}
