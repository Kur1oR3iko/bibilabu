// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE
#include "user_settings.h"
#include "vdec_ex.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static void write_text(const char *path,const char *text){FILE *f=fopen(path,"wb");assert(f);fputs(text,f);fclose(f);}
int main(void)
{
    char dir[]="/tmp/bibi-settings-XXXXXX";assert(mkdtemp(dir) && !chdir(dir));
    assert(wili_settings_load_density("prefs.cfg")==6);
    for(unsigned level=0;level<=6;level++){
        assert(!wili_settings_save_density("prefs.cfg",level));assert(wili_settings_load_density("prefs.cfg")==level);
        write_text("prefs.cfg","partial");assert(wili_settings_load_density("prefs.cfg")==level);
    }
    assert(!wili_settings_save_density("prefs.cfg",0));assert(wili_settings_load_density("prefs.cfg")==0);
    write_text("wiliwili_config.json","{\"logged_out\":true}");assert(wili_settings_load_density("prefs.cfg")==0);
    assert(wili_settings_save_density("prefs.cfg",7)<0 && wili_settings_load_density("prefs.cfg")==0);
    assert(wili_settings_save_density("missing/prefs.cfg",3)<0);
    write_text("prefs.cfg","bibilabu-settings-v1\ndanmaku_density=6\ntrailing");write_text("prefs.cfg.bak","bad");assert(wili_settings_load_density("prefs.cfg")==6);
    wili_avc_sps s={0};s.coded_width=720;s.coded_height=1280;s.max_num_ref_frames=7;s.max_dec_frame_buffering=7;s.max_num_reorder_frames=4;
    wili_vdec_avc_spec spec;assert(!wili_vdec_ex_spec(&s,&spec));assert(spec.size==12 && spec.width==720 && spec.height==1280 && spec.buffers==8 && !spec.disable_deblock);
    s.max_dec_frame_buffering=9;assert(!wili_vdec_ex_spec(&s,&spec) && spec.buffers==10);
    s.max_num_ref_frames=16;assert(wili_vdec_ex_spec(&s,&spec)<0);
    unlink("prefs.cfg");unlink("prefs.cfg.bak");unlink("wiliwili_config.json");assert(!chdir("/") && !rmdir(dir));
    puts("PASS density default6, all levels/zero persistence, atomic roundtrip, backup recovery, logout independence, invalid data and AVC extended-spec dimensions/buffer requirements");
}
