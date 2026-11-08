// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#include "bili_feed.h"
#include "https_request.h"
#include "search_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>

#if defined(__PPU__) || defined(__CELLOS_LV2__)
#include <sys/thread.h>
#include <sys/systime.h>
#include <sys/mutex.h>
#define THREAD_RET void
#define THREAD_ARG void *arg
static sys_mutex_t s_feed_mutex = 0;
static int s_feed_mutex_inited = 0;
static void feed_lock(void) {
    if(!s_feed_mutex_inited) {
        sys_mutex_attr_t attr;
        sysMutexAttrInitialize(attr);
        attr.attr_recursive = SYS_MUTEX_ATTR_RECURSIVE;
        if(sysMutexCreate(&s_feed_mutex, &attr) == 0) s_feed_mutex_inited = 1;
    }
    if(s_feed_mutex_inited) sysMutexLock(s_feed_mutex, 0);
}
static void feed_unlock(void) {
    if(s_feed_mutex_inited) sysMutexUnlock(s_feed_mutex);
}
#else
#include <pthread.h>
#include <unistd.h>
#define THREAD_RET void*
#define THREAD_ARG void *arg
static pthread_mutex_t s_feed_mutex = PTHREAD_MUTEX_INITIALIZER;
static void feed_lock(void) { pthread_mutex_lock(&s_feed_mutex); }
static void feed_unlock(void) { pthread_mutex_unlock(&s_feed_mutex); }
#endif

static void strip_html(char *dst, const char *src, size_t max_len)
{
    if(!dst || !src || max_len == 0) return;
    size_t d = 0;
    int in_tag = 0;
    while(*src && d + 1 < max_len) {
        if(*src == '<') {
            in_tag = 1;
            src++;
        } else if(*src == '>') {
            in_tag = 0;
            src++;
        } else if(in_tag) {
            src++;
        } else if(*src == '&') {
            if(!strncmp(src, "&amp;", 5)) { dst[d++] = '&'; src += 5; }
            else if(!strncmp(src, "&quot;", 6)) { dst[d++] = '"'; src += 6; }
            else if(!strncmp(src, "&#39;", 5)) { dst[d++] = '\''; src += 5; }
            else if(!strncmp(src, "&apos;", 6)) { dst[d++] = '\''; src += 6; }
            else if(!strncmp(src, "&lt;", 4)) { dst[d++] = '<'; src += 4; }
            else if(!strncmp(src, "&gt;", 4)) { dst[d++] = '>'; src += 4; }
            else if(!strncmp(src, "&nbsp;", 6)) { dst[d++] = ' '; src += 6; }
            else { dst[d++] = *src++; }
        } else {
            // Strip invisible zero-width unicode characters (U+200B..U+200D: UTF-8 0xE2 0x80 0x8B..0x8D)
            unsigned char c1 = (unsigned char)src[0];
            unsigned char c2 = c1 == 0xE2 ? (unsigned char)src[1] : 0;
            unsigned char c3 = c2 == 0x80 ? (unsigned char)src[2] : 0;
            if(c1 == 0xE2 && c2 == 0x80 && (c3 >= 0x8B && c3 <= 0x8F)) {
                src += 3;
            } else {
                dst[d++] = *src++;
            }
        }
    }
    dst[d] = 0;
}

void bili_feed_parse_item(struct json_object *obj, bili_feed_item_t *item)
{
    if(!obj || !item) return;
    memset(item, 0, sizeof(*item));

    struct json_object *j_bvid = NULL, *j_title = NULL, *j_pic = NULL, *j_owner = NULL;
    struct json_object *j_stat = NULL, *j_duration = NULL;

    json_object_object_get_ex(obj, "bvid", &j_bvid);
    if(!j_bvid) {
        struct json_object *j_hist = NULL;
        if(json_object_object_get_ex(obj, "history", &j_hist) && j_hist) {
            json_object_object_get_ex(j_hist, "bvid", &j_bvid);
        }
    }
    struct json_object *j_progress=NULL,*j_time=NULL;
    if(json_object_object_get_ex(obj,"progress",&j_progress))item->progress_sec=json_object_get_int(j_progress);
    if(json_object_object_get_ex(obj,"view_at",&j_time))item->viewed_at=(uint64_t)json_object_get_int64(j_time);
    json_object_object_get_ex(obj, "title", &j_title);
    json_object_object_get_ex(obj, "pic", &j_pic);
    if(!j_pic) json_object_object_get_ex(obj, "cover", &j_pic);
    json_object_object_get_ex(obj, "owner", &j_owner);
    if(!j_owner) json_object_object_get_ex(obj, "upper", &j_owner);
    if(!j_owner) json_object_object_get_ex(obj, "author", &j_owner);
    if(!j_owner) json_object_object_get_ex(obj, "author_name", &j_owner);
    json_object_object_get_ex(obj, "stat", &j_stat);
    json_object_object_get_ex(obj, "duration", &j_duration);

    if(j_bvid) {
        const char *bv = json_object_get_string(j_bvid);
        if(bv) snprintf(item->bvid, sizeof(item->bvid), "%s", bv);
    }
    if(j_title) {
        const char *t = json_object_get_string(j_title);
        if(t) strip_html(item->title, t, sizeof(item->title));
    }
    if(j_pic) {
        const char *p = json_object_get_string(j_pic);
        if(p) snprintf(item->pic_url, sizeof(item->pic_url), "%s%s",!strncmp(p,"//",2)?"https:":"",p);
    }
    if(j_owner) {
        struct json_object *j_name = NULL;
        if(json_object_is_type(j_owner, json_type_string)) {
            const char *u = json_object_get_string(j_owner);
            if(u) snprintf(item->author, sizeof(item->author), "%s", u);
        } else if(json_object_object_get_ex(j_owner, "name", &j_name) && j_name) {
            const char *u = json_object_get_string(j_name);
            if(u) snprintf(item->author, sizeof(item->author), "%s", u);
        }
    }
    if(j_stat) {
        struct json_object *j_view = NULL;
        if(json_object_object_get_ex(j_stat, "view", &j_view) && j_view) {
            item->view_count = (uint32_t)json_object_get_int64(j_view);
        }
    }
    if(!item->view_count){
        struct json_object *views=NULL;
        if(json_object_object_get_ex(obj,"play",&views))item->view_count=(uint32_t)json_object_get_int64(views);
    }
    if(j_duration) {
        if(json_object_is_type(j_duration, json_type_int)) {
            item->duration_sec = (uint32_t)json_object_get_int(j_duration);
        } else if(json_object_is_type(j_duration, json_type_string)) {
            const char *ds = json_object_get_string(j_duration);
            if(ds) {
                int mm = 0, ss = 0;
                if(sscanf(ds, "%d:%d", &mm, &ss) == 2) {
                    item->duration_sec = mm * 60 + ss;
                }
            }
        }
    }
}

static int feed_cancel(void *arg);

int bili_feed_parse_response(bili_feed_state_t *st,const char *body,int search)
{
    struct json_object *root=body?json_tokener_parse(body):NULL,*code=NULL,*data=NULL,*array=NULL;
    if(!root){snprintf(st->status_msg,sizeof(st->status_msg),"数据解析失败");st->loading=0;return -1;}
    json_object_object_get_ex(root,"code",&code);
    if(!code || json_object_get_int(code)!=0){
        int value=code?json_object_get_int(code):-1;
        snprintf(st->status_msg,sizeof(st->status_msg),value==-101?"登录已过期，请重新扫码":"接口返回错误 (%d)，请稍后重试",value);
        json_object_put(root);st->loading=0;return -1;
    }
    json_object_object_get_ex(root,"data",&data);
    if(data && json_object_is_type(data,json_type_array))array=data;
    else if(data){
        if(st->current_tab==FEED_TAB_FAVORITE && st->favorite_id)json_object_object_get_ex(data,"medias",&array);
        else if(search)json_object_object_get_ex(data,"result",&array);
        else if(!json_object_object_get_ex(data,"item",&array))json_object_object_get_ex(data,"list",&array);
    }
    if(!array || !json_object_is_type(array,json_type_array)){
        json_object_put(root);st->loading=0;snprintf(st->status_msg,sizeof(st->status_msg),"未收到有效的视频列表");return -1;
    }
    st->item_count=0;unsigned count=(unsigned)json_object_array_length(array);
    int folders=st->current_tab==FEED_TAB_FAVORITE && !st->favorite_id;
    unsigned start=folders && st->page>1?(st->page-1)*BILI_FEED_MAX_ITEMS:0;
    for(unsigned i=start;i<count && st->item_count<BILI_FEED_MAX_ITEMS;i++){
        struct json_object *object=json_object_array_get_idx(array,i);
        bili_feed_item_t item;bili_feed_parse_item(object,&item);
        if(folders){
            struct json_object *id=NULL,*media_count=NULL;
            json_object_object_get_ex(object,"id",&id);json_object_object_get_ex(object,"media_count",&media_count);
            item.resource_id=id && json_object_get_int64(id)>0?(uint64_t)json_object_get_int64(id):0;item.is_folder=1;
            item.view_count=media_count?(uint32_t)json_object_get_int(media_count):0;
            if(item.resource_id)st->items[st->item_count++]=item;
        }else if(!strncmp(item.bvid,"BV",2) && strlen(item.bvid)==12)st->items[st->item_count++]=item;
    }
    st->has_more=folders?count>start+BILI_FEED_MAX_ITEMS:count>=BILI_FEED_MAX_ITEMS;
    if(st->current_tab==FEED_TAB_FAVORITE && st->favorite_id){struct json_object *more=NULL;if(json_object_object_get_ex(data,"has_more",&more))st->has_more=json_object_get_boolean(more);}
    if(st->current_tab==FEED_TAB_HISTORY && data){
        struct json_object *cursor=NULL,*max=NULL,*when=NULL;
        if(json_object_object_get_ex(data,"cursor",&cursor)){
            json_object_object_get_ex(cursor,"max",&max);json_object_object_get_ex(cursor,"view_at",&when);
            st->next_max=max?(uint64_t)json_object_get_int64(max):0;
            st->next_view_at=when?(uint64_t)json_object_get_int64(when):0;
            st->has_more=count>0 && (st->next_max || st->next_view_at);
        }else st->has_more=0;
    }
    st->loading=0;
    snprintf(st->status_msg,sizeof(st->status_msg),"第 %u 页 · %d 条视频",st->page?st->page:1,st->item_count);
    if(!st->item_count)snprintf(st->status_msg,sizeof(st->status_msg),search?"没有找到相关视频，按方块键重新搜索":"暂无观看记录或更多内容");
    json_object_put(root);return 0;
}
static int fetch_feed(bili_feed_state_t *st,const char *url,int search)
{
    wili_https_response r={0};
    int rc=wili_https_get_cookie(url,st->ca_path,NULL,st->cookie,524288,feed_cancel,NULL,&r,st->log);
    if(rc || r.status!=200 || !r.body){
        st->network_error=r.curl_code==6 || r.curl_code==7 || r.curl_code==28;
        st->loading=0;snprintf(st->status_msg,sizeof(st->status_msg),"网络请求失败 (HTTP %ld)",(long)r.status);
        wili_https_response_free(&r);return -1;
    }
    rc=bili_feed_parse_response(st,r.body,search);wili_https_response_free(&r);
    if(st->log){char line[160];snprintf(line,sizeof(line),"FEED_RESULT tab=%d page=%u items=%d rc=%d",st->current_tab,st->page,st->item_count,rc);st->log(line);}
    return rc;
}
static int do_feed_load_tab(bili_feed_state_t *st,bili_feed_tab_t tab)
{
    char url[512];unsigned page=st->page?st->page:1;
    if(tab==FEED_TAB_RECOMMEND)snprintf(url,sizeof(url),"https://api.bilibili.com/x/web-interface/index/top/feed/rcmd?ps=%d&fresh_idx=%u",BILI_FEED_MAX_ITEMS,page);
    else if(tab==FEED_TAB_POPULAR)snprintf(url,sizeof(url),"https://api.bilibili.com/x/web-interface/popular?ps=%d&pn=%u",BILI_FEED_MAX_ITEMS,page);
    else if(tab==FEED_TAB_HISTORY)snprintf(url,sizeof(url),"https://api.bilibili.com/x/web-interface/history/cursor?ps=%d&max=%llu&view_at=%llu&business=archive",BILI_FEED_MAX_ITEMS,(unsigned long long)st->req_max,(unsigned long long)st->req_view_at);
    else if(tab==FEED_TAB_FAVORITE){
        if(!st->cookie || !*st->cookie){st->loading=0;snprintf(st->status_msg,sizeof(st->status_msg),"登录后查看收藏夹");return -1;}
        if(st->favorite_id)snprintf(url,sizeof(url),"https://api.bilibili.com/x/v3/fav/resource/list?media_id=%llu&pn=%u&ps=%d&order=mtime&type=0&tid=0&platform=web",(unsigned long long)st->favorite_id,page,BILI_FEED_MAX_ITEMS);
        else{
            const char *id=strstr(st->cookie,"DedeUserID=");unsigned long long uid=id?strtoull(id+11,NULL,10):0;
            if(!uid){st->loading=0;snprintf(st->status_msg,sizeof(st->status_msg),"账号标识缺失，请重新登录");return -1;}
            snprintf(url,sizeof(url),"https://api.bilibili.com/x/v3/fav/folder/created/list-all?up_mid=%llu",uid);
        }
    }else {st->loading=0;return 0;}
    return fetch_feed(st,url,0);
}
// Search requires the normal client identifiers issued by Bilibili's API.
// Fetch once per run; do not rotate identifiers or retry denied requests.
static char search_client_cookie[384];
static int search_client_attempted;
static void prepare_search_client(bili_feed_state_t *st)
{
    if(search_client_attempted || (st->cookie && strstr(st->cookie,"buvid3=")))return;
    search_client_attempted=1;wili_https_response r={0};
    int rc=wili_https_get_cookie("https://api.bilibili.com/x/frontend/finger/spi",st->ca_path,NULL,NULL,65536,feed_cancel,NULL,&r,st->log);
    struct json_object *root=!rc && r.status==200 && r.body?json_tokener_parse(r.body):NULL,*data=NULL,*b3=NULL,*b4=NULL;
    if(root && json_object_object_get_ex(root,"data",&data)){
        json_object_object_get_ex(data,"b_3",&b3);json_object_object_get_ex(data,"b_4",&b4);
        const char *v3=b3?json_object_get_string(b3):NULL,*v4=b4?json_object_get_string(b4):NULL;
        const char *allowed="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";
        if(v3 && v4 && *v3 && *v4 && strlen(v3)<160 && strlen(v4)<160 && strspn(v3,allowed)==strlen(v3) && strspn(v4,allowed)==strlen(v4)){
            snprintf(search_client_cookie,sizeof(search_client_cookie),"buvid3=%s; buvid4=%s",v3,v4);
            if(st->log)st->log("SEARCH_CLIENT_ID_READY");
        }
    }
    if(root)json_object_put(root);
    wili_https_response_free(&r);
}
static int do_feed_search(bili_feed_state_t *st,const char *keyword)
{
    char escaped[768],url[1024];
    if(wili_url_encode(keyword,escaped,sizeof(escaped))<0){st->loading=0;return -1;}
    snprintf(url,sizeof(url),"https://api.bilibili.com/x/web-interface/search/type?search_type=video&keyword=%s&page=%u&page_size=%d",escaped,st->page?st->page:1,BILI_FEED_MAX_ITEMS);
    prepare_search_client(st);
    const char *original=st->cookie;char combined[1536];
    snprintf(combined,sizeof(combined),"%s%s%s",original?original:"",original && *original?"; ":"",search_client_cookie);
    st->cookie=combined;int rc=fetch_feed(st,url,1);st->cookie=original;return rc;
}


#define FEED_SESSION_CACHE_ENTRIES 256u
typedef struct {bili_feed_state_t *view;unsigned epoch;int ready,success;} feed_cache_entry;
static feed_cache_entry session_cache[FEED_SESSION_CACHE_ENTRIES];
static feed_cache_entry *last_view[FEED_TAB_COUNT],*active_cache,*queued_cache,*inflight_cache;
static unsigned cache_epoch[FEED_TAB_COUNT],inflight_epoch;
static char request_cookie[1024];
static volatile unsigned worker_running;
static int feed_cancel(void *arg){(void)arg;return !__sync_add_and_fetch(&worker_running,0);}
static void copy_view(bili_feed_state_t *dst,const bili_feed_state_t *src)
{
    dst->current_tab=src->current_tab;dst->item_count=src->item_count;
    memcpy(dst->items,src->items,sizeof(dst->items));dst->loading=src->loading;dst->network_error=src->network_error;
    dst->page=src->page;dst->has_more=src->has_more;dst->next_max=src->next_max;dst->next_view_at=src->next_view_at;
    dst->req_max=src->req_max;dst->req_view_at=src->req_view_at;dst->favorite_id=src->favorite_id;
    memcpy(dst->favorite_title,src->favorite_title,sizeof(dst->favorite_title));
    memcpy(dst->search_keyword,src->search_keyword,sizeof(dst->search_keyword));
    memcpy(dst->status_msg,src->status_msg,sizeof(dst->status_msg));
}
static void reset_cache(void)
{
    for(unsigned i=0;i<FEED_TAB_COUNT;i++){cache_epoch[i]++;last_view[i]=NULL;}
    for(unsigned i=0;i<FEED_SESSION_CACHE_ENTRIES;i++){free(session_cache[i].view);memset(&session_cache[i],0,sizeof(session_cache[i]));}
    active_cache=queued_cache=NULL;
}
static int same_key(const bili_feed_state_t *a,const bili_feed_state_t *b)
{
    return a->current_tab==b->current_tab && a->favorite_id==b->favorite_id && a->page==b->page &&
        (a->current_tab!=FEED_TAB_SEARCH || !strcmp(a->search_keyword,b->search_keyword));
}
static feed_cache_entry *cache_find(const bili_feed_state_t *st)
{
    feed_cache_entry *empty=NULL;
    for(unsigned i=0;i<FEED_SESSION_CACHE_ENTRIES;i++){
        feed_cache_entry *e=&session_cache[i];
        if(e->view && same_key(e->view,st))return e;
        if(!e->view && !empty)empty=e;
    }
    if(!empty)return NULL;
    empty->view=malloc(sizeof(*st));if(!empty->view)return NULL;
    *empty->view=*st;empty->view->cookie=NULL;empty->epoch=cache_epoch[st->current_tab];return empty;
}
// Called with the feed lock held. A request already running for this key is
// allowed to finish into the cache even if the user visits another category.
static int activate_view(bili_feed_state_t *st,int fetch)
{
    st->req_pending=0;queued_cache=NULL;active_cache=cache_find(st);
    if(!active_cache){st->loading=0;snprintf(st->status_msg,sizeof(st->status_msg),"会话缓存已满，请重启软件后继续");return -1;}
    last_view[st->current_tab]=active_cache;
    if(active_cache->ready && active_cache->success && active_cache->epoch==cache_epoch[st->current_tab]){
        copy_view(st,active_cache->view);st->loading=0;return 0;
    }
    if(!fetch || !st->running){st->loading=0;*active_cache->view=*st;active_cache->view->cookie=NULL;
        active_cache->epoch=cache_epoch[st->current_tab];active_cache->ready=active_cache->success=1;return 0;}
    st->loading=1;
    if(active_cache==inflight_cache && inflight_epoch==cache_epoch[st->current_tab])return 0;
    active_cache->ready=0;active_cache->epoch=cache_epoch[st->current_tab];
    st->req_tab=st->current_tab;st->req_favorite_id=st->favorite_id;st->req_page=st->page;
    snprintf(st->req_search,sizeof(st->req_search),"%s",st->current_tab==FEED_TAB_SEARCH?st->search_keyword:"");
    queued_cache=active_cache;st->req_pending=1;return 0;
}
static THREAD_RET feed_worker(THREAD_ARG)
{
    bili_feed_state_t *st=arg;
    while(__sync_add_and_fetch(&worker_running,0)){
        bili_feed_state_t job;char cookie[1024];feed_cache_entry *entry=NULL;unsigned epoch=0;
        feed_lock();
        if(st->req_pending && queued_cache){
            entry=queued_cache;queued_cache=NULL;st->req_pending=0;
            memset(&job,0,sizeof(job));job.ca_path=st->ca_path;job.log=st->log;job.current_tab=st->req_tab;
            job.favorite_id=st->req_favorite_id;job.page=st->req_page;job.req_max=st->req_max;job.req_view_at=st->req_view_at;
            snprintf(job.favorite_title,sizeof(job.favorite_title),"%s",st->favorite_title);
            snprintf(job.search_keyword,sizeof(job.search_keyword),"%s",st->req_search);
            snprintf(cookie,sizeof(cookie),"%s",request_cookie);job.cookie=cookie;
            epoch=cache_epoch[job.current_tab];inflight_cache=entry;inflight_epoch=epoch;
        }
        feed_unlock();
        if(!entry){
#if defined(__PPU__) || defined(__CELLOS_LV2__)
            sysUsleep(10000);
#else
            usleep(10000);
#endif
            continue;
        }
        int rc=job.search_keyword[0]?do_feed_search(&job,job.search_keyword):do_feed_load_tab(&job,job.current_tab);
        job.loading=0;job.cookie=NULL;
        feed_lock();
        if(epoch==cache_epoch[job.current_tab] && entry->view && same_key(entry->view,&job)){
            *entry->view=job;entry->epoch=epoch;entry->success=rc==0;entry->ready=1;
        }
        inflight_cache=NULL;feed_unlock();
    }
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    sysThreadExit(0);
#else
    return NULL;
#endif
}
int bili_feed_init(bili_feed_state_t *st,const char *cookie,const char *ca,void (*log)(const char*))
{
    if(!st)return -1;
    feed_lock();
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    if(!s_feed_mutex_inited)return -1;
#endif
    if(strcmp(request_cookie,cookie?cookie:""))reset_cache();
    memset(st,0,sizeof(*st));st->ca_path=ca;st->log=log;
    snprintf(request_cookie,sizeof(request_cookie),"%s",cookie?cookie:"");
    active_cache=queued_cache=inflight_cache=NULL;worker_running=1;feed_unlock();
    int rc;
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    sys_ppu_thread_t tid=0;rc=sysThreadCreate(&tid,feed_worker,st,1100,262144,THREAD_JOINABLE,"bibi-feed");
#else
    pthread_t tid;rc=pthread_create(&tid,NULL,feed_worker,st);
#endif
    if(rc){worker_running=0;snprintf(st->status_msg,sizeof(st->status_msg),"内容线程启动失败");return -1;}
    st->worker_thread=(uint64_t)tid;st->running=1;return 0;
}
void bili_feed_cancel(void){__sync_lock_test_and_set(&worker_running,0);}
void bili_feed_shutdown(bili_feed_state_t *st)
{
    if(!st || !st->running)return;
    bili_feed_cancel();
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    u64 result;sysThreadJoin((sys_ppu_thread_t)st->worker_thread,&result);
#else
    pthread_join((pthread_t)st->worker_thread,NULL);
#endif
    st->running=0;st->worker_thread=0;
}
void bili_feed_set_cookie(const char *cookie)
{
    feed_lock();if(strcmp(request_cookie,cookie?cookie:""))reset_cache();
    snprintf(request_cookie,sizeof(request_cookie),"%s",cookie?cookie:"");feed_unlock();
}
int bili_feed_pump(bili_feed_state_t *st)
{
    int changed=0;feed_lock();
    if(st->loading && active_cache && active_cache->view && active_cache->ready &&
       active_cache->epoch==cache_epoch[st->current_tab] && same_key(st,active_cache->view)){
        copy_view(st,active_cache->view);st->loading=0;changed=1;
    }
    feed_unlock();return changed;
}
int bili_feed_load_tab(bili_feed_state_t *st,bili_feed_tab_t tab)
{
    if(!st || tab<0 || tab>=FEED_TAB_COUNT)return -1;
    feed_lock();int root=tab==FEED_TAB_FAVORITE && st->current_tab==tab && st->favorite_id;
    feed_cache_entry *last=last_view[tab];
    if(!root && last && last->view && last->epoch==cache_epoch[tab])copy_view(st,last->view);
    else{
        st->current_tab=tab;st->item_count=0;st->loading=st->network_error=st->has_more=0;st->page=1;
        st->favorite_id=st->req_max=st->req_view_at=st->next_max=st->next_view_at=0;
        st->favorite_title[0]=st->search_keyword[0]=0;snprintf(st->status_msg,sizeof(st->status_msg),"正在获取内容…");
    }
    int fetch=1;
    if((tab==FEED_TAB_HISTORY || tab==FEED_TAB_FAVORITE) && !request_cookie[0]){snprintf(st->status_msg,sizeof(st->status_msg),"登录后查看此栏目");fetch=0;}
    if(tab==FEED_TAB_USER)fetch=0;
    if(tab==FEED_TAB_SEARCH && !st->search_keyword[0]){snprintf(st->status_msg,sizeof(st->status_msg),"按叉键输入关键词，支持中文搜索");fetch=0;}
    int rc=activate_view(st,fetch);feed_unlock();return rc;
}
int bili_feed_search(bili_feed_state_t *st,const char *keyword)
{
    if(!st || !keyword || !*keyword || !st->running)return -1;
    char copy[256];snprintf(copy,sizeof(copy),"%s",keyword);
    feed_lock();st->current_tab=FEED_TAB_SEARCH;st->page=1;st->favorite_id=0;st->item_count=st->has_more=st->network_error=0;
    st->req_max=st->req_view_at=0;snprintf(st->search_keyword,sizeof(st->search_keyword),"%s",copy);
    int rc=activate_view(st,1);feed_unlock();return rc;
}
int bili_feed_copy_items_safe(bili_feed_state_t *st,bili_feed_item_t *dst,int max,int *count)
{
    if(!st || !dst || max<=0)return -1;
    bili_feed_pump(st);int n=st->item_count<max?st->item_count:max;memcpy(dst,st->items,n*sizeof(*dst));if(count)*count=n;return 0;
}
int bili_feed_next_page(bili_feed_state_t *st)
{
    if(!st || !st->running || st->loading || !st->has_more)return -1;
    feed_lock();st->req_max=st->next_max;st->req_view_at=st->next_view_at;st->page++;st->item_count=0;
    snprintf(st->status_msg,sizeof(st->status_msg),"正在读取下一页…");int rc=activate_view(st,1);feed_unlock();return rc;
}
int bili_feed_open_folder(bili_feed_state_t *st,int index)
{
    if(!st || !st->running || st->current_tab!=FEED_TAB_FAVORITE || index<0 || index>=st->item_count || !st->items[index].is_folder)return -1;
    feed_lock();st->favorite_id=st->items[index].resource_id;
    snprintf(st->favorite_title,sizeof(st->favorite_title),"%.127s",st->items[index].title);
    st->page=1;st->item_count=st->has_more=st->network_error=0;st->req_max=st->req_view_at=0;
    int rc=activate_view(st,1);feed_unlock();return rc;
}
int bili_feed_refresh(bili_feed_state_t *st)
{
    if(!st || !st->running)return -1;
    feed_lock();cache_epoch[st->current_tab]++;st->page=1;st->req_max=st->req_view_at=0;st->item_count=0;st->network_error=0;
    int fetch=st->current_tab!=FEED_TAB_USER && (st->current_tab!=FEED_TAB_SEARCH || st->search_keyword[0]);
    int rc=activate_view(st,fetch);feed_unlock();return rc;
}
