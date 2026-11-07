// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE
#include "bili_detail.h"
#include "bili_api.h"
#include "https_request.h"
#include "search_text.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__PPU__) || defined(__CELLOS_LV2__)
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/systime.h>
static sys_mutex_t lock;static sys_ppu_thread_t thread;
#define LOCK() sysMutexLock(lock,0)
#define UNLOCK() sysMutexUnlock(lock)
#define SLEEP() sysUsleep(20000)
#define THREAD_RETURN void
#else
#include <pthread.h>
#include <unistd.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;static pthread_t thread;
#define LOCK() pthread_mutex_lock(&lock)
#define UNLOCK() pthread_mutex_unlock(&lock)
#define SLEEP() usleep(20000)
#define THREAD_RETURN void*
#endif
static volatile unsigned running,epoch,allowed=1;
static int started,pending;static unsigned revision,seen;
static uint64_t reply_target;
static char account[1024],draft[1024];
static bili_detail_state state;
// Worker-owned snapshots; never mutate while a UI reader uses the state copy.
#define RELATED_POOL_CAP 128
static bili_feed_item_t related_pool[RELATED_POOL_CAP];
static unsigned related_pool_count,related_pool_epoch;
#define CREATOR_CACHE_PAGES 64
static struct {unsigned epoch,page,total;int count,more; bili_feed_item_t items[BILI_DETAIL_VIDEOS];} creator_cache[CREATOR_CACHE_PAGES];

static const char *ca_path;static void (*logger)(const char*);
static struct json_object *get(struct json_object *o,const char *k){struct json_object *v=NULL;if(o)json_object_object_get_ex(o,k,&v);return v;}
static uint64_t num(struct json_object *o,const char *k){struct json_object *v=get(o,k);int64_t n=v?json_object_get_int64(v):0;return n>0?(uint64_t)n:0;}
static void text(char *dst,size_t cap,struct json_object *o,const char *k)
{
    struct json_object *v=get(o,k);const char *s=v && json_object_is_type(v,json_type_string)?json_object_get_string(v):"";
    size_t n=strlen(s);if(n>=cap){n=cap-1;while(n && ((unsigned char)s[n]&0xc0)==0x80)n--;}
    memcpy(dst,s,n);dst[n]=0;for(size_t i=0;i<n;i++)if((unsigned char)dst[i]<32)dst[i]=' ';
}
static struct json_object *data_root(const char *body,size_t n)
{
    struct json_object *root=wili_bili_json_parse(body,n);if(!root)return NULL;
    struct json_object *code=get(root,"code");if(!code || json_object_get_int(code)){
        if(logger){char line[80];snprintf(line,sizeof(line),"DETAIL_API_CODE code=%d",code?json_object_get_int(code):-1);logger(line);}
        json_object_put(root);return NULL;
    }return root;
}
int bili_detail_parse_view(const char *body,size_t n,bili_detail_state *s)
{
    struct json_object *root=data_root(body,n);if(!root)return -1;struct json_object *d=get(root,"data"),*owner=get(d,"owner"),*stats=get(d,"stat");
    char bv[16];text(bv,sizeof(bv),d,"bvid");
    if(!wili_bili_valid_bvid(bv) || (s->bvid[0] && strcmp(s->bvid,bv)) || !num(d,"aid") || !num(owner,"mid")){json_object_put(root);return -1;}
    snprintf(s->bvid,sizeof(s->bvid),"%s",bv);text(s->title,sizeof(s->title),d,"title");text(s->description,sizeof(s->description),d,"desc");
    text(s->up_name,sizeof(s->up_name),owner,"name");text(s->up_face,sizeof(s->up_face),owner,"face");
    s->aid=num(d,"aid");s->cid=num(d,"cid");s->mid=num(owner,"mid");s->views=(unsigned)num(stats,"view");s->likes=(unsigned)num(stats,"like");s->replies=(unsigned)num(stats,"reply");s->ready=1;
    json_object_put(root);return 0;
}
int bili_detail_parse_comments(const char *body,size_t n,bili_detail_state *s)
{
    struct json_object *root=data_root(body,n);if(!root)return -1;struct json_object *d=get(root,"data"),*list=get(d,"replies");
    s->comment_count=0;memset(s->comments,0,sizeof(s->comments));
    if(list && json_object_is_type(list,json_type_array))for(size_t i=0;i<(size_t)json_object_array_length(list) && s->comment_count<BILI_DETAIL_COMMENTS;i++){
        struct json_object *e=json_object_array_get_idx(list,i),*member=get(e,"member"),*content=get(e,"content");bili_comment *c=&s->comments[s->comment_count];
        c->id=num(e,"rpid");c->mid=num(e,"mid");c->likes=(unsigned)num(e,"like");text(c->author,sizeof(c->author),member,"uname");text(c->message,sizeof(c->message),content,"message");
        if(c->id && c->message[0])s->comment_count++;
    }
    uint64_t total=num(get(d,"page"),"count");s->comments_more=total>(uint64_t)s->comment_page*BILI_DETAIL_COMMENTS;
    snprintf(s->comment_status,sizeof(s->comment_status),"%s",s->comment_count?"叉键回复所选评论":"暂无评论，或该视频关闭了评论");json_object_put(root);return 0;
}
static int parse_videos(struct json_object *list,bili_feed_item_t *out,unsigned capacity)
{
    int count=0;if(!list || !json_object_is_type(list,json_type_array))return 0;
    for(size_t i=0;i<(size_t)json_object_array_length(list) && (unsigned)count<capacity;i++){
        struct json_object *v=json_object_array_get_idx(list,i);bili_feed_item_t *item=&out[count];memset(item,0,sizeof(*item));
        text(item->bvid,sizeof(item->bvid),v,"bvid");if(!wili_bili_valid_bvid(item->bvid))continue;
        text(item->title,sizeof(item->title),v,"title");text(item->pic_url,sizeof(item->pic_url),v,"pic");
        text(item->author,sizeof(item->author),get(v,"owner"),"name");if(!item->author[0])text(item->author,sizeof(item->author),v,"author");
        item->duration_sec=(unsigned)num(v,"duration");item->view_count=(unsigned)num(get(v,"stat"),"view");count++;
    }
    return count;
}
int bili_detail_cookie_value(const char *cookie,const char *name,char *out,size_t cap)
{
    if(!cookie || !name || !out || !cap)return -1;
    size_t key=strlen(name);out[0]=0;
    for(const char *p=cookie;*p;){while(*p==' ' || *p==';')p++;const char *end=strchr(p,';');if(!end)end=p+strlen(p);
        if((size_t)(end-p)>key && !strncmp(p,name,key) && p[key]=='='){size_t n=(size_t)(end-p)-key-1;if(n>=cap)return -1;memcpy(out,p+key+1,n);out[n]=0;return n?0:-1;}
        p=end;
    }return -1;
}
static int canceled(void *arg){return !__sync_add_and_fetch(&running,0) || (unsigned)(uintptr_t)arg!=__sync_add_and_fetch(&epoch,0);}
static int request(const char *url,const char *cookie,const char *form,unsigned generation,wili_https_response *r)
{
    while(!allowed && !canceled((void*)(uintptr_t)generation))SLEEP();
    if(canceled((void*)(uintptr_t)generation))return -1;
    int rc=form?wili_https_post_form(url,ca_path,cookie,form,2*1024*1024,canceled,(void*)(uintptr_t)generation,r,logger):
        wili_https_get_cookie(url,ca_path,NULL,cookie,2*1024*1024,canceled,(void*)(uintptr_t)generation,r,logger);
    return rc || r->status!=200?-1:0;
}
static void publish(bili_detail_state *s,unsigned generation,int final)
{
    LOCK();if(epoch==generation){s->busy=!final;state=*s;revision++;}UNLOCK();
}
static void comments(bili_detail_state *s,const char *cookie,unsigned generation)
{
    char url[256];snprintf(url,sizeof(url),"https://api.bilibili.com/x/v2/reply?type=1&oid=%llu&pn=%u&ps=%u&sort=2",(unsigned long long)s->aid,s->comment_page,BILI_DETAIL_COMMENTS);
    wili_https_response r={0};if(request(url,cookie,NULL,generation,&r) || bili_detail_parse_comments(r.body,r.bytes,s)){
        s->comment_count=0;s->comments_more=0;snprintf(s->comment_status,sizeof(s->comment_status),"评论暂不可用，三角键重试");}
    wili_https_response_free(&r);
}
static void load(bili_detail_state *s,const char *cookie,unsigned generation)
{
    char url[512];wili_https_response r={0};snprintf(url,sizeof(url),"https://api.bilibili.com/x/web-interface/view?bvid=%s",s->bvid);
    if(request(url,cookie,NULL,generation,&r) || bili_detail_parse_view(r.body,r.bytes,s)){snprintf(s->status,sizeof(s->status),"视频详情暂不可用");wili_https_response_free(&r);return;}
    wili_https_response_free(&r);publish(s,generation,0);
    snprintf(url,sizeof(url),"https://api.bilibili.com/x/web-interface/archive/related?bvid=%s",s->bvid);
    related_pool_count=0;related_pool_epoch=generation;
    if(!request(url,cookie,NULL,generation,&r)){struct json_object *root=data_root(r.body,r.bytes);if(root){related_pool_count=(unsigned)parse_videos(get(root,"data"),related_pool,RELATED_POOL_CAP);json_object_put(root);}}
    s->related_page=1;s->related_pages=(related_pool_count+BILI_DETAIL_VIDEOS-1)/BILI_DETAIL_VIDEOS;
    s->related_count=related_pool_count>BILI_DETAIL_VIDEOS?BILI_DETAIL_VIDEOS:(int)related_pool_count;
    memcpy(s->related,related_pool,s->related_count*sizeof(s->related[0]));
    wili_https_response_free(&r);publish(s,generation,0);
    s->comment_page=1;comments(s,cookie,generation);publish(s,generation,0);
    if(cookie && *cookie){
        snprintf(url,sizeof(url),"https://api.bilibili.com/x/relation?fid=%llu",(unsigned long long)s->mid);
        if(!request(url,cookie,NULL,generation,&r)){struct json_object *root=data_root(r.body,r.bytes);if(root){unsigned attr=(unsigned)num(get(root,"data"),"attribute");s->following=attr==2 || attr==6;json_object_put(root);}}
        wili_https_response_free(&r);
        snprintf(url,sizeof(url),"https://api.bilibili.com/x/web-interface/archive/has/like?aid=%llu",(unsigned long long)s->aid);
        if(!request(url,cookie,NULL,generation,&r)){struct json_object *root=data_root(r.body,r.bytes);if(root){s->liked=json_object_get_int(get(root,"data"))==1;json_object_put(root);}}
        wili_https_response_free(&r);
    }
    snprintf(s->status,sizeof(s->status),"就绪");
}
static void related_page(bili_detail_state *s,int direction,unsigned generation)
{
    if(related_pool_epoch!=generation || !s->related_pages)return;
    unsigned page=s->related_page?s->related_page:1;
    if(direction>0){if(page>=s->related_pages){snprintf(s->status,sizeof(s->status),"已到当前相关推荐末页");return;}page++;}
    else{if(page<=1){snprintf(s->status,sizeof(s->status),"已经是第一页");return;}page--;}
    unsigned offset=(page-1)*BILI_DETAIL_VIDEOS,n=related_pool_count-offset;
    if(n>BILI_DETAIL_VIDEOS)n=BILI_DETAIL_VIDEOS;
    memset(s->related,0,sizeof(s->related));memcpy(s->related,related_pool+offset,n*sizeof(s->related[0]));
    s->related_page=page;s->related_count=(int)n;snprintf(s->status,sizeof(s->status),"相关推荐 · 第 %u / %u 页",page,s->related_pages);
}
static void creator(bili_detail_state *s,const char *cookie,unsigned generation,int action)
{
    unsigned page=action==BILI_DETAIL_CREATOR?1:action==BILI_DETAIL_CREATOR_NEXT?s->creator_page+1:s->creator_page>1?s->creator_page-1:1;
    unsigned slot=(page-1)%CREATOR_CACHE_PAGES;
    if(action==BILI_DETAIL_CREATOR){for(unsigned i=0;i<CREATOR_CACHE_PAGES;i++)creator_cache[i].epoch=0;}
    else if(creator_cache[slot].epoch==generation && creator_cache[slot].page==page){
        memcpy(s->creator,creator_cache[slot].items,sizeof(s->creator));s->creator_count=creator_cache[slot].count;
        s->creator_page=page;s->creator_total=creator_cache[slot].total;s->creator_more=creator_cache[slot].more;
        snprintf(s->creator_status,sizeof(s->creator_status),"第 %u 页 · %s",page,s->creator_more?"R2 下一页":"已到最后一页");snprintf(s->status,sizeof(s->status),"就绪");return;
    }
    long creator_http=0;int success=0;
    char url[1536],query[1024],raw[512];wili_https_response r={0};
    if(!s->creator_ready){
        snprintf(url,sizeof(url),"https://api.bilibili.com/x/web-interface/card?mid=%llu",(unsigned long long)s->mid);
        if(!request(url,cookie,NULL,generation,&r)){struct json_object *root=data_root(r.body,r.bytes);if(root){struct json_object *d=get(root,"data"),*card=get(d,"card");s->fans=(unsigned)num(d,"follower");text(s->up_name,sizeof(s->up_name),card,"name");text(s->up_face,sizeof(s->up_face),card,"face");json_object_put(root);}}
        wili_https_response_free(&r);
    }
    wili_bili_nav nav;int ok=!request("https://api.bilibili.com/x/web-interface/nav",cookie,NULL,generation,&r) && !wili_bili_parse_nav(r.body,r.bytes,&nav) && nav.keys_ready;
    uint64_t stamp=r.server_epoch?r.server_epoch:(uint64_t)time(NULL);wili_https_response_free(&r);
    if(ok){
        snprintf(raw,sizeof(raw),"mid=%llu&order=pubdate&pn=%u&ps=%u&wts=%llu",(unsigned long long)s->mid,page,BILI_DETAIL_VIDEOS,(unsigned long long)stamp);
        if(!wili_bili_sign_sorted(raw,nav.mixin,query,sizeof(query))){snprintf(url,sizeof(url),"https://api.bilibili.com/x/space/wbi/arc/search?%s",query);
            if(!request(url,cookie,NULL,generation,&r)){struct json_object *root=data_root(r.body,r.bytes);if(root){
                struct json_object *data=get(root,"data"),*list=get(get(data,"list"),"vlist");
                if(list && json_object_is_type(list,json_type_array)){
                    int count=parse_videos(list,creator_cache[slot].items,BILI_DETAIL_VIDEOS);
                    unsigned total=(unsigned)num(get(data,"page"),"count");
                    int more=total?(uint64_t)page*BILI_DETAIL_VIDEOS<total:count==BILI_DETAIL_VIDEOS;
                    creator_cache[slot].epoch=generation;creator_cache[slot].page=page;creator_cache[slot].total=total;
                    creator_cache[slot].more=more;creator_cache[slot].count=count;
                    if(count || page==1){
                        s->creator_count=count;memcpy(s->creator,creator_cache[slot].items,sizeof(s->creator));
                        s->creator_page=page;s->creator_total=total;
                    }
                    s->creator_more=more;success=1;
                }
                json_object_put(root);
            }}
            creator_http=r.status;wili_https_response_free(&r);
        }
    }
    s->creator_ready=1;
    if(success)snprintf(s->creator_status,sizeof(s->creator_status),"第 %u 页 · %s",s->creator_page,s->creator_more?"R2 下一页":s->creator_count?"已到最后一页":"暂无投稿");
    else snprintf(s->creator_status,sizeof(s->creator_status),"加载失败，保留当前页；R2 重试或 △ 刷新");
    if(!success && (creator_http==403 || creator_http==412))snprintf(s->creator_status,sizeof(s->creator_status),"投稿接口受限（HTTP %ld），请稍后重试",creator_http);
    snprintf(s->status,sizeof(s->status),"%s",success?"就绪":"投稿加载失败，当前视频继续播放");
}
static void write_action(bili_detail_state *s,int kind,const char *cookie,const char *message,uint64_t parent,unsigned generation)
{
    int posted=0;
    char csrf[128],form[4096],encoded[3072];
    if(!s->aid || !s->mid || bili_detail_cookie_value(cookie,"bili_jct",csrf,sizeof(csrf)) || strlen(csrf)!=32 || strspn(csrf,"0123456789abcdefABCDEF")!=32){snprintf(s->status,sizeof(s->status),"请先扫码登录，再执行此操作");return;}
    const char *url=NULL;
    if(kind==BILI_DETAIL_FOLLOW){url="https://api.bilibili.com/x/relation/modify";snprintf(form,sizeof(form),"fid=%llu&act=%u&re_src=11&csrf=%s",(unsigned long long)s->mid,s->following?2:1,csrf);}
    else if(kind==BILI_DETAIL_LIKE){url="https://api.bilibili.com/x/web-interface/archive/like";snprintf(form,sizeof(form),"aid=%llu&like=%u&csrf=%s",(unsigned long long)s->aid,s->liked?2:1,csrf);}
    else{
        if(!message[0] || wili_url_encode(message,encoded,sizeof(encoded))<0){snprintf(s->status,sizeof(s->status),"评论为空或过长");return;}
        url="https://api.bilibili.com/x/v2/reply/add";
        snprintf(form,sizeof(form),"oid=%llu&type=1&plat=1&message=%s&root=%llu&parent=%llu&csrf=%s",(unsigned long long)s->aid,encoded,(unsigned long long)parent,(unsigned long long)parent,csrf);
    }
    wili_https_response r={0};int rc=request(url,cookie,form,generation,&r);struct json_object *root=rc?NULL:wili_bili_json_parse(r.body,r.bytes);
    struct json_object *code=root?get(root,"code"):NULL;int result=code?json_object_get_int(code):-1;
    if(!code || result){snprintf(s->status,sizeof(s->status),"操作未确认成功（%d），请检查登录或官方客户端提示",result);}
    else if(kind==BILI_DETAIL_COMMENT){struct json_object *d=get(root,"data");
        if(num(d,"success_action") || (!num(d,"rpid") && !num(get(d,"reply"),"rpid")))snprintf(s->status,sizeof(s->status),"评论需进一步验证，请使用官方客户端");
        else{snprintf(s->status,sizeof(s->status),"评论已提交，可能需要审核");s->comment_page=1;posted=1;}
    }else if(kind==BILI_DETAIL_FOLLOW){s->following=!s->following;snprintf(s->status,sizeof(s->status),"%s",s->following?"已关注":"已取消关注");}
    else{s->liked=!s->liked;snprintf(s->status,sizeof(s->status),"%s",s->liked?"已点赞":"已取消点赞");}
    if(root)json_object_put(root);
    wili_https_response_free(&r);
    if(posted)comments(s,cookie,generation);
}
static THREAD_RETURN worker(void *arg)
{
    (void)arg;bili_detail_state *job=malloc(sizeof(*job));
    if(job)while(__sync_add_and_fetch(&running,0)){
        int kind=0;unsigned generation=0;char cookie[1024],message[1024];uint64_t parent=0;
        LOCK();if(pending){kind=pending;pending=0;generation=epoch;*job=state;parent=reply_target;snprintf(cookie,sizeof(cookie),"%s",account);snprintf(message,sizeof(message),"%s",draft);}UNLOCK();
        if(!kind){SLEEP();continue;}
        if(kind==1)load(job,cookie,generation);
        else if(kind==BILI_DETAIL_CREATOR || kind==BILI_DETAIL_CREATOR_NEXT || kind==BILI_DETAIL_CREATOR_PREV)creator(job,cookie,generation,kind);
        else if(kind==BILI_DETAIL_RELATED_NEXT || kind==BILI_DETAIL_RELATED_PREV)related_page(job,kind==BILI_DETAIL_RELATED_NEXT?1:-1,generation);
        else if(kind==BILI_DETAIL_COMMENTS_NEXT || kind==BILI_DETAIL_COMMENTS_FIRST){job->comment_page=kind==BILI_DETAIL_COMMENTS_FIRST?1:job->comment_page+1;comments(job,cookie,generation);}
        else write_action(job,kind,cookie,message,parent,generation);
        publish(job,generation,1);
    }
    free(job);
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    sysThreadExit(0);
#else
    return NULL;
#endif
}
int bili_detail_init(const char *ca,void (*log)(const char*))
{
    if(started)return 0;
    ca_path=ca;logger=log;running=1;int rc;
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    sys_mutex_attr_t attr;sysMutexAttrInitialize(attr);if(sysMutexCreate(&lock,&attr)){running=0;return -1;}
    rc=sysThreadCreate(&thread,worker,NULL,1150,131072,THREAD_JOINABLE,"bibi-detail");
#else
    rc=pthread_create(&thread,NULL,worker,NULL);
#endif
    if(rc){running=0;return -1;}started=1;return 0;
}
void bili_detail_open(const char *bv,const char *cookie)
{
    if(!started || !wili_bili_valid_bvid(bv))return;
    LOCK();epoch++;memset(&state,0,sizeof(state));snprintf(state.bvid,sizeof(state.bvid),"%s",bv);snprintf(account,sizeof(account),"%s",cookie?cookie:"");
    snprintf(state.status,sizeof(state.status),"正在读取详情…");state.busy=1;pending=1;allowed=0;revision++;UNLOCK();
}
void bili_detail_allow_network(int value){__sync_lock_test_and_set(&allowed,value!=0);}
int bili_detail_pump(bili_detail_state *out)
{
    if(!started || !out)return 0;
    int changed=0;LOCK();if(seen!=revision){*out=state;seen=revision;changed=1;}UNLOCK();return changed;
}
int bili_detail_action(int action,const char *message,uint64_t parent)
{
    if(!started || action<2 || action>BILI_DETAIL_RELATED_PREV)return -1;
    LOCK();
    if(state.busy || !state.ready){UNLOCK();return -1;}
    if(action==BILI_DETAIL_COMMENTS_NEXT && !state.comments_more){UNLOCK();return -1;}
    if(action>=BILI_DETAIL_FOLLOW && action<=BILI_DETAIL_COMMENT && !account[0]){snprintf(state.status,sizeof(state.status),"请先扫码登录");revision++;UNLOCK();return -1;}
    if(action==BILI_DETAIL_CREATOR_NEXT && state.creator_page && !state.creator_more){
        snprintf(state.creator_status,sizeof(state.creator_status),"已到最后一页");revision++;UNLOCK();return -1;
    }
    if(action==BILI_DETAIL_CREATOR_PREV && state.creator_page<=1){
        snprintf(state.creator_status,sizeof(state.creator_status),"已经是第一页");revision++;UNLOCK();return -1;
    }
    snprintf(draft,sizeof(draft),"%s",message?message:"");reply_target=parent;pending=action;state.busy=1;revision++;
    snprintf(state.status,sizeof(state.status),"正在处理…");UNLOCK();return 0;
}
void bili_detail_cancel(void){if(started){LOCK();epoch++;pending=0;state.busy=0;UNLOCK();}}
void bili_detail_shutdown(void)
{
    if(!started)return;
    bili_detail_cancel();
    __sync_lock_test_and_set(&running,0);
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    u64 result;sysThreadJoin(thread,&result);sysMutexDestroy(lock);
#else
    pthread_join(thread,NULL);
#endif
    started=0;
}
