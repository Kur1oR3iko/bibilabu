// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE
#include "bili_feed.h"
#include "https_request.h"
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
static volatile unsigned requests;
static void log_line(const char *s){(void)s;}
int wili_https_get_cookie(const char *url,const char *ca,const char *range,const char *cookie,size_t cap,
                   int (*cancel)(void*),void *arg,wili_https_response *r,void (*log)(const char*))
{
    (void)ca;(void)range;(void)cookie;(void)cap;(void)log;
    __sync_add_and_fetch(&requests,1);memset(r,0,sizeof(*r));
    for(int i=0;i<120 && !cancel(arg);i++)usleep(1000);
    const char *name=strstr(url,"popular")?"current":"stale";
    char body[256];snprintf(body,sizeof(body),"{\"code\":0,\"data\":{\"list\":[{\"bvid\":\"BV17x411w7KC\",\"title\":\"%s\"}]}}",name);
    if(strstr(url,"/finger/spi"))snprintf(body,sizeof(body),"{\"code\":0,\"data\":{\"b_3\":\"test-client\",\"b_4\":\"test-client4\"}}");
    if(strstr(url,"/search/type")){
        assert(strstr(url,"keyword=%E4%B8%AD%E6%96%87%20%26%3F"));
        assert(cookie && strstr(cookie,"buvid3=test-client") && strstr(cookie,"buvid4=test-client4"));
        snprintf(body,sizeof(body),"{\"code\":0,\"data\":{\"result\":[{\"bvid\":\"BV17x411w7KC\",\"title\":\"search result\"}]}}");
    }
    if(strstr(url,"/fav/folder/created/list-all")){
        assert(strstr(url,"up_mid=42"));
        snprintf(body,sizeof(body),"{\"code\":0,\"data\":{\"list\":[{\"id\":123456789012,\"title\":\"folder\",\"media_count\":17}]}}");
    }
    if(strstr(url,"/fav/resource/list")){
        assert(strstr(url,"media_id=123456789012"));
        snprintf(body,sizeof(body),"{\"code\":0,\"data\":{\"has_more\":%s,\"medias\":[{\"bvid\":\"BV17x411w7KC\",\"title\":\"favorite video\"}]}}",strstr(url,"pn=2")?"false":"true");
    }
    r->body=strdup(body);r->bytes=strlen(body);r->status=200;return 0;
}
void wili_https_response_free(wili_https_response *r){free(r->body);r->body=NULL;}
int main(void)
{
    bili_feed_state_t st;assert(!bili_feed_init(&st,NULL,NULL,log_line));
    bili_feed_load_tab(&st,FEED_TAB_RECOMMEND);
    for(int i=0;i<1000 && !__sync_add_and_fetch(&requests,0);i++)usleep(1000);
    assert(requests==1);bili_feed_load_tab(&st,FEED_TAB_POPULAR);
    int published=0;
    for(int i=0;i<1500;i++){
        if(bili_feed_pump(&st)){published++;assert(st.item_count==1);assert(!strcmp(st.items[0].title,"current"));break;}
        usleep(1000);
    }
    assert(published==1 && requests==2);
    assert(!bili_feed_load_tab(&st,FEED_TAB_RECOMMEND));assert(!st.loading && st.item_count==1 && !strcmp(st.items[0].title,"stale"));
    assert(!bili_feed_load_tab(&st,FEED_TAB_POPULAR));assert(!st.loading && !strcmp(st.items[0].title,"current"));assert(requests==2);
    assert(!bili_feed_refresh(&st));
    for(int i=0;i<1000 && st.loading;i++){bili_feed_pump(&st);usleep(1000);}assert(!st.loading && requests==3);
    puts("PASS category cache retains completed background requests; tab switches do not fetch; explicit refresh fetches once");
    bili_feed_load_tab(&st,FEED_TAB_FAVORITE);assert(st.item_count==0 && !st.loading);
    assert(!bili_feed_search(&st,"中文 &?"));published=0;
    for(int i=0;i<1500;i++){if(bili_feed_pump(&st)){published=1;break;}usleep(1000);}
    assert(published && st.item_count==1 && !strcmp(st.items[0].title,"search result"));
    assert(requests==5);puts("PASS search worker: official client IDs, escaped keyword, result publication");
    bili_feed_set_cookie("DedeUserID=42; SESSDATA=fixture");
    assert(!bili_feed_load_tab(&st,FEED_TAB_FAVORITE));
    for(int i=0;i<1000 && st.loading;i++){bili_feed_pump(&st);usleep(1000);}
    assert(st.item_count==1 && st.items[0].is_folder && !st.favorite_id);
    assert(!bili_feed_open_folder(&st,0));
    for(int i=0;i<1000 && st.loading;i++){bili_feed_pump(&st);usleep(1000);}
    assert(st.favorite_id==123456789012ULL && st.item_count==1 && !st.items[0].is_folder && st.has_more);
    assert(!bili_feed_next_page(&st));
    for(int i=0;i<1000 && st.loading;i++){bili_feed_pump(&st);usleep(1000);}
    assert(st.page==2 && !st.has_more);
    bili_feed_load_tab(&st,FEED_TAB_FAVORITE);
    for(int i=0;i<1000 && st.loading;i++){bili_feed_pump(&st);usleep(1000);}
    assert(!st.favorite_id && st.items[0].is_folder);
    puts("PASS favorite worker folder -> media -> next page -> back, authenticated UID and 64-bit IDs");
    unsigned cached_requests=requests;
    assert(!bili_feed_open_folder(&st,0));assert(!st.loading && st.page==1);
    assert(!bili_feed_next_page(&st));assert(!st.loading && st.page==2);
    bili_feed_load_tab(&st,FEED_TAB_USER);bili_feed_load_tab(&st,FEED_TAB_FAVORITE);
    assert(!st.loading && st.page==2 && st.favorite_id==123456789012ULL && requests==cached_requests);
    bili_feed_shutdown(&st);bili_feed_shutdown(&st);
    assert(!bili_feed_init(&st,"DedeUserID=42; SESSDATA=fixture",NULL,log_line));
    bili_feed_load_tab(&st,FEED_TAB_FAVORITE);assert(!st.loading && st.page==2 && requests==cached_requests);bili_feed_shutdown(&st);
    puts("PASS folder/page/session cache survives category changes and network worker restart");
    puts("PASS stale response discarded, main-thread publication, honest unsupported tab, safe join");
}
