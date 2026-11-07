// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_runtime.h"
#include <audio/audio.h>
#include <sysmodule/sysmodule.h>
#include <stdio.h>
static unsigned owners;
static int module_owned;
static void rc_log(void (*log)(const char*),const char *name,s32 rc)
{
    if(log){char line[96];snprintf(line,sizeof(line),"%s rc=%08x",name,(unsigned)rc);log(line);}
}
int ui_audio_runtime_acquire(void (*log)(const char*))
{
    if(owners++){return 0;}
    s32 rc=sysModuleLoad(SYSMODULE_AUDIO);rc_log(log,"UI_AUDIO_MODULE_LOAD",rc);
    if(rc && (u32)rc!=SYSMODULE_ERR_DUPLICATE){owners=0;return -1;}
    module_owned=!rc;
    rc=audioInit();rc_log(log,"UI_AUDIO_INIT",rc);
    if(rc){if(module_owned)sysModuleUnload(SYSMODULE_AUDIO);module_owned=0;owners=0;return -1;}
    return 0;
}
int ui_audio_runtime_release(void (*log)(const char*))
{
    if(!owners)return 0;
    if(--owners)return 0;
    s32 rc=audioQuit();rc_log(log,"UI_AUDIO_QUIT",rc);
    if(rc){owners=1;return -1;}
    if(module_owned)rc_log(log,"UI_AUDIO_MODULE_UNLOAD",sysModuleUnload(SYSMODULE_AUDIO));
    module_owned=0;return 0;
}
