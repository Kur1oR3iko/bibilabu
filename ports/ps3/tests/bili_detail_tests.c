#define _DEFAULT_SOURCE
#include "bili_detail.h"
#include "https_request.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static volatile unsigned posts,gets,creator_requests,fail_creator;
static const char *cookie="SESSDATA=fixture; DedeUserID=7; bili_jct=0123456789abcdef0123456789abcdef";
static void log_line(const char *s){(void)s;}
static void response(wili_https_response *r,const char *json){memset(r,0,sizeof(*r));r->body=strdup(json);r->bytes=strlen(json);r->status=200;r->server_epoch=1702204169;}
static void videos(wili_https_response *r,unsigned page,int creator)
{
    char body[8192];size_t n=0;
    if(creator)n=(size_t)snprintf(body,sizeof(body),"{\"code\":0,\"data\":{\"page\":{\"count\":18},\"list\":{\"vlist\":[");
    else n=(size_t)snprintf(body,sizeof(body),"{\"code\":0,\"data\":[");
    unsigned first=creator?(page-1)*8:0,count=creator?(page==3?2:8):18;
    for(unsigned i=0;i<count;i++)n+=(size_t)snprintf(body+n,sizeof(body)-n,"%s{\"bvid\":\"BV1XzJ86dEV3\",\"title\":\"video-%u\"}",i?",":"",first+i);
    snprintf(body+n,sizeof(body)-n,"%s",creator?"]}}}":"]}");response(r,body);
}
int wili_https_get_cookie(const char *url,const char *ca,const char *range,const char *account,size_t cap,int (*cancel)(void*),void *arg,wili_https_response *r,void (*log)(const char*))
{
    (void)ca;(void)range;(void)account;(void)cap;(void)log;gets++;
    for(int i=0;i<5 && !cancel(arg);i++)usleep(1000);
    if(cancel(arg)){memset(r,0,sizeof(*r));return -1;}
    if(strstr(url,"/view?")){response(r,"{\"code\":0,\"data\":{\"bvid\":\"BV17x411w7KC\",\"aid\":170001,\"cid\":279786,\"title\":\"中文视频\",\"owner\":{\"mid\":42,\"name\":\"示例UP\"},\"stat\":{\"view\":12,\"like\":3,\"reply\":7}}}");}
    else if(strstr(url,"/reply?"))response(r,"{\"code\":0,\"data\":{\"page\":{\"count\":7},\"replies\":[{\"rpid\":9001,\"mid\":99,\"member\":{\"uname\":\"评论用户\"},\"content\":{\"message\":\"你好，评论区\"}}]}}");
    else if(strstr(url,"/related?"))videos(r,1,0);
    else if(strstr(url,"/card?"))response(r,"{\"code\":0,\"data\":{\"card\":{\"name\":\"示例UP\"},\"follower\":100}}");
    else if(strstr(url,"/nav"))response(r,"{\"code\":0,\"data\":{\"wbi_img\":{\"img_url\":\"https://i0.hdslb.com/bfs/wbi/7cd084941338484aae1ad9425b84077c.png\",\"sub_url\":\"https://i0.hdslb.com/bfs/wbi/4932caff0ff746eab6f01bf08b70ac45.png\"}}}");
    else if(strstr(url,"/arc/search?")){
        assert(strstr(url,"w_rid=") && strstr(url,"mid=42") && strstr(url,"ps=8&"));creator_requests++;
        unsigned page=strstr(url,"pn=3&")?3:strstr(url,"pn=2&")?2:1;
        if(fail_creator){response(r,"{}");r->status=412;}else videos(r,page,1);
    }
    else if(strstr(url,"/relation?"))response(r,"{\"code\":0,\"data\":{\"attribute\":0}}");
    else response(r,"{\"code\":0,\"data\":0}");
    return 0;
}
int wili_https_post_form(const char *url,const char *ca,const char *account,const char *form,size_t cap,int (*cancel)(void*),void *arg,wili_https_response *r,void (*log)(const char*))
{
    (void)ca;(void)cap;(void)cancel;(void)arg;(void)log;assert(account && strstr(account,"SESSDATA=fixture"));
    assert(strstr(form,"csrf=0123456789abcdef0123456789abcdef"));posts++;
    if(strstr(url,"reply/add")){assert(strstr(form,"oid=170001") && strstr(form,"message=%E4%BD%A0%E5%A5%BD") && strstr(form,"parent=9001"));response(r,"{\"code\":0,\"data\":{\"rpid\":12345,\"success_action\":0}}");}
    else {assert(strstr(form,"fid=42") || strstr(form,"aid=170001"));response(r,"{\"code\":0}");}return 0;
}
void wili_https_response_free(wili_https_response *r){free(r->body);r->body=NULL;}
static bili_detail_state view;
static void wait_ready(void){for(int i=0;i<5000;i++){bili_detail_pump(&view);if(!view.busy && view.ready)return;usleep(1000);}assert(0);}
int main(void)
{
    char v[64];assert(!bili_detail_cookie_value(cookie,"bili_jct",v,sizeof(v)));assert(strlen(v)==32);
    assert(bili_detail_cookie_value("notbili_jct=wrong","bili_jct",v,sizeof(v))<0);
    assert(!bili_detail_init(NULL,log_line));bili_detail_open("BV17x411w7KC",cookie);bili_detail_allow_network(1);wait_ready();
    assert(view.aid==170001 && view.mid==42 && view.related_count==8 && view.comment_count==1 && posts==0);
    assert(!bili_detail_action(BILI_DETAIL_CREATOR,NULL,0));wait_ready();assert(view.creator_ready && view.creator_count==8 && view.fans==100 && posts==0);
    assert(view.related_page==1 && view.related_pages==3 && !strcmp(view.related[0].title,"video-0"));
    unsigned reads=gets;
    assert(!bili_detail_action(BILI_DETAIL_RELATED_NEXT,NULL,0));wait_ready();assert(view.related_page==2 && !strcmp(view.related[0].title,"video-8") && gets==reads);
    assert(!bili_detail_action(BILI_DETAIL_RELATED_NEXT,NULL,0));wait_ready();assert(view.related_page==3 && view.related_count==2 && !strcmp(view.related[0].title,"video-16"));
    assert(!bili_detail_action(BILI_DETAIL_RELATED_NEXT,NULL,0));wait_ready();assert(view.related_page==3 && gets==reads);
    assert(!bili_detail_action(BILI_DETAIL_RELATED_PREV,NULL,0));wait_ready();assert(view.related_page==2 && view.related_count==8);
    assert(view.creator_page==1 && view.creator_more && creator_requests==1);
    fail_creator=1;assert(!bili_detail_action(BILI_DETAIL_CREATOR_NEXT,NULL,0));wait_ready();assert(view.creator_page==1 && view.creator_count==8 && strstr(view.creator_status,"412"));
    fail_creator=0;assert(!bili_detail_action(BILI_DETAIL_CREATOR_NEXT,NULL,0));wait_ready();assert(view.creator_page==2 && !strcmp(view.creator[0].title,"video-8"));
    assert(!bili_detail_action(BILI_DETAIL_CREATOR_NEXT,NULL,0));wait_ready();assert(view.creator_page==3 && view.creator_count==2 && !view.creator_more);
    assert(bili_detail_action(BILI_DETAIL_CREATOR_NEXT,NULL,0)<0);
    reads=gets;assert(!bili_detail_action(BILI_DETAIL_CREATOR_PREV,NULL,0));wait_ready();assert(view.creator_page==2 && gets==reads);
    assert(!bili_detail_action(BILI_DETAIL_CREATOR_NEXT,NULL,0));wait_ready();assert(view.creator_page==3 && gets==reads && posts==0);
    assert(!bili_detail_action(BILI_DETAIL_COMMENTS_NEXT,NULL,0));wait_ready();assert(view.comment_page==2 && posts==0);
    assert(!bili_detail_action(BILI_DETAIL_FOLLOW,NULL,0));wait_ready();assert(view.following && posts==1);
    assert(!bili_detail_action(BILI_DETAIL_LIKE,NULL,0));wait_ready();assert(view.liked && posts==2);
    assert(!bili_detail_action(BILI_DETAIL_COMMENT,"你好",9001));wait_ready();assert(posts==3 && strstr(view.status,"已提交"));
    bili_detail_open("BV17x411w7KC",NULL);bili_detail_allow_network(1);wait_ready();assert(bili_detail_action(BILI_DETAIL_FOLLOW,NULL,0)<0 && posts==3);
    assert(!bili_detail_action(BILI_DETAIL_CREATOR,NULL,0));wait_ready();assert(view.creator_page==1);
    assert(!bili_detail_action(BILI_DETAIL_CREATOR_NEXT,NULL,0));wait_ready();assert(view.creator_page==2 && posts==3);
    bili_detail_cancel();bili_detail_shutdown();puts("PASS details, related and creator pagination/cache/end/retry/guest navigation, comments/page, signed UP submissions; no implicit writes, CSRF-bound explicit follow/like/reply, guest refusal");
}
