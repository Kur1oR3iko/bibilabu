// SPDX-License-Identifier: GPL-3.0-or-later
#include "pcm_ring.h"
#include <assert.h>
#include <stdio.h>
static float value(unsigned n){return (float)((int)(n%257)-128)/256.0f;}
int main(void)
{
    float ring[4096*2],input[512*8],output[128*2];
    unsigned produced=0,consumed=0,queued=0,iteration=0,pauses=0;
    while(consumed<100000){
        unsigned free=4096-(produced-consumed),n=100000-produced;
        if(n>512)n=512;
        if(n && free>=n){
            unsigned i;for(i=0;i<n;i++){input[i*8]=value(produced+i);input[i*8+1]=-value(produced+i);}
            assert(!wili_pcm_ring_write(ring,4096,produced,consumed,input,n,8));produced+=n;
        }
        if(iteration++%37==0){queued=consumed;pauses++;} // discard output queue; source must remain
        n=produced-queued;if(n>128)n=128;
        if(n){
            assert(!wili_pcm_ring_read(ring,4096,queued,produced,output,n));
            unsigned i;for(i=0;i<n;i++){assert(output[i*2]==value(queued+i));assert(output[i*2+1]==-value(queued+i));}
            queued+=n;
        }
        if(produced==100000)consumed=queued;
        else if(queued-consumed>1024)consumed=queued-1024;
        assert(produced-consumed<=4096);
    }
    assert(pauses>20);
    assert(wili_pcm_ring_read(ring,4096,100000,100000,output,1)<0);
    assert(wili_pcm_ring_write(ring,4096,4096,0,input,1,2)<0);
    assert(wili_pcm_ring_read(ring,4096,100001,100000,output,0)<0);
    puts("PASS PCM ring: 100000 source frames, wrap-around, queued-data retention, repeated pause/resume rewinds and capacity guards");return 0;
}
