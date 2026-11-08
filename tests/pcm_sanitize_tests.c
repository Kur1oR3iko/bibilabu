// SPDX-License-Identifier: GPL-3.0-or-later
// Regression for the 1.0.1 hardware freeze: a single NaN or >1 float sample
// makes wili_pcm_ring_write reject the batch and kill the player. Host
// reproduction with the exact logged counters (build/media-probe/pcm_repro.c)
// showed capacity was NOT the cause at held=30955.
#include "pcm_ring.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    // 8 stereo frames = 16 channel values (ring_write strides by slots=2).
    float v[16]={0.1f,-0.2f, 0.3f,-0.3f, NAN,0.0f, 1.0001f,0.0f,
                 -1.0001f,0.0f, INFINITY,0.0f, -INFINITY,0.0f, 0.5f,-0.5f};
    uint32_t bits=0;
    unsigned bad=wili_pcm_sanitize(v,16,&bits);
    assert(bad==5);
    assert(v[0]==0.1f && v[1]==-0.2f && v[14]==0.5f && v[15]==-0.5f);
    assert(v[4]==0.0f);                  // NaN -> silence
    assert(v[6]==1.0f && v[8]==-1.0f);  // clamp
    assert(v[10]==1.0f && v[12]==-1.0f);// +/-Inf -> clamp
    assert(bits!=0);                     // first offending bit pattern reported
    float ring[16*2];memset(ring,0,sizeof(ring));
    assert(!wili_pcm_ring_write(ring,16,0,0,v,8,2));
    float raw[16]={0};raw[2]=NAN;
    assert(wili_pcm_ring_write(ring,16,0,0,raw,8,2)<0); // unsanitized still rejected
    assert(wili_pcm_sanitize(NULL,16,NULL)==0);
    assert(wili_pcm_sanitize(v,0,NULL)==0);
    // Real 1.0.1 logged state: capacity is fine at held=30955, n=1115.
    float *big=malloc(32768u*2*sizeof(float)),*in=malloc(1115u*2*sizeof(float));
    assert(big && in);
    unsigned i;for(i=0;i<1115u*2;i++)in[i]=0.0f;
    assert(!wili_pcm_ring_write(big,32768u,377835u,346880u,in,1115u,2));
    free(big);free(in);
    puts("PASS PCM sanitize: NaN/Inf/over-range repaired, first bits reported, ring accepts, logged counters have capacity");
    return 0;
}
