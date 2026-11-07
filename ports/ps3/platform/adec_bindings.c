// SPDX-License-Identifier: GPL-3.0-or-later
#include "adec_bindings.h"
#include <stdio.h>

#define IMPORT_NAMES(X,P) X(P,QueryAttr) X(P,Open) X(P,Close) X(P,StartSeq) X(P,EndSeq) X(P,DecodeAu) X(P,GetPcm) X(P,GetPcmItem)
#define DECLARE(P,N) extern volatile uint32_t P##N##_stub; extern const uint32_t __##P##N;
IMPORT_NAMES(DECLARE,wiliAdec)
IMPORT_NAMES(DECLARE,wiliCellAdec)
typedef struct {const char *name;volatile uint32_t *slot;const uint32_t *trampoline;} binding;
#define ROW(P,N) {#N,&P##N##_stub,&__##P##N},
static const binding legacy[]={IMPORT_NAMES(ROW,wiliAdec)};
static const binding cell[]={IMPORT_NAMES(ROW,wiliCellAdec)};
#define API(P) {P##QueryAttr,P##Open,P##Close,P##StartSeq,P##EndSeq,P##DecodeAu,P##GetPcm,P##GetPcmItem}
static const wili_adec_api legacy_api=API(wiliAdec),cell_api=API(wiliCellAdec);

static int validate(const char *library,const binding *entries,void (*log)(const char*))
{
    unsigned i,valid=0;
    for(i=0;i<8;i++){
        uint32_t target=*entries[i].slot;
        uint32_t own=(uint32_t)(uintptr_t)entries[i].trampoline;
        uint32_t code=0,toc=0;
        // The linked (unresolved) slot points at our instruction trampoline,
        // not an OPD. Never interpret those instruction words as an address.
        int resolved=target!=own && target>=0x10000 && target<0xe0000000u && !(target&3);
        if(resolved){
            const volatile uint32_t *opd=(const void*)(uintptr_t)target;
            code=opd[0];toc=opd[1];
            resolved=code>=0x10000 && code<0xe0000000u && !(code&3) &&
                     toc>=0x10000 && toc<0xe0000000u && !(toc&3) && code!=own;
        }
        char line[192];
        snprintf(line,sizeof(line),"AAC_BINDING library=%s function=%s resolved=%d slot=%08x own=%08x code=%08x toc=%08x",library,entries[i].name,resolved,target,own,code,toc);log(line);
        if(resolved)valid++;
    }
    return valid==8;
}
const wili_adec_api *wili_resolve_adec(void (*log)(const char*))
{
    int legacy_ok=validate("libadec",legacy,log);
    int cell_ok=validate("cellAdec",cell,log);
    if(cell_ok){log("AAC_BINDINGS_READY library=cellAdec");return &cell_api;}
    if(legacy_ok){log("AAC_BINDINGS_READY library=libadec");return &legacy_api;}
    log("AAC_BINDINGS_UNRESOLVED refusing_decoder_call");return NULL;
}
