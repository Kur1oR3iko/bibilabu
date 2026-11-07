// SPDX-License-Identifier: GPL-3.0-or-later
#include "search_osk.h"
#include "search_text.h"
#include <sysutil/osk.h>
#include <sysutil/sysutil.h>
#include <sys/memory.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(oskParam)==20 && sizeof(oskInputFieldInfo)==12 && sizeof(oskCallbackReturnParam)==12,"OSK PPU32 ABI");
static int active,done,unloading,unloaded,aborting;
static sys_mem_container_t container;
static uint16_t title[128] __attribute__((aligned(16)));
static uint16_t initial_text[241] __attribute__((aligned(16))),result_text[241] __attribute__((aligned(16)));
static oskCallbackReturnParam result;
static oskParam params __attribute__((aligned(16)));
static oskInputFieldInfo field __attribute__((aligned(16)));
static void (*logger)(const char *);
static void log_rc(const char *what,int rc){if(logger){char s[96];snprintf(s,sizeof(s),"%s rc=%08x",what,(unsigned)rc);logger(s);}}
int search_osk_open_text(const char *initial,const char *caption,int multiline,void (*log)(const char *))
{
    if(active)return -1;
    logger=log;
    memset(initial_text,0,sizeof(initial_text));memset(result_text,0,sizeof(result_text));
    unsigned limit=multiline?240:80;
    if(wili_utf8_to_utf16(initial?initial:"",initial_text,limit+1)<0)initial_text[0]=0;
    if(wili_utf8_to_utf16(caption?caption:"输入文字",title,128)<0)title[0]=0;
    int rc=sysMemContainerCreate(&container,4*1024*1024);log_rc("OSK_CONTAINER",rc);if(rc)return -1;
    memset(&params,0,sizeof(params));
    params.allowedPanels=OSK_PANEL_TYPE_SIMPLIFIED_CHINESE|OSK_PANEL_TYPE_TRADITIONAL_CHINESE|OSK_PANEL_TYPE_ENGLISH|OSK_PANEL_TYPE_ALPHABET|OSK_PANEL_TYPE_NUMERAL;
    params.firstViewPanel=OSK_PANEL_TYPE_SIMPLIFIED_CHINESE;params.prohibitFlags=multiline?0:OSK_PROHIBIT_RETURN;
    field.message=title;field.startText=initial_text;field.maxLength=limit;
    // Register Chinese explicitly instead of depending on the console language.
    log_rc("OSK_INPUT_DEVICE",oskSetInitialInputDevice(OSK_DEVICE_PAD));
    log_rc("OSK_KEY_LAYOUT",oskSetKeyLayoutOption(OSK_10KEY_PANEL|OSK_FULLKEY_PANEL));
    // This API replaces the language mask; separate calls discard Chinese.
    log_rc("OSK_LANGUAGE_MASK",oskAddSupportLanguage(OSK_PANEL_TYPE_SIMPLIFIED_CHINESE|OSK_PANEL_TYPE_TRADITIONAL_CHINESE|OSK_PANEL_TYPE_ENGLISH));
    log_rc("OSK_INITIAL_LAYOUT",oskSetInitialKeyLayout(OSK_INITIAL_SYSTEM_PANEL));
    log_rc("OSK_LAYOUT_MODE",oskSetLayoutMode(OSK_LAYOUTMODE_HORIZONTAL_ALIGN_CENTER|OSK_LAYOUTMODE_VERTICAL_ALIGN_CENTER));
    memset(&result,0,sizeof(result));result.len=limit;result.str=result_text;
    done=unloading=unloaded=aborting=0;
    rc=oskLoadAsync(container,&params,&field);log_rc("OSK_LOAD",rc);
    if((unsigned)rc==0x8002b504u){
        // Keep registered Chinese languages when falling back to system layout.
        params.allowedPanels=OSK_PANEL_TYPE_DEFAULT;params.firstViewPanel=OSK_PANEL_TYPE_DEFAULT;
        rc=oskLoadAsync(container,&params,&field);log_rc("OSK_LOAD_SYSTEM_PANEL_WITH_CHINESE",rc);
    }
    if(rc){sysMemContainerDestroy(container);return -1;}active=1;return 0;
}
int search_osk_open(const char *initial,void (*log)(const char *))
{return search_osk_open_text(initial,"搜索视频 · 可切换中文输入",0,log);}
void search_osk_event(uint64_t event)
{
    if(!active)return;
    if(event==SYSUTIL_OSK_DONE)done=1;
    if(event==SYSUTIL_OSK_UNLOADED)unloaded=1;
}
int search_osk_active(void){return active;}
void search_osk_abort(void){if(active && !aborting){aborting=1;log_rc("OSK_ABORT",oskAbort());}}
int search_osk_tick(char *out,unsigned cap)
{
    if(!active)return 0;
    if(done && !unloading){int rc=oskUnloadAsync(&result);log_rc("OSK_UNLOAD",rc);if(!rc)unloading=1;}
    if(!unloaded)return 0;
    int ok=!aborting && result.res==OSK_OK && wili_utf16_to_utf8(result_text,241,out,cap)>0;
    log_rc("OSK_CONTAINER_RELEASE",sysMemContainerDestroy(container));active=0;
    return ok?1:-1;
}
