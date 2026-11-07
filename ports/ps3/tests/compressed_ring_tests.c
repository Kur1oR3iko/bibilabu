// SPDX-License-Identifier: GPL-3.0-or-later
#include "compressed_ring.h"
#include "video_buffer_policy.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <stdio.h>
static uint8_t arena[4096];
static wili_compressed_ring ring={arena,sizeof(arena),UINT32_MAX-2000,UINT32_MAX-2000};
static struct {uint32_t offset,n;volatile unsigned ready;} slots[16];
static void *produce(void *arg)
{
    (void)arg;uint8_t data[701];
    for(unsigned i=0;i<50000;i++){
        unsigned n=i%sizeof(data)+1,slot=i%16;while(__sync_add_and_fetch(&slots[slot].ready,0))sched_yield();
        for(unsigned k=0;k<n;k++)data[k]=(uint8_t)(i+k*7);
        int rc;do{rc=wili_compressed_push(&ring,data,n,&slots[slot].offset);if(!rc)sched_yield();}while(!rc);assert(rc==1);
        slots[slot].n=n;__sync_lock_test_and_set(&slots[slot].ready,1);
    }
    return NULL;
}
int main(void)
{
    pthread_t worker;assert(!pthread_create(&worker,NULL,produce,NULL));uint8_t data[701];
    for(unsigned i=0;i<50000;i++){
        unsigned slot=i%16;while(!__sync_add_and_fetch(&slots[slot].ready,0))sched_yield();
        assert(!wili_compressed_pop(&ring,data,slots[slot].n,slots[slot].offset));
        for(unsigned k=0;k<slots[slot].n;k++)assert(data[k]==(uint8_t)(i+k*7));
        __sync_lock_test_and_set(&slots[slot].ready,0);
    }
    assert(!pthread_join(worker,NULL));assert(ring.head==ring.tail);
    uint32_t offset;assert(wili_compressed_push(&ring,arena,sizeof(arena)+1,&offset)<0);
    assert(wili_compressed_pop(&ring,data,1,ring.tail)<0);
    puts("PASS 50000 concurrent variable packets: arena wrap, 32-bit counter wrap, backpressure and ownership");
    // Reproduce the observed starvation: zero packets and video behind audio.
    int buffering=wili_video_should_buffer(0,0,-30000,0);assert(buffering);
    assert(wili_video_should_buffer(buffering,500,300000,0));
    assert(wili_video_should_buffer(buffering,1800,-200000,0));
    assert(wili_video_should_buffer(buffering,1800,200000,0));
    assert(!wili_video_should_buffer(buffering,3000,200000,0));
    assert(!wili_video_should_buffer(0,1000,200000,0));
    assert(!wili_video_should_buffer(1,0,0,1));
    puts("PASS video starvation holds the audio clock; hysteresis waits for packets AND decoded headroom; EOF releases");
}
