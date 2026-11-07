#define _DEFAULT_SOURCE
#include "history_sync.h"
#include "https_request.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
static const char *cookie="SESSDATA=fixture; DedeUserID=7; bili_jct=0123456789abcdef0123456789abcdef";
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned calls,progresses[128];static uint64_t aids[128];static int failure,hold;
static void log_line(const char *s){assert(!strstr(s,"fixture") && !strstr(s,"csrf=") && !strstr(s,"0123456789abcdef"));}
int wili_https_post_form(const char *url,const char *ca,const char *account,const char *form,size_t cap,int (*cancel)(void*),void *arg,wili_https_response *r,void (*log)(const char*))
{
    (void)ca;(void)cap;(void)log;assert(!strcmp(url,"https://api.bilibili.com/x/v2/history/report"));assert(!strcmp(account,cookie));
    assert(strstr(form,"csrf=0123456789abcdef0123456789abcdef") && strstr(form,"mid=7&") && strstr(form,"type=3&dt=2"));
    pthread_mutex_lock(&lock);unsigned n=calls++;assert(n<128);progresses[n]=(unsigned)strtoul(strstr(form,"progress=")+9,NULL,10);aids[n]=strtoull(strstr(form,"aid=")+4,NULL,10);pthread_mutex_unlock(&lock);
    while(__sync_add_and_fetch(&hold,0) && !cancel(arg))usleep(1000);
    if(cancel(arg)){memset(r,0,sizeof(*r));return -1;}
    memset(r,0,sizeof(*r));r->status=200;r->body=strdup(__sync_add_and_fetch(&failure,0)?"{\"code\":-101}":"{\"code\":0}");r->bytes=strlen(r->body);return 0;
}
void wili_https_response_free(wili_https_response *r){free(r->body);r->body=NULL;}
static unsigned count(void){pthread_mutex_lock(&lock);unsigned n=calls;pthread_mutex_unlock(&lock);return n;}
static void wait_count(unsigned n){for(int i=0;i<3000 && count()<n;i++)usleep(1000);assert(count()>=n);}
static void wait_status(int value){for(int i=0;i<3000 && history_sync_status()!=value;i++)usleep(1000);assert(history_sync_status()==value);}
static uint64_t now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
int main(void)
{
    assert(!history_sync_init(NULL,log_line));
    history_sync_begin(42,101,120000,NULL);history_sync_tick(1000,0,0,1000000);usleep(50000);assert(!count());
    history_sync_begin(42,101,120000,cookie);history_sync_end(0,0);usleep(50000);assert(!count()); // no frame presented
    history_sync_begin(42,101,120000,cookie);history_sync_tick(1000,0,0,1000000);wait_count(1);wait_status(2);assert(progresses[0]==1);
    for(unsigned t=2;t<=15;t++)history_sync_tick(t*1000,0,0,t*1000000);
    usleep(50000);assert(count()==1);
    history_sync_tick(16000,0,0,16000000);wait_count(2);wait_status(2);assert(progresses[1]==16);
    history_sync_tick(16000,1,0,17000000);wait_count(3);wait_status(2);
    for(unsigned t=18;t<=35;t++)history_sync_tick(16000,1,0,t*1000000);
    usleep(50000);assert(count()==3);
    history_sync_tick(5000,1,0,36000000);wait_count(4);wait_status(2);assert(progresses[3]==5); // backward seek is not max(progress)
    history_sync_end(7000,0);wait_count(5);wait_status(2);assert(progresses[4]==7);
    history_sync_begin(43,102,120000,cookie);history_sync_tick(119000,0,1,40000000);wait_count(6);wait_status(2);assert(progresses[5]==120);
    // An HTTP 200 with API code -101 is a failure, not an acknowledgement.
    failure=1;history_sync_begin(44,103,120000,cookie);history_sync_tick(1000,0,0,50000000);wait_count(7);wait_status(-1);
    usleep(50000);assert(count()==7);
    history_sync_tick(2000,1,0,51000000);usleep(50000);assert(count()==7); // newer progress retains backoff
    failure=0;history_sync_close_begin(1000);history_sync_shutdown(1000);assert(count()==8 && progresses[7]==2);
    // In-flight work cannot overwrite a newer position. Account logout cancels old work.
    assert(!history_sync_init(NULL,log_line));hold=1;
    history_sync_begin(45,104,120000,cookie);history_sync_tick(1000,0,0,60000000);wait_count(9);
    history_sync_tick(23000,0,0,61000000);hold=0;wait_count(10);wait_status(2);assert(progresses[9]==23);
    hold=1;history_sync_begin(46,105,120000,cookie);history_sync_tick(1000,0,0,62000000);wait_count(11);
    history_sync_account(NULL);hold=0;usleep(50000);assert(history_sync_status()==0);
    // Closing is bounded even if the network request remains stuck until cancellation.
    hold=1;history_sync_begin(47,106,120000,cookie);history_sync_tick(1000,0,0,63000000);wait_count(12);
    uint64_t begin=now_ms();history_sync_shutdown(100);assert(now_ms()-begin<1000);hold=0;
    puts("PASS history first frame/15s/pause/seek/end, guest/no-frame refusal, authenticated POST, API failure/backoff/coalescing, logout cancellation and bounded shutdown");
}
