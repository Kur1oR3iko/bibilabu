// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_motion.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void)
{
    ui_motion m={0};assert(!ui_motion_step(&m,0,2,1,0,16,1000000));
    assert(ui_motion_step(&m,0,3,1,0,16,1000001));float previous=m.selection.value;
    for(unsigned t=1;t<=9;t++){
        ui_motion_step(&m,0,3,1,0,16,1000001+t*20000);
        assert(m.selection.value>=previous && m.selection.value<=3);previous=m.selection.value;
    }
    assert(fabsf(m.selection.value-3)<0.0001f);
    ui_motion_step(&m,0,8,1,0,16,1200000);ui_motion_step(&m,0,8,1,0,16,1250000);
    float interrupted=m.selection.value;ui_motion_step(&m,0,1,1,0,16,1250000);
    assert(fabsf(m.selection.value-interrupted)<0.0001f);
    ui_motion_step(&m,0,1,1,0,16,1430000);assert(m.selection.value==1);
    ui_motion_step(&m,5,0,1,0,16,1500000);assert(m.category.to==-1);
    ui_motion_step(&m,5,0,1,0,16,1680000);assert(m.category.value==-1);
    ui_motion_step(&m,5,7,2,42,16,1700000);assert(m.selection.value==7);
    ui_loading_clock loading={0};unsigned redraws=0,steps=0;uint64_t last_present=0;
    for(uint64_t now=1000000;now<2000000;now+=1000){
        // Cached flips keep advancing independently of loading composition.
        if(now-last_present>=16667)last_present=now;
        if(ui_loading_redraw(&loading,1,now)){redraws++;steps|=1u<<((now/80000)%12);}
    }
    assert(redraws>=28 && redraws<=31 && steps==4095);
    assert(!ui_loading_redraw(&loading,0,2000000));
    assert(ui_loading_redraw(&loading,1,2000001));
    assert(ui_loading_redraw(&loading,1,10)); // tolerate a reset clock
    puts("PASS loading animation redraws despite continuous 60Hz cached flips, mode restart and clock reset");
    ui_buffer_notice notice={0};
    // The real log had 24 pauses of roughly 100-190ms, not sustained stalls.
    for(unsigned burst=0;burst<24;burst++)for(unsigned tick=0;tick<1000;tick+=10)
        assert(!ui_buffer_notice_step(&notice,tick<190,3000000ULL+burst*1000000ULL+tick*1000));
    notice=(ui_buffer_notice){0};assert(!ui_buffer_notice_step(&notice,1,1000000));
    assert(!ui_buffer_notice_step(&notice,1,1749999));assert(ui_buffer_notice_step(&notice,1,1750000));
    assert(ui_buffer_notice_step(&notice,0,1800000));assert(ui_buffer_notice_step(&notice,0,3249999));
    assert(!ui_buffer_notice_step(&notice,0,3250000));assert(!ui_buffer_notice_step(&notice,1,10));
    puts("PASS 24 brief real-log-style stalls never summon controls; sustained stall debounce/hold and clock reset");
    puts("PASS animation interruption continuity, monotonic ease-out, category wrap and page reset");
}
