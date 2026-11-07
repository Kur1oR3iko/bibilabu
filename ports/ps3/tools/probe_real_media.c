// SPDX-License-Identifier: GPL-3.0-or-later
// Host-only probe: fresh Bilibili playurl via the existing WBI/HTTPS C code,
// then bounded Range reads of the real video/audio MP4 initialization data.
// No signed URL is written to the report or logs. No retry on 403/412.
#include "bili_api.h"
#include "bili_probe.h"
#include "https_request.h"
#include <json-c/json.h>
#include <curl/curl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HEAD_BYTES 262144u

static FILE *report;
static const char *outdir;

static void log_line(const char *line){puts(line);}
static void rep(const char *fmt,...)
{
    va_list ap;va_start(ap,fmt);vfprintf(report,fmt,ap);va_end(ap);fputc('\n',report);
}
static const char *jstr(struct json_object *o,const char *key)
{
    struct json_object *v=NULL;
    if(!o || !json_object_object_get_ex(o,key,&v))return "";
    if(!json_object_is_type(v,json_type_string))return "";
    return json_object_get_string(v);
}
static int64_t jint(struct json_object *o,const char *key)
{
    struct json_object *v=NULL;
    if(!o || !json_object_object_get_ex(o,key,&v) || !json_object_is_type(v,json_type_int))return -1;
    return json_object_get_int64(v);
}
static const char *host_only(const char *url)
{
    static char buf[256];buf[0]=0;
    if(!url || strncmp(url,"https://",8))return "?";
    const char *p=url+8,*slash=strchr(p,'/');size_t n=slash ? (size_t)(slash-p) : strlen(p);
    if(n>sizeof(buf)-1)n=sizeof(buf)-1;
    memcpy(buf,p,n);buf[n]=0;return buf;
}
static void describe(struct json_object *dash,const char *track)
{
    struct json_object *arr=NULL;
    if(!json_object_object_get_ex(dash,track,&arr) || !json_object_is_type(arr,json_type_array))return;
    size_t n=json_object_array_length(arr);size_t i;
    rep("  \"%s_entries\": %u,",track,(unsigned)n);
    for(i=0;i<n && i<12;i++){
        struct json_object *e=json_object_array_get_idx(arr,i),*seg=NULL;
        const char *init="",*index="";
        if(json_object_object_get_ex(e,"SegmentBase",&seg) && json_object_is_type(seg,json_type_object)){
            struct json_object *sb=NULL;
            if(json_object_object_get_ex(seg,"Initialization",&sb) && json_object_is_type(sb,json_type_string))init=json_object_get_string(sb);
            index=jstr(seg,"indexRange");
        }
        rep("  \"%s[%u]\": {\"id\":%lld,\"width\":%lld,\"height\":%lld,\"bandwidth\":%lld,\"codecs\":\"%s\",\"frame_rate\":\"%s\",\"sar\":\"%s\",\"mimeType\":\"%s\",\"codecid\":%lld,\"SegmentBase_Initialization\":\"%s\",\"indexRange\":\"%s\",\"host\":\"%s\"},",
            track,(unsigned)i,(long long)jint(e,"id"),(long long)jint(e,"width"),(long long)jint(e,"height"),(long long)jint(e,"bandwidth"),
            jstr(e,"codecs"),jstr(e,"frame_rate"),jstr(e,"sar"),jstr(e,"mimeType"),(long long)jint(e,"codecid"),init,index,
            host_only(jstr(e,"baseUrl")));
    }
}
static int dump(const char *name,const char *url,uint64_t total,int tail)
{
    char range[80],file[512],line[384];wili_https_response r={0};
    uint64_t first=tail ? (total>HEAD_BYTES ? total-HEAD_BYTES : 0) : 0;
    uint64_t last=tail ? total-1 : (total>HEAD_BYTES ? HEAD_BYTES-1 : total-1);
    snprintf(range,sizeof(range),"%llu-%llu",(unsigned long long)first,(unsigned long long)last);
    snprintf(line,sizeof(line),"MEDIA_RANGE_BEGIN track=%s range=%s",name,range);log_line(line);
    int rc=wili_https_get(url,NULL,range,HEAD_BYTES+1,NULL,NULL,&r,log_line);
    int ok=!rc && r.tls_verified && r.status==206 && r.has_range && r.range_start==first &&
           r.range_end==last && r.range_total==total && r.bytes==(size_t)(last-first+1);
    rep("  \"%s_range\": {\"request\":\"%s\",\"curl\":%d,\"http\":%ld,\"tls_verified\":%d,\"content_range_start\":%llu,\"content_range_end\":%llu,\"content_range_total\":%llu,\"bytes\":%u,\"accepted\":%d},",
        name,range,r.curl_code,r.status,r.tls_verified,(unsigned long long)r.range_start,(unsigned long long)r.range_end,
        (unsigned long long)r.range_total,(unsigned)r.bytes,ok);
    if(r.status==403 || r.status==412){log_line("MEDIA_RANGE_PRECONDITION_OR_FORBIDDEN stop_no_retry");wili_https_response_free(&r);return -1;}
    if(ok){
        snprintf(file,sizeof(file),"%s/%s_%s.bin",outdir,name,tail ? "tail" : "head");
        FILE *f=fopen(file,"wb");
        if(f){fwrite(r.body,1,r.bytes,f);fclose(f);}
        snprintf(line,sizeof(line),"MEDIA_RANGE_SAVED track=%s bytes=%u",name,(unsigned)r.bytes);log_line(line);
    }
    wili_https_response_free(&r);return ok ? 0 : -1;
}

int main(int argc,char **argv)
{
    if(argc<3){fprintf(stderr,"usage: probe_real_media <bvid> <output-dir>\n");return 2;}
    const char *bvid=argv[1];outdir=argv[2];
    if(!wili_bili_valid_bvid(bvid)){fprintf(stderr,"invalid bvid\n");return 2;}
    if(curl_global_init(CURL_GLOBAL_DEFAULT))return 2;
    char path[512];snprintf(path,sizeof(path),"%s/probe.json",outdir);
    report=fopen(path,"w");if(!report)return 2;
    rep("{");rep("  \"probe\": \"fresh_Bilibili_DASH_media\", \"bvid\": \"%s\",",bvid);
    rep("  \"generated_epoch\": %lld,",(long long)time(NULL));
    wili_https_response r={0};char url[4096],query[1024];int rc;

    rc=wili_https_get("https://api.bilibili.com/x/web-interface/nav",NULL,NULL,262144,NULL,NULL,&r,log_line);
    wili_bili_nav nav;
    if(rc || r.status!=200 || !r.tls_verified || wili_bili_parse_nav(r.body,r.bytes,&nav) || !nav.keys_ready){
        log_line("NAV_FAILED stop_no_retry");return 1;
    }
    uint64_t epoch=r.server_epoch ? r.server_epoch : (uint64_t)time(NULL);
    rep("  \"nav\": {\"api_code\":%d,\"keys_ready\":1,\"https_date_epoch\":%llu},",nav.code,(unsigned long long)r.server_epoch);
    wili_https_response_free(&r);

    snprintf(url,sizeof(url),"https://api.bilibili.com/x/web-interface/view?bvid=%s",bvid);
    rc=wili_https_get(url,NULL,NULL,262144,NULL,NULL,&r,log_line);
    wili_bili_video video;
    if(rc || r.status!=200 || !r.tls_verified || wili_bili_parse_video(r.body,r.bytes,&video) || video.code){
        log_line("VIEW_FAILED stop_no_retry");return 1;
    }
    rep("  \"view\": {\"bvid\":\"%s\",\"aid\":%llu,\"cid\":%llu,\"duration_field\":%u},",
        video.bvid,(unsigned long long)video.aid,(unsigned long long)video.cid,video.duration);
    wili_https_response_free(&r);

    if(wili_bili_play_query(video.bvid,video.cid,epoch,nav.mixin,query,sizeof(query))){log_line("WBI_QUERY_FAILED");return 1;}
    snprintf(url,sizeof(url),"https://api.bilibili.com/x/player/wbi/playurl?%s",query);
    rc=wili_https_get(url,NULL,NULL,262144,NULL,NULL,&r,log_line);
    if(rc || r.status!=200 || !r.tls_verified){log_line("PLAYURL_HTTPS_FAILED stop_no_retry");return 1;}
    wili_bili_play play;
    if(wili_bili_parse_play(r.body,r.bytes,&play) || play.code){log_line("PLAYURL_PARSE_OR_API_FAILED stop_no_retry");return 1;}
    rep("  \"playurl\": {\"api_code\":%d,\"requested_qn\":32,\"returned_qn\":%u,\"selected_video_id\":%u,\"selected_size\":[%u,%u],\"selected_audio_id\":%u,\"video_host\":\"%s\",\"audio_host\":\"%s\"},",
        play.code,play.returned_quality,play.video_id,play.width,play.height,play.audio_id,
        host_only(play.video_url),host_only(play.audio_url));

    struct json_object *root=json_tokener_parse(r.body),*data=NULL,*dash=NULL;
    if(root && json_object_object_get_ex(root,"data",&data) && json_object_object_get_ex(data,"dash",&dash)){
        rep("  \"dash\": {\"duration\":%lld,\"minBufferTime\":%lld,\"min_buffer_time\":%lld,",
            (long long)jint(dash,"duration"),(long long)jint(dash,"minBufferTime"),(long long)jint(dash,"min_buffer_time"));
        describe(dash,"video");describe(dash,"audio");
        rep("  \"dash_end\": 1},");
    }
    if(root)json_object_put(root);
    wili_https_response_free(&r);

    // Discover each track's total size with a bounded first range (no signed URL logged).
    wili_https_response probe={0};uint64_t video_total=0,audio_total=0;
    rc=wili_https_get(play.video_url,NULL,"0-4095",8192,NULL,NULL,&probe,log_line);
    if(!rc && probe.status==206 && probe.has_range && probe.tls_verified)video_total=probe.range_total;
    rep("  \"video_total_bytes\": %llu,",(unsigned long long)video_total);
    wili_https_response_free(&probe);
    rc=wili_https_get(play.audio_url,NULL,"0-4095",8192,NULL,NULL,&probe,log_line);
    if(!rc && probe.status==206 && probe.has_range && probe.tls_verified)audio_total=probe.range_total;
    rep("  \"audio_total_bytes\": %llu,",(unsigned long long)audio_total);
    wili_https_response_free(&probe);
    if(!video_total || !audio_total){log_line("CDN_TOTAL_UNAVAILABLE stop_no_retry");return 1;}

    int bad=dump("video",play.video_url,video_total,0)|dump("audio",play.audio_url,audio_total,0);
    if(video_total>HEAD_BYTES)bad|=dump("video",play.video_url,video_total,1);
    if(audio_total>HEAD_BYTES)bad|=dump("audio",play.audio_url,audio_total,1);
    rep("  \"result\": \"%s\"",bad ? "RANGE_DUMP_INCOMPLETE" : "RANGE_DUMP_COMPLETE");
    rep("}");
    fclose(report);
    curl_global_cleanup();
    log_line(bad ? "REAL_MEDIA_PROBE_INCOMPLETE" : "REAL_MEDIA_PROBE_COMPLETE");
    return bad ? 1 : 0;
}
