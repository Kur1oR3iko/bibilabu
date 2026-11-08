// SPDX-License-Identifier: GPL-3.0-or-later
#include "bili_probe.h"
#include "bili_api.h"
#include "https_request.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
static struct {unsigned revision;char bvid[16],lines[6][128];void (*log)(const char*);} p;
static void status(unsigned row,const char *text)
{
    snprintf(p.lines[row],sizeof(p.lines[row]),"%.127s",text);p.revision++;
}
int wili_bili_probe_prepare(const char *bv,void (*log)(const char*))
{
    memset(&p,0,sizeof(p));p.log=log;p.revision=1;
    status(0,"HTTPS / WBI: waiting");status(1,"Video info: waiting");status(2,"Play URL: waiting");status(3,"Video CDN: waiting");status(4,"Audio CDN: waiting");status(5,"CIRCLE/OPTIONS: exit");
    if(!wili_bili_valid_bvid(bv)){status(0,"Invalid BVID configuration");log("BILI_CONFIG_INVALID_BVID");return -1;}
    strcpy(p.bvid,bv);return 0;
}
static int response_ok(wili_https_response *r,int rc,unsigned row,const char *step)
{
    if(rc || r->status!=200 || !r->tls_verified){
        char line[128];snprintf(line,sizeof(line),"%s: curl=%d HTTP=%ld TLS=%s",step,r->curl_code,r->status,r->tls_verified ? "OK" : "FAIL");status(row,line);return 0;
    }
    return 1;
}
static int cdn(const char *url,const char *ca,int (*cancel)(void*),void *arg,unsigned row,const char *name)
{
    char line[160];snprintf(line,sizeof(line),"%s CDN: requesting HTTPS range",name);status(row,line);
    wili_https_response r={0};int rc=wili_https_get(url,ca,"0-4095",8192,cancel,arg,&r,p.log);
    int ok=!rc && r.tls_verified && r.status==206 && r.has_range && !r.range_start && r.range_end<=4095 && r.bytes==r.range_end+1;
    if(ok){
        snprintf(line,sizeof(line),"%s CDN: HTTPS range OK (%u bytes)",name,(unsigned)r.bytes);status(row,line);
        snprintf(line,sizeof(line),"BILI_CDN_RANGE_OK track=%s bytes=%u total=%llu",name,(unsigned)r.bytes,(unsigned long long)r.range_total);p.log(line);
    }else {snprintf(line,sizeof(line),"%s CDN: curl=%d HTTP=%ld TLS=%s",name,r.curl_code,r.status,r.tls_verified ? "OK" : "FAIL");status(row,line);p.log("BILI_CDN_RANGE_FAILED");}
    wili_https_response_free(&r);return ok ? 0 : -1;
}
int wili_bili_probe_run(const char *ca,int (*cancel)(void*),void *arg)
{
    if(!p.bvid[0])return -1;
    char line[384],url[4096],query[1024];wili_https_response r={0};wili_bili_nav nav;
    status(0,"HTTPS / WBI: requesting nav");
    int rc=wili_https_get("https://api.bilibili.com/x/web-interface/nav",ca,NULL,262144,cancel,arg,&r,p.log);
    if(!response_ok(&r,rc,0,"NAV")){wili_https_response_free(&r);return -1;}
    uint64_t server_time=r.server_epoch;
    if(wili_bili_parse_nav(r.body,r.bytes,&nav) || !nav.keys_ready){status(0,"NAV: HTTPS OK, WBI keys unavailable");p.log("BILI_NAV_JSON_OR_KEYS_FAILED");wili_https_response_free(&r);return -1;}
    snprintf(line,sizeof(line),"BILI_NAV_OK api_code=%d login_required_for_keys=0",nav.code);p.log(line);status(0,"HTTPS / WBI: OK (anonymous)");wili_https_response_free(&r);
    if(cancel && cancel(arg))return -1;
    status(1,"Video info: requesting HTTPS");snprintf(url,sizeof(url),"https://api.bilibili.com/x/web-interface/view?bvid=%s",p.bvid);
    rc=wili_https_get(url,ca,NULL,262144,cancel,arg,&r,p.log);
    if(!response_ok(&r,rc,1,"VIEW")){wili_https_response_free(&r);return -1;}
    wili_bili_video video;
    if(wili_bili_parse_video(r.body,r.bytes,&video)){status(1,"Video info: malformed JSON/schema");p.log("BILI_VIEW_JSON_FAILED");wili_https_response_free(&r);return -1;}
    wili_https_response_free(&r);
    if(video.code){snprintf(line,sizeof(line),"Video info: API code %d",video.code);status(1,line);p.log(line);return -1;}
    snprintf(line,sizeof(line),"Video info: OK CID=%llu",(unsigned long long)video.cid);status(1,line);
    if(strcmp(video.bvid,p.bvid)){status(1,"Video info: BVID mismatch");p.log("BILI_VIEW_BVID_MISMATCH");return -1;}
    snprintf(line,sizeof(line),"BILI_VIDEO_INFO bvid=%s aid=%llu cid=%llu duration=%u title=%s",video.bvid,(unsigned long long)video.aid,(unsigned long long)video.cid,video.duration,video.title);p.log(line);
    time_t local=time(NULL);uint64_t epoch=server_time ? server_time : local>0 ? (uint64_t)local : 0;
    snprintf(line,sizeof(line),"BILI_WBI_CLOCK source=%s epoch=%llu local_epoch=%lld",server_time ? "verified_HTTPS_Date" : "console",(unsigned long long)epoch,(long long)local);p.log(line);
    if(wili_bili_play_query(video.bvid,video.cid,epoch,nav.mixin,query,sizeof(query))){status(2,"Play URL: WBI/clock validation failed");p.log("BILI_WBI_QUERY_FAILED");return -1;}
    if(cancel && cancel(arg))return -1;
    status(2,"Play URL: requesting signed HTTPS");snprintf(url,sizeof(url),"https://api.bilibili.com/x/player/wbi/playurl?%s",query);
    rc=wili_https_get(url,ca,NULL,262144,cancel,arg,&r,p.log);
    if(!response_ok(&r,rc,2,"PLAYURL")){wili_https_response_free(&r);return -1;}
    wili_bili_play play;
    if(wili_bili_parse_play(r.body,r.bytes,&play)){status(2,"Play URL: malformed JSON/schema");p.log("BILI_PLAY_JSON_FAILED");wili_https_response_free(&r);return -1;}
    wili_https_response_free(&r);
    if(play.code){snprintf(line,sizeof(line),"Play URL: API code %d",play.code);status(2,line);p.log(line);return -1;}
    if(!play.dash_ready){snprintf(line,sizeof(line),"Play URL: no compatible DASH (qn=%u)",play.returned_quality);status(2,line);p.log(line);return -1;}
    snprintf(line,sizeof(line),"Play URL: %ux%u AVC / AAC %u",play.width,play.height,play.audio_id);status(2,line);
    snprintf(line,sizeof(line),"BILI_DASH_SELECTED requested_qn=32 returned_qn=%u video_id=%u size=%ux%u audio_id=%u needs_SPS_ASC_probe=1",play.returned_quality,play.video_id,play.width,play.height,play.audio_id);p.log(line);
    // Metadata/CDN connectivity only: actual Bili SPS/ASC and MP4 variants still
    // need adaptation before feeding these URLs into the fixed-fixture player.
    if(cancel && cancel(arg))return -1;
    int video_cdn=cdn(play.video_url,ca,cancel,arg,3,"Video");
    if(cancel && cancel(arg))return -1;
    int audio_cdn=cdn(play.audio_url,ca,cancel,arg,4,"Audio");
    status(5,"API check complete / CIRCLE: exit");
    p.log(!video_cdn && !audio_cdn ? "BILI_API_AND_CDN_PROBE_COMPLETE" : "BILI_API_OK_CDN_PROBE_INCOMPLETE");return video_cdn || audio_cdn ? -1 : 0;
}
void wili_bili_probe_network_error(void){status(0,"Network initialization failed: see log");}
unsigned wili_bili_probe_revision(void){return p.revision;}
const char *wili_bili_probe_bvid(void){return p.bvid;}
void wili_bili_probe_line(unsigned row,char *out,size_t cap){snprintf(out,cap,"%s",row<6 ? p.lines[row] : "");}
