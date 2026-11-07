// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_motion.h"
#include <math.h>
#define DURATION 180000u
static void update(ui_tween *t,uint64_t now)
{
    float p=now>=t->start?(float)(now-t->start)/DURATION:0;
    if(p>1)p=1;
    float left=1-p;t->value=t->from+(t->to-t->from)*(1-left*left*left);
}
static void target(ui_tween *t,float value,uint64_t now)
{
    if(t->to==value)return;
    t->from=t->value;t->to=value;t->start=now;
}
int ui_motion_step(ui_motion *m,int tab,int selected,unsigned page,uint64_t context,int count,uint64_t now)
{
    if(!m->initialized){m->category.from=m->category.to=m->category.value=(float)tab;m->selection.from=m->selection.to=m->selection.value=(float)selected;m->initialized=1;m->tab=tab;m->page=page;m->context=context;m->count=count;return 0;}
    update(&m->category,now);update(&m->selection,now);
    if(tab!=m->tab){
        int delta=tab-m->tab;if(delta>3)delta-=6;if(delta< -3)delta+=6;
        target(&m->category,m->category.to+(float)delta,now);
    }
    if(tab!=m->tab || page!=m->page || context!=m->context || count!=m->count){
        m->selection.from=m->selection.to=m->selection.value=(float)selected;
    }else target(&m->selection,(float)selected,now);
    m->tab=tab;m->page=page;m->context=context;m->count=count;
    return fabsf(m->category.value-m->category.to)>0.001f || fabsf(m->selection.value-m->selection.to)>0.001f;
}

int ui_loading_redraw(ui_loading_clock *clock,int active,uint64_t now)
{
    if(!active){clock->active=0;return 0;}
    if(!clock->active || now<clock->last || now-clock->last>=33333){
        clock->active=1;clock->last=now;return 1;
    }
    return 0;
}

int ui_buffer_notice_step(ui_buffer_notice *notice,int buffering,uint64_t now)
{
    if(now<notice->last)*notice=(ui_buffer_notice){0};
    if(buffering && !notice->buffering)notice->since=now;
    notice->buffering=buffering;notice->last=now;
    if(buffering && now-notice->since>=750000)notice->hold_until=now+1500000;
    return now<notice->hold_until;
}
