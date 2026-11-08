// SPDX-License-Identifier: GPL-3.0-or-later
// Producers never wait for storage. One writer owns all file descriptors.
#include "runtime_log.h"
#include <sys/file.h>
#include <sys/mutex.h>
#include <sys/thread.h>
#include <sys/systime.h>
#include <stdio.h>
#include <string.h>
#define LOG_SLOTS 256u
#define LOG_LINE 384u
static char queue[LOG_SLOTS][LOG_LINE], paths[3][128];
static unsigned head, tail, dropped, bytes[3];
static int fds[3]={-1,-1,-1};
static volatile unsigned ready;
static volatile unsigned active,writer_done;
static u64 log_epoch,log_frequency;
static sys_mutex_t mutex;
static sys_ppu_thread_t thread;

static int rotate(unsigned i)
{
    char prev[144];
    if(fds[i]>=0){sysLv2FsFsync(fds[i]);sysLv2FsClose(fds[i]);fds[i]=-1;}
    snprintf(prev,sizeof(prev),"%s.previous",paths[i]);
    sysFSStat st;
    if(!sysLv2FsStat(paths[i],&st)){
        sysLv2FsUnlink(prev);
        if(sysLv2FsRename(paths[i],prev))return -1;
    }
    bytes[i]=0;
    return sysLv2FsOpen(paths[i],SYS_O_WRONLY|SYS_O_CREAT|SYS_O_TRUNC,&fds[i],0666,0,0);
}
static u64 log_time(void)
{
    u64 ticks;__asm__ volatile("mftb %0":"=r"(ticks));
    u64 delta=ticks-log_epoch;
    return log_frequency?(delta/log_frequency)*1000000ULL+(delta%log_frequency)*1000000ULL/log_frequency:0;
}
static void write_batch(const char *data,size_t n)
{
    const unsigned order[3]={1,2,0};
    for(unsigned slot=0;slot<3;slot++){
        unsigned i=order[slot];if(fds[i]<0)continue;
        if(bytes[i]>4*1024*1024 && rotate(i))continue;
        u64 done=0;
        while(done<n){u64 amount=0;if(sysLv2FsWrite(fds[i],data+done,n-done,&amount)||!amount)break;done+=amount;}
        bytes[i]+=(unsigned)done;
    }
}
static void writer(void *arg)
{
    (void)arg;u64 synced=log_time();
    for(;;){
        char batch[16384];size_t used=0;unsigned lost=0;
        sysMutexLock(mutex,0);
        while(head!=tail && used+LOG_LINE+1<sizeof(batch)){
            size_t n=strlen(queue[head]);memcpy(batch+used,queue[head],n);used+=n;batch[used++]='\n';head=(head+1)%LOG_SLOTS;
        }
        lost=dropped;dropped=0;
        sysMutexUnlock(mutex);
        if(lost)used+=(size_t)snprintf(batch+used,sizeof(batch)-used,"LOG_QUEUE_DROPPED count=%u\n",lost);
        if(used)write_batch(batch,used);
        else {
            if(!__sync_add_and_fetch(&active,0))break;
            sysUsleep(20000);
        }
        if(log_time()-synced>=1000000){for(unsigned i=0;i<3;i++)if(fds[i]>=0)sysLv2FsFsync(fds[i]);synced=log_time();}
    }
    unsigned i;for(i=0;i<3;i++)if(fds[i]>=0){sysLv2FsFsync(fds[i]);sysLv2FsClose(fds[i]);fds[i]=-1;}
    __sync_lock_test_and_set(&writer_done,1);sysThreadExit(0);
}
int runtime_log_init(void)
{
    __asm__ volatile("mftb %0":"=r"(log_epoch));log_frequency=sysGetTimebaseFrequency();
    sys_mutex_attr_t attr;sysMutexAttrInitialize(attr);
    if(sysMutexCreate(&mutex,&attr))return -1;
    snprintf(paths[1],128,"/dev_hdd0/tmp/BIBI_PLAY.LOG");
    snprintf(paths[2],128,"/dev_hdd0/game/WILIPRB01/USRDIR/BIBI_PLAY.LOG");
    rotate(1);rotate(2);
    unsigned port;
    for(port=0;port<8;port++){
        snprintf(paths[0],128,"/dev_usb00%u/BIBI_PLAY.LOG",port);
        if(!rotate(0))break;
    }
    active=1;writer_done=0;
    if(sysThreadCreate(&thread,writer,NULL,1200,32768,THREAD_JOINABLE,"bibi-log")){
        active=0;unsigned i;for(i=0;i<3;i++)if(fds[i]>=0){sysLv2FsClose(fds[i]);fds[i]=-1;}
        sysMutexDestroy(mutex);return -1;
    }
    ready=1;return 0;
}
void runtime_log_write(const char *line)
{
    if(!__sync_add_and_fetch(&ready,0) || !line)return;
    sysMutexLock(mutex,0);
    unsigned next=(tail+1)%LOG_SLOTS;
    if(next==head)dropped++;
    else {snprintf(queue[tail],LOG_LINE,"[%llu ms] %s",(unsigned long long)(log_time()/1000),line);tail=next;}
    sysMutexUnlock(mutex);
}
void runtime_log_shutdown(void)
{
    if(!ready)return;
    __sync_lock_test_and_set(&ready,0);
    __sync_lock_test_and_set(&active,0);
    u64 begin=log_time();unsigned waits=0;
    while(!__sync_add_and_fetch(&writer_done,0) && log_time()-begin<750000 && waits++<750)sysUsleep(1000);
    if(writer_done){u64 result=0;sysThreadJoin(thread,&result);}
    // Slow/unplugged storage must not hold exit indefinitely. On timeout the
    // writer retains its buffers/descriptors until the process is terminated.
    // Retain the mutex until process exit: a timed-out producer may still return.
}
