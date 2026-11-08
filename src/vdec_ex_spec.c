// SPDX-License-Identifier: GPL-3.0-or-later
#include "vdec_ex.h"
#include "video_geometry.h"
#include <string.h>
int wili_vdec_ex_spec(const wili_avc_sps *s,wili_vdec_avc_spec *out)
{
    if(!s || !out || !wili_video_is_720(s->coded_width,s->coded_height) || (s->coded_width&15) || (s->coded_height&15))return -1;
    unsigned frames=s->max_num_ref_frames;
    if(s->max_dec_frame_buffering>frames)frames=s->max_dec_frame_buffering;
    if(s->max_num_reorder_frames>frames)frames=s->max_num_reorder_frames;
    if(frames>=16)return -1;
    memset(out,0,sizeof(*out));out->size=sizeof(*out);out->width=s->coded_width;out->height=s->coded_height;out->buffers=frames+1;return 0;
}
