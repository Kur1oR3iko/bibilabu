// SPDX-License-Identifier: GPL-3.0-or-later
#include "detail_layout.h"
#include <math.h>
unsigned detail_content_height(unsigned comments)
{if(comments>6)comments=6;return DETAIL_COMMENT_Y+(comments?comments:1)*DETAIL_COMMENT_ROW;}
int detail_scroll_step(detail_scroll *s,unsigned focus,unsigned comments,uint64_t now)
{
    float old=s->value;if(!s->initialized){s->value=s->from=s->target=0;s->begin=now;s->initialized=1;}
    float t=now>=s->begin?(float)(now-s->begin)/180000.0f:0;if(t>1)t=1;
    float left=1-t;s->value=s->from+(s->target-s->from)*(1-left*left*left);
    if(fabsf(s->value-s->target)<0.05f)s->value=s->target;
    float target=s->target;unsigned top=0,bottom=0;
    if(focus>=1 && focus<=5){top=focus==1?0:28;bottom=72;}
    else if(focus==6){top=74;bottom=100;}
    else if(focus>=DETAIL_COMMENT_FOCUS){unsigned row=focus-DETAIL_COMMENT_FOCUS;if(row>5)row=5;top=DETAIL_COMMENT_Y+row*DETAIL_COMMENT_ROW;bottom=top+DETAIL_COMMENT_ROW;}
    if(bottom){if(top<target)target=(float)top;else if(bottom>target+DETAIL_BODY_H)target=(float)(bottom-DETAIL_BODY_H);}
    float maximum=(float)(detail_content_height(comments)-DETAIL_BODY_H);
    if(target<0)target=0;
    if(target>maximum)target=maximum;
    if(target!=s->target){s->from=s->value;s->target=target;s->begin=now;}
    return old!=s->value || s->value!=s->target;
}
unsigned detail_navigate(unsigned focus,unsigned dir,unsigned related,unsigned comments)
{
    if(related>8)related=8;
    if(comments>6)comments=6;
    if(focus>=DETAIL_COMMENT_FOCUS){
        if(dir==DETAIL_NAV_UP)return focus>DETAIL_COMMENT_FOCUS?focus-1:6;
        if(dir==DETAIL_NAV_DOWN)return comments && focus+1<DETAIL_COMMENT_FOCUS+comments?focus+1:focus;
        if(dir==DETAIL_NAV_LEFT)return 1;
        if(dir==DETAIL_NAV_RIGHT)return related?DETAIL_RELATED_FOCUS:0;
    }else if(focus>=DETAIL_RELATED_FOCUS){
        if(dir==DETAIL_NAV_UP)return focus>DETAIL_RELATED_FOCUS?focus-1:0;
        if(dir==DETAIL_NAV_DOWN)return related && focus+1<DETAIL_RELATED_FOCUS+related?focus+1:focus;
        if(dir==DETAIL_NAV_LEFT)return 0;
    }else if(!focus){
        if(dir==DETAIL_NAV_DOWN)return 1;
        if(dir==DETAIL_NAV_RIGHT)return related?DETAIL_RELATED_FOCUS:0;
    }else if(focus==6){
        if(dir==DETAIL_NAV_UP || dir==DETAIL_NAV_LEFT)return 1;
        if(dir==DETAIL_NAV_DOWN)return DETAIL_COMMENT_FOCUS;
        if(dir==DETAIL_NAV_RIGHT)return related?DETAIL_RELATED_FOCUS:0;
    }else{
        if(dir==DETAIL_NAV_LEFT)return focus>1?focus-1:0;
        if(dir==DETAIL_NAV_RIGHT)return focus<5?focus+1:related?DETAIL_RELATED_FOCUS:focus;
        if(dir==DETAIL_NAV_UP)return 0;
        if(dir==DETAIL_NAV_DOWN)return 6;
    }
    return focus;
}
