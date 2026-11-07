#include "detail_layout.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    assert(DETAIL_VIDEO_Y+DETAIL_VIDEO_H<DETAIL_BODY_Y);
    assert(DETAIL_BODY_Y+DETAIL_BODY_H<720 && detail_content_height(6)<=720);
    unsigned focus=0;focus=detail_navigate(focus,DETAIL_NAV_DOWN,8,6);assert(focus==1);
    focus=detail_navigate(focus,DETAIL_NAV_DOWN,8,6);assert(focus==6);
    focus=detail_navigate(focus,DETAIL_NAV_DOWN,8,6);assert(focus==DETAIL_COMMENT_FOCUS);
    assert(detail_navigate(focus,DETAIL_NAV_RIGHT,8,6)==DETAIL_RELATED_FOCUS);
    assert(detail_navigate(DETAIL_RELATED_FOCUS,DETAIL_NAV_LEFT,8,6)==0);
    detail_scroll s={0};float before=0;
    for(unsigned i=0;i<=7;i++){detail_scroll_step(&s,20,6,i*33333);assert(s.value>=before && s.value<=440);before=s.value;}
    assert(s.value==440 && s.target==440);
    detail_scroll_step(&s,7,6,250000);assert(s.value==440); // related navigation preserves comments
    detail_scroll_step(&s,1,6,300000);assert(s.value==440 && s.target==0);
    detail_scroll_step(&s,1,6,350000);before=s.value;
    detail_scroll_step(&s,18,6,350000);assert(s.value==before); // interrupt without jumping
    detail_scroll_step(&s,18,6,600000);assert(s.value==280);
    detail_scroll_step(&s,15,0,700000);detail_scroll_step(&s,15,0,1000000);assert(s.value<=40);
    for(unsigned f=0;f<21;f++)for(unsigned d=0;d<4;d++)assert(detail_navigate(f,d,8,6)<=20);
    puts("PASS fixed video geometry, independent bounded comment viewport, navigation, smooth scrolling and interruption continuity");
}
