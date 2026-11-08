#include "ui_chime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static float mono[UI_CHIME_FRAMES],whole[UI_CHIME_FRAMES*2],chunks[UI_CHIME_FRAMES*2];
int main(void)
{
    ui_chime_generate(mono);ui_chime_mix(whole,UI_CHIME_FRAMES,mono,0);
    for(unsigned at=0;at<UI_CHIME_FRAMES;at+=256)ui_chime_mix(chunks+at*2,256,mono,at);
    assert(!memcmp(whole,chunks,sizeof(whole)));double late=0;
    for(unsigned i=0;i<UI_CHIME_FRAMES;i++){assert(isfinite(mono[i]) && fabsf(mono[i])<=0.35f);if(i>1728)late+=fabsf(mono[i]);}
    assert(late>1.0 && fabsf(mono[UI_CHIME_FRAMES-1])<0.00001f);
    puts("PASS chime retains phase and second note across 256-frame blocks, bounded amplitude and fade");
}
