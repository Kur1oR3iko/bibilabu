// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L
#include "user_settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__PPU__) || defined(__CELLOS_LV2__)
#include <sys/file.h>
#else
#include <unistd.h>
#endif
static int fs_read_all(const char *path, char **out_buf, size_t *out_sz)
{
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    int fd = -1;
    if(sysLv2FsOpen(path, SYS_O_RDONLY, &fd, 0, 0, 0) != 0) return -1;
    sysFSStat st;
    if(sysLv2FsFStat(fd, &st) != 0 || st.st_size == 0 || st.st_size > 65536) {
        sysLv2FsClose(fd);
        return -1;
    }
    char *buf = (char*)malloc((size_t)st.st_size + 1);
    if(!buf) { sysLv2FsClose(fd); return -1; }
    u64 got = 0;
    if(sysLv2FsRead(fd, buf, st.st_size, &got) != 0 || got != st.st_size) {
        free(buf); sysLv2FsClose(fd); return -1;
    }
    sysLv2FsClose(fd);
    buf[got] = 0;
    *out_buf = buf;
    *out_sz = (size_t)got;
    return 0;
#else
    FILE *f = fopen(path, "rb");
    if(!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(sz <= 0 || sz > 65536) { fclose(f); return -1; }
    char *buf = (char*)malloc(sz + 1);
    if(!buf) { fclose(f); return -1; }
    size_t n = fread(buf, 1, sz, f);
    fclose(f);
    buf[n] = 0;
    *out_buf = buf;
    *out_sz = n;
    return 0;
#endif
}

static int fs_write_all(const char *path,const char *data,size_t sz)
{
    char temp[320];snprintf(temp,sizeof(temp),"%s.tmp",path);
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    int fd=-1;if(sysLv2FsOpen(temp,SYS_O_WRONLY|SYS_O_CREAT|SYS_O_TRUNC,&fd,0600,0,0))return -1;
    u64 done=0;int rc=0;
    while(done<sz){u64 amount=0;rc=sysLv2FsWrite(fd,data+done,sz-done,&amount);if(rc || !amount){rc=-1;break;}done+=amount;}
    if(!rc)rc=sysLv2FsFsync(fd);
    sysLv2FsClose(fd);
    if(rc){sysLv2FsUnlink(temp);return -1;}
    rc=sysLv2FsRename(temp,path);
    if(rc){
        char old[320];sysFSStat st;snprintf(old,sizeof(old),"%s.old",path);
        if(sysLv2FsStat(path,&st))return -1;
        sysLv2FsUnlink(old);if(sysLv2FsRename(path,old))return -1;
        rc=sysLv2FsRename(temp,path);
        if(rc)sysLv2FsRename(old,path);else sysLv2FsUnlink(old);
    }
    return rc? -1:0;
#else
    FILE *f=fopen(temp,"wb");if(!f)return -1;
    int rc=fwrite(data,1,sz,f)!=sz;
    if(fflush(f) || fsync(fileno(f)))rc=1;
    if(fclose(f))rc=1;
    if(!rc)rc=rename(temp,path);
    if(rc)unlink(temp);
    return rc?-1:0;
#endif
}

static int valid_qn(unsigned qn){return qn==16 || qn==32 || qn==64;}
void wili_settings_defaults(wili_settings *s)
{
    if(!s)return;
    s->danmaku_density=WILI_DEFAULT_DANMAKU_DENSITY;
    s->default_qn=WILI_DEFAULT_QUALITY_QN;
    s->volume=WILI_DEFAULT_VOLUME;
}
/* Both formats are validated by re-rendering the canonical text and comparing
   byte for byte: no free-form sscanf result is ever trusted. */
static int parse_settings(const char *data,size_t n,wili_settings *out)
{
    wili_settings s;wili_settings_defaults(&s);
    char canonical[192];unsigned density=7,qn=0,volume=101;int length;
    if(sscanf(data,"bibilabu-settings-v2\ndanmaku_density=%u\ndefault_qn=%u\nvolume=%u",
              &density,&qn,&volume)==3 &&
       density<=6 && valid_qn(qn) && volume<=100){
        length=snprintf(canonical,sizeof(canonical),
            "bibilabu-settings-v2\ndanmaku_density=%u\ndefault_qn=%u\nvolume=%u\n",density,qn,volume);
        if(length>0 && (size_t)length==n && !memcmp(data,canonical,n)){
            s.danmaku_density=density;s.default_qn=qn;s.volume=volume;*out=s;return 0;
        }
    }
    density=7;
    if(sscanf(data,"bibilabu-settings-v1\ndanmaku_density=%u",&density)==1 && density<=6){
        length=snprintf(canonical,sizeof(canonical),"bibilabu-settings-v1\ndanmaku_density=%u\n",density);
        if(length>0 && (size_t)length==n && !memcmp(data,canonical,n)){
            s.danmaku_density=density;*out=s;return 0;
        }
    }
    return -1;
}
wili_settings wili_settings_load(const char *path)
{
    wili_settings s;wili_settings_defaults(&s);
    if(!path || strlen(path)>240)return s;
    for(unsigned i=0;i<2;i++){
        char file[256];snprintf(file,sizeof(file),"%s%s",path,i?".bak":"");
        char *data=NULL;size_t n=0;if(fs_read_all(file,&data,&n))continue;
        wili_settings parsed;int ok=!parse_settings(data,n,&parsed);free(data);
        if(ok)return parsed;
    }
    return s;
}
int wili_settings_save(const char *path,const wili_settings *s)
{
    if(!path || strlen(path)>240 || !s || s->danmaku_density>6 || !valid_qn(s->default_qn) || s->volume>100)return -1;
    char content[192],backup[256];
    int n=snprintf(content,sizeof(content),
        "bibilabu-settings-v2\ndanmaku_density=%u\ndefault_qn=%u\nvolume=%u\n",
        s->danmaku_density,s->default_qn,s->volume);
    if(n<=0 || (size_t)n>=sizeof(content) || fs_write_all(path,content,(size_t)n))return -1;
    snprintf(backup,sizeof(backup),"%s.bak",path);
    return fs_write_all(backup,content,(size_t)n);
}
unsigned wili_settings_load_density(const char *path){return wili_settings_load(path).danmaku_density;}
int wili_settings_save_density(const char *path,unsigned density)
{
    if(density>6)return -1;
    wili_settings s=wili_settings_load(path);s.danmaku_density=density;
    return wili_settings_save(path,&s);
}
