// SPDX-License-Identifier: GPL-3.0-or-later
#include "search_text.h"
int wili_utf16_to_utf8(const uint16_t *src,size_t units,char *dst,size_t cap)
{
    size_t n=0;if(!src || !dst || !cap)return -1;dst[0]=0;
    for(size_t i=0;i<units && src[i];i++){
        uint32_t c=src[i];
        if(c>=0xd800 && c<=0xdbff){if(i+1>=units || src[i+1]<0xdc00 || src[i+1]>0xdfff)return -1;c=0x10000+((c-0xd800)<<10)+(src[++i]-0xdc00);}
        else if(c>=0xdc00 && c<=0xdfff)return -1;
        unsigned k=c<0x80?1:c<0x800?2:c<0x10000?3:4;
        if(n+k>=cap)return -1;
        if(k==1)dst[n++]=(char)c;
        else {
            dst[n++]=(char)((k==2?0xc0:k==3?0xe0:0xf0)|(c>>(6*(k-1))));
            for(unsigned j=k-1;j;j--)dst[n++]=(char)(0x80|((c>>(6*(j-1)))&63));
        }
        dst[n]=0;
    }
    return (int)n;
}
int wili_utf8_to_utf16(const char *src,uint16_t *dst,size_t cap)
{
    if(!src || !dst || !cap)return -1;
    size_t n=0;dst[0]=0;
    while(*src){
        unsigned char c=(unsigned char)*src++;uint32_t cp=c;unsigned rest=0;
        if(c>=0xf0 && c<=0xf4){rest=3;cp=c&7;}
        else if(c>=0xe0 && c<=0xef){rest=2;cp=c&15;}
        else if(c>=0xc2 && c<=0xdf){rest=1;cp=c&31;}
        else if(c>=0x80)return -1;
        unsigned original=rest;
        while(rest--){unsigned char next=(unsigned char)*src++;if((next&0xc0)!=0x80)return -1;cp=(cp<<6)|(next&63);}
        if((original==1 && cp<0x80)||(original==2 && cp<0x800)||(original==3 && cp<0x10000)||cp>0x10ffff||(cp>=0xd800 && cp<=0xdfff))return -1;
        if(n+(cp>=0x10000?2:1)>=cap)return -1;
        if(cp>=0x10000){cp-=0x10000;dst[n++]=(uint16_t)(0xd800+(cp>>10));dst[n++]=(uint16_t)(0xdc00+(cp&1023));}
        else dst[n++]=(uint16_t)cp;
        dst[n]=0;
    }
    return (int)n;
}
int wili_url_encode(const char *src,char *dst,size_t cap)
{
    static const char hex[]="0123456789ABCDEF";size_t n=0;
    if(!src || !dst || !cap)return -1;
    while(*src){unsigned char c=(unsigned char)*src++;int safe=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~';
        if(n+(safe?1:3)>=cap)return -1;
        if(safe)dst[n++]=(char)c;else{dst[n++]='%';dst[n++]=hex[c>>4];dst[n++]=hex[c&15];}
    }
    dst[n]=0;return (int)n;
}
