// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only discovery of saved static XMB wallpaper; no VSH hooks or writes.
#include "wallpaper.h"
#include "ui_image.h"
#include <sys/file.h>
#include <sysutil/sysutil.h>
#include <stdio.h>
#include <string.h>
static char selected[256];
static uint64_t checked,mtime,size;
static int exists(const char *path){sysFSStat st;return !sysLv2FsStat(path,&st) && st.st_size>0 && st.st_size<=8*1024*1024;}
static int discover(char out[256])
{
    char current[128]={0},only[256]={0};unsigned profiles=0;
    sysUtilGetSystemParamString(SYSUTIL_SYSTEMPARAM_ID_CURRENT_USERNAME,current,sizeof(current));
    s32 dir=-1;
    if(!sysLv2FsOpenDir("/dev_hdd0/home",&dir)){
        for(unsigned i=0;i<128;i++){
            sysFSDirent entry;memset(&entry,0,sizeof(entry));u64 got=0;
            if(sysLv2FsReadDir(dir,&entry,&got) || !got)break;
            const char *id=entry.d_name;if(strlen(id)!=8 || strspn(id,"0123456789")!=8)continue;
            profiles++;char path[256],name[128]={0};s32 fd=-1;
            snprintf(path,sizeof(path),"/dev_hdd0/home/%s/localusername",id);
            if(!sysLv2FsOpen(path,SYS_O_RDONLY,&fd,0,0,0)){u64 n=0;sysLv2FsRead(fd,name,sizeof(name)-1,&n);sysLv2FsClose(fd);name[strcspn(name,"\r\n")]=0;}
            snprintf(path,sizeof(path),"/dev_hdd0/home/%s/theme/wallpaper.png",id);
            if(exists(path)){
                snprintf(only,sizeof(only),"%s",path);
                if(current[0] && !strcmp(name,current)){snprintf(out,256,"%s",path);sysLv2FsCloseDir(dir);return 1;}
            }
        }
        sysLv2FsCloseDir(dir);
    }
    if(profiles==1 && only[0]){snprintf(out,256,"%s",only);return 1;}
    const char *app="/dev_hdd0/game/WILIPRB01/USRDIR/wallpaper.png";
    if(exists(app)){snprintf(out,256,"%s",app);return 1;}
    for(unsigned i=0;i<8;i++){snprintf(out,256,"/dev_usb00%u/BIBI_WALLPAPER.PNG",i);if(exists(out))return 1;}
    out[0]=0;return 0;
}
void wallpaper_poll(uint64_t now,int force,void (*log)(const char *))
{
    if(!force && checked && now-checked<5000000)return;
    checked=now;
    char path[256]={0};if(!discover(path)){if(force && log)log("WALLPAPER_NOT_FOUND_OR_USER_AMBIGUOUS using_application_background");return;}
    sysFSStat st;if(sysLv2FsStat(path,&st))return;
    if(force || strcmp(path,selected) || mtime!=(uint64_t)st.st_mtime || size!=st.st_size){
        snprintf(selected,sizeof(selected),"%s",path);mtime=st.st_mtime;size=st.st_size;
        ui_image_request_wallpaper(path);
        if(log){char line[320];snprintf(line,sizeof(line),"WALLPAPER_REQUEST path=%s bytes=%u",path,(unsigned)size);log(line);}
    }
}
