// SPDX-License-Identifier: GPL-3.0-or-later
#include "pcm_ring.h"
#include <string.h>
unsigned wili_pcm_sanitize(float *samples,unsigned count,uint32_t *first_bad_bits)
{
    if(!samples || !count)return 0;
    if(first_bad_bits)*first_bad_bits=0;
    unsigned i,bad=0;
    for(i=0;i<count;i++){
        float v=samples[i];
        if(v!=v || v>1.0f || v<-1.0f){
            uint32_t bits;memcpy(&bits,&v,sizeof(bits));
            if(first_bad_bits && !bad)*first_bad_bits=bits;
            bad++;
            if(v!=v)samples[i]=0.0f;
            else samples[i]=v>1.0f ? 1.0f : -1.0f;
        }
    }
    return bad;
}
int wili_pcm_ring_write(float *ring,unsigned cap,uint32_t produced,uint32_t consumed,const float *in,unsigned n,unsigned slots)
{
    if(!ring || !in || !cap || slots<2 || slots>8 || produced<consumed || produced-consumed>cap || n>cap-(produced-consumed))return -1;
    unsigned i;
    for(i=0;i<n;i++){
        float left=in[i*slots],right=in[i*slots+1];
        if(left!=left || right!=right || left>1 || left< -1 || right>1 || right< -1)return -1;
        unsigned at=(produced+i)%cap;ring[at*2]=left;ring[at*2+1]=right;
    }
    return 0;
}
int wili_pcm_ring_read(const float *ring,unsigned cap,uint32_t queued,uint32_t produced,float *out,unsigned n)
{
    if(!ring || !out || !cap || queued>produced || produced-queued>cap || n>produced-queued)return -1;
    unsigned i;
    for(i=0;i<n;i++){unsigned at=(queued+i)%cap;out[i*2]=ring[at*2];out[i*2+1]=ring[at*2+1];}
    return 0;
}
