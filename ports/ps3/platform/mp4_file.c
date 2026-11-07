// SPDX-License-Identifier: GPL-3.0-or-later
#include "mp4_file.h"
#include "avc_test_stream.h"
#include <sys/file.h>
#include <stdio.h>
#include <string.h>
static int read_at(void *arg,uint64_t at,void *data,size_t size)
{
#ifdef WILI_NETWORK_TEST
    wili_mp4_file *f=arg;return f->opened ? wili_http_range_read(&f->network,at,data,size) : -1;
#else
    wili_mp4_file *f=arg;uint64_t position=0,done=0;
    if(!f->opened || at>f->size || size>f->size-at || sysLv2FsLSeek64(f->fd,at,SEEK_SET,&position) || position!=at)return -1;
    while(done<size){
        uint64_t got=0;if(sysLv2FsRead(f->fd,(uint8_t*)data+done,size-done,&got) || !got || got>size-done)return -1;done+=got;
    }
    return 0;
#endif
}
int wili_mp4_file_open(wili_mp4_file *f,const char *name,uint64_t size,uint32_t crc,int audio,void (*log)(const char*))
{
#ifdef WILI_NETWORK_TEST
    (void)crc;memset(f,0,sizeof(*f));f->size=size;
    if(wili_network_open(&f->network,name,size,log))return -1;
    f->opened=1;
    if(wili_fmp4_open(&f->reader,read_at,f,size,audio)){log("HTTP_MP4_INIT_FAILED");return -1;}
    log("HTTP_MP4_SOURCE_READY no_USB_media_fallback=1");return 0;
#else
    memset(f,0,sizeof(*f));f->fd=-1;char path[96],line[160];unsigned port;
    snprintf(path,sizeof(path),"/dev_usb000/%s",name);
    for(port=0;port<8;port++){path[10]=(char)('0'+port);if(!sysLv2FsOpen(path,SYS_O_RDONLY,&f->fd,0,0,0)){f->opened=1;break;}}
    if(!f->opened){log("MP4_FILE_NOT_FOUND");return -1;}
    sysFSStat stat;if(sysLv2FsFStat(f->fd,&stat) || stat.st_size!=size){log("MP4_FILE_SIZE_MISMATCH");return -1;}f->size=size;
    uint64_t at=0;uint32_t state=~0u;
    // Diagnostic fixture integrity check: bounded scratch, never whole-file allocation.
    while(at<size){size_t n=size-at>WILI_MP4_META ? WILI_MP4_META : (size_t)(size-at);
        if(read_at(f,at,f->reader.metadata,n)){log("MP4_FILE_CHECK_READ_FAILED");return -1;}
        state=wili_crc32_update(state,f->reader.metadata,n);at+=n;
    }
    if(~state!=crc){log("MP4_FILE_CHECKSUM_MISMATCH");return -1;}
    if(wili_fmp4_open(&f->reader,read_at,f,size,audio)){log("MP4_INIT_REJECTED");return -1;}
    snprintf(line,sizeof(line),"MP4_SOURCE_READY file=%s timescale=%u parser_bytes=%u sample_limit=%u",name,f->reader.timescale,(unsigned)sizeof(f->reader),WILI_MP4_SAMPLES);log(line);return 0;
#endif
}
void wili_mp4_file_close(wili_mp4_file *f)
{
#ifdef WILI_NETWORK_TEST
    if(f->opened){wili_network_close(&f->network);f->opened=0;}
#else
    if(f->opened){sysLv2FsClose(f->fd);f->opened=0;}
#endif
}
