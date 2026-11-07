// SPDX-License-Identifier: GPL-3.0-or-later
#include "vdec_ex.h"
#include <stdio.h>
extern volatile uint32_t wiliVdecQueryAttrEx_stub,wiliVdecOpenEx_stub;
extern const uint32_t __wiliVdecQueryAttrEx,__wiliVdecOpenEx;
int wili_vdec_ex_available(void (*log)(const char*))
{
    volatile uint32_t *slots[]={&wiliVdecQueryAttrEx_stub,&wiliVdecOpenEx_stub};
    const uint32_t *tramp[]={&__wiliVdecQueryAttrEx,&__wiliVdecOpenEx};
    const char *names[]={"QueryAttrEx","OpenEx"};int all=1;
    for(unsigned i=0;i<2;i++){
        uint32_t target=*slots[i],own=(uint32_t)(uintptr_t)tramp[i],code=0,toc=0;
        int ok=target!=own && target>=0x10000 && target<0xe0000000u && !(target&3);
        if(ok){const volatile uint32_t *opd=(const void*)(uintptr_t)target;code=opd[0];toc=opd[1];
            ok=code!=own && code>=0x10000 && code<0xe0000000u && !(code&3) && toc>=0x10000 && toc<0xe0000000u && !(toc&3);}
        char line[160];snprintf(line,sizeof(line),"VDEC_EX_BINDING function=%s resolved=%d code=%08x toc=%08x",names[i],ok,code,toc);log(line);all=all && ok;
    }
    return all;
}
