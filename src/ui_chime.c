// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_chime.h"
#include <math.h>
void ui_chime_generate(float *mono)
{
    for(unsigned i=0;i<UI_CHIME_FRAMES;i++){
        float y=0;
        if(i<1728){float e=(1728.0f-i)/1728.0f,a=i<96?i/96.0f:1.0f;
            y+=0.20f*a*e*e*sinf(i*(6.283185307f*1046.5f/48000.0f));}
        if(i>=1056){unsigned at=i-1056;float e=(2784.0f-at)/2784.0f,a=at<96?at/96.0f:1.0f;
            y+=0.15f*a*e*e*sinf(at*(6.283185307f*1479.0f/48000.0f));}
        mono[i]=y;
    }
}
void ui_chime_mix(float *dst,unsigned frames,const float *mono,unsigned offset)
{
    for(unsigned i=0;i<frames && offset+i<UI_CHIME_FRAMES;i++){
        float y=dst[i*2]+mono[offset+i];if(y>0.35f)y=0.35f;if(y< -0.35f)y=-0.35f;
        dst[i*2]=dst[i*2+1]=y;
    }
}
