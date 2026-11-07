// SPDX-License-Identifier: GPL-3.0-or-later
#include "bili_session.h"
#include "bili_api.h"
#include "https_request.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int range_read(void *arg,uint64_t at,void *out,size_t n)
{
    return wili_http_range_read((wili_http_range*)arg,at,out,n);
}
static void status(wili_bili_session *s,unsigned row,const char *fmt,...)
{
    va_list ap;va_start(ap,fmt);vsnprintf(s->lines[row],sizeof(s->lines[row]),fmt,ap);va_end(ap);s->revision++;
}
static int stopped(wili_bili_session *s,const char *step,wili_https_response *r,int rc)
{
    if(!rc && r->status==200 && r->tls_verified)return 0;
    s->network_error=r->curl_code==6 || r->curl_code==7 || r->curl_code==28;
    char line[192];
    snprintf(line,sizeof(line),"BILI_SESSION_HTTP_STOP step=%s curl=%d http=%ld tls_verified=%d",step,r->curl_code,r->status,r->tls_verified);
    s->log(line);
    if(r->status==403 || r->status==412)s->log("BILI_SESSION_403_412_REPORTED no_retry no_identity_change");
    status(s,0,"%s: HTTP %ld / TLS %s",step,r->status,r->tls_verified ? "OK" : "FAIL");
    return -1;
}
int wili_bili_cdn_probe_ok(const wili_https_response *r,int rc)
{
    if(!r || rc || !r->tls_verified || r->status!=206 || !r->has_range || r->overflow)return 0;
    if(r->range_start!=0 || !r->range_total || r->range_end<r->range_start || r->range_end>=r->range_total)return 0;
    if(!r->bytes || (uint64_t)r->bytes!=r->range_end-r->range_start+1)return 0;
    return 1;
}
static int track(wili_bili_session *s,const char *name,const char *url,const char *cookie,const char *ca,
                 int (*cancel)(void*),void *arg,int audio,wili_http_range *range,wili_mp4_dash *reader,
                 uint64_t *out_size,char *host,size_t host_cap)
{
    char line[256];wili_https_response r={0};
    int rc=wili_https_get_cookie(url,ca,"0-4095",cookie,8192,cancel,arg,&r,s->log);
    if(!wili_bili_cdn_probe_ok(&r,rc)){
        snprintf(line,sizeof(line),"BILI_SESSION_CDN_FAILED track=%s curl=%d http=%ld tls=%d has_range=%d start=%llu end=%llu total=%llu bytes=%u overflow=%d",
                 name,r.curl_code,r.status,r.tls_verified,r.has_range,
                 (unsigned long long)r.range_start,(unsigned long long)r.range_end,
                 (unsigned long long)r.range_total,(unsigned)r.bytes,r.overflow);s->log(line);
        if(r.status==403 || r.status==412)s->log("BILI_SESSION_403_412_REPORTED no_retry no_identity_change");
        wili_https_response_free(&r);return -1;
    }
    *out_size=r.range_total;
    snprintf(host,host_cap,"%s",r.host);
    snprintf(line,sizeof(line),"BILI_SESSION_CDN_RANGE track=%s bytes=%u total=%llu tls_verified=1",
             name,(unsigned)r.bytes,(unsigned long long)r.range_total);s->log(line);
    wili_https_response_free(&r);
    if(wili_http_range_open(range,url,*out_size,ca,cancel,arg,s->log)){s->log("BILI_SESSION_RANGE_OPEN_FAILED");return -1;}
    if(wili_mp4_dash_open(reader,range_read,range,*out_size,audio)){
        snprintf(line,sizeof(line),"BILI_SESSION_MP4_REJECTED track=%s stage=%d",name,reader->stage);s->log(line);return -1;
    }
    const wili_dash_info *i=wili_mp4_dash_info(reader);
    if(audio){
        snprintf(line,sizeof(line),"BILI_SESSION_AUDIO_CONFIG codec=%s rate=%u channels=%u asc=%02x%02x timescale=%u start_pts=%llu sidx_refs=%u sidx_ms=%llu",
                 "AAC-LC",i->sample_rate,i->channels,i->config[0],i->config_size>1 ? i->config[1] : 0,
                 i->timescale,(unsigned long long)i->start_pts,i->sidx_references,
                 (unsigned long long)(i->sidx_duration_ticks*1000/(i->sidx_timescale ? i->sidx_timescale : 1)));
    }else{
        snprintf(line,sizeof(line),"BILI_SESSION_VIDEO_CONFIG codec=%s profile=%u level=%u coded=%ux%u display=%ux%u sar=%u:%u refs=%u timescale=%u nal_len=%u start_pts=%llu empty_edit_ms=%llu max_cto=%u sidx_refs=%u sidx_ms=%llu",
                 "H264",i->sps.profile,i->level,i->coded_width,i->coded_height,i->display_width,i->display_height,
                 i->sar_width,i->sar_height,i->refs,i->timescale,i->nal_length_size,
                 (unsigned long long)i->start_pts,(unsigned long long)i->empty_edit_ms,i->max_cto_ticks,
                 i->sidx_references,(unsigned long long)(i->sidx_duration_ticks*1000/(i->sidx_timescale ? i->sidx_timescale : 1)));
    }
    s->log(line);
    return 0;
}
int wili_bili_session_open_ex(wili_bili_session *s,const char *bvid,unsigned qn,const char *cookie,const char *ca,
                              int (*cancel)(void*),void *arg,void (*log)(const char*))
{
    if(!s || !log || !wili_bili_valid_bvid(bvid))return -1;
    memset(s,0,sizeof(*s));s->log=log;strcpy(s->bvid,bvid);
    status(s,0,"HTTPS/WBI: waiting");status(s,1,"Video info: waiting");status(s,2,"Play URL: waiting");
    status(s,3,"Video CDN: waiting");status(s,4,"Audio CDN: waiting");status(s,5,"CIRCLE/OPTIONS: exit");
    wili_https_response r={0};char url[4096],query[1024],line[512];int rc;
    rc=wili_https_get_cookie("https://api.bilibili.com/x/web-interface/nav",ca,NULL,cookie,262144,cancel,arg,&r,log);
    wili_bili_nav nav;
    if(stopped(s,"NAV",&r,rc) || wili_bili_parse_nav(r.body,r.bytes,&nav) || !nav.keys_ready){wili_https_response_free(&r);return -1;}
    uint64_t epoch=r.server_epoch ? r.server_epoch : (uint64_t)time(NULL);
    snprintf(line,sizeof(line),"BILI_SESSION_NAV code=%d wbi_keys=1 clock=%s auth=%s",nav.code,r.server_epoch ? "verified_HTTPS_Date" : "console",cookie && *cookie ? "logged_in" : "guest");log(line);
    wili_https_response_free(&r);
    status(s,0,cookie && *cookie ? "HTTPS/WBI: OK (logged in)" : "HTTPS/WBI: OK (guest)");
    if(cancel && cancel(arg))return -1;
    snprintf(url,sizeof(url),"https://api.bilibili.com/x/web-interface/view?bvid=%s",bvid);
    rc=wili_https_get_cookie(url,ca,NULL,cookie,2u*1024u*1024u,cancel,arg,&r,log);
    wili_bili_video video;
    if(stopped(s,"VIEW",&r,rc) || wili_bili_parse_video(r.body,r.bytes,&video)){wili_https_response_free(&r);return -1;}
    wili_https_response_free(&r);
    if(video.code){snprintf(line,sizeof(line),"BILI_SESSION_VIEW_API_CODE code=%d",video.code);log(line);return -1;}
    if(strcmp(video.bvid,bvid)){log("BILI_SESSION_VIEW_BVID_MISMATCH");return -1;}
    s->aid=video.aid;s->cid=video.cid;s->view_duration=video.duration;s->page_duration=video.page_duration;
    snprintf(s->title,sizeof(s->title),"%s",video.title);
    snprintf(line,sizeof(line),"BILI_SESSION_VIEW bvid=%s aid=%llu cid=%llu view_duration=%u page_duration=%u title=%s",
             video.bvid,(unsigned long long)video.aid,(unsigned long long)video.cid,video.duration,video.page_duration,video.title);log(line);
    status(s,1,"Video info: CID=%llu",(unsigned long long)video.cid);
    unsigned req_qn = qn ? qn : 32;
    // 74 is Bilibili 720p60; keep the public tier at 720p while requesting all its variants.
    if(req_qn>=64)req_qn=74;
    if(wili_bili_play_query_qn(video.bvid,video.cid,epoch,nav.mixin,req_qn,query,sizeof(query))){log("BILI_SESSION_WBI_FAILED");return -1;}
    if(cancel && cancel(arg))return -1;
    snprintf(url,sizeof(url),"https://api.bilibili.com/x/player/wbi/playurl?%s",query);
    rc=wili_https_get_cookie(url,ca,NULL,cookie,262144,cancel,arg,&r,log);
    wili_bili_play play;
    // The visible playback policy currently ends at 720p (quality 64).
    if(stopped(s,"PLAYURL",&r,rc) || wili_bili_parse_play_limit(r.body,r.bytes,req_qn>=64?64:req_qn,&play)){wili_https_response_free(&r);return -1;}
    wili_https_response_free(&r);
    if(play.code){snprintf(line,sizeof(line),"BILI_SESSION_PLAYURL_API_CODE code=%d",play.code);log(line);return -1;}
    if(!play.dash_ready){log("BILI_SESSION_NO_COMPATIBLE_DASH");status(s,2,"Play URL: no AVC/AAC DASH");return -1;}
    memcpy(s->video_url,play.video_url,sizeof(s->video_url));memcpy(s->audio_url,play.audio_url,sizeof(s->audio_url));
    s->quality_mask=play.available_mask;s->advertised_fps=play.advertised_fps;
    s->returned_qn=play.returned_quality;s->video_id=play.video_id;s->audio_id=play.audio_id;
    s->width=play.width;s->height=play.height;s->dash_duration_ms=play.dash_duration_ms;
    s->accept_count = play.accept_count;
    for(unsigned i = 0; i < play.accept_count; i++) {
        s->accept_qualities[i] = play.accept_qualities[i];
        snprintf(s->accept_descriptions[i], sizeof(s->accept_descriptions[i]), "%s", play.accept_descriptions[i]);
    }
    strcpy(s->video_codecs,play.video_codecs);strcpy(s->audio_codecs,play.audio_codecs);
    char qualities[64]="";size_t quality_at=0;
    for(unsigned i=0;i<play.accept_count && quality_at<sizeof(qualities);i++){
        int written=snprintf(qualities+quality_at,sizeof(qualities)-quality_at,"%s%u",i?",":"",play.accept_qualities[i]);
        if(written<0 || (size_t)written>=sizeof(qualities)-quality_at)break;
        quality_at+=(size_t)written;
    }
    snprintf(line,sizeof(line),"BILI_SESSION_QUALITY requested_qn=%u returned_qn=%u accept=%s avc_mask=%u entries=%u avc=%u hevc=%u other=%u reject_geometry=%u reject_fps=%u reject_profile=%u reject_bitrate=%u advertised_fps=%u",
             req_qn,s->returned_qn,qualities,s->quality_mask,play.video_entries,play.avc_entries,play.hevc_entries,
             play.other_codec_entries,play.rejected_geometry,play.rejected_frame_rate,play.rejected_profile,play.rejected_bitrate,s->advertised_fps);log(line);
    snprintf(line,sizeof(line),"BILI_SESSION_PLAYURL requested_qn=%u returned_qn=%u video_id=%u size=%ux%u video_codecs=%s audio_id=%u audio_codecs=%s dash_duration_ms=%u advertised_fps=%u",
             req_qn,s->returned_qn,s->video_id,s->width,s->height,s->video_codecs,s->audio_id,s->audio_codecs,s->dash_duration_ms,s->advertised_fps);log(line);
    status(s,2,"Play URL: qn=%u %ux%u AVC/AAC",s->returned_qn,s->width,s->height);
    if(cancel && cancel(arg))return -1;
    if(track(s,"Video",s->video_url,cookie,ca,cancel,arg,0,&s->video_range,&s->video,&s->video_size,s->video_host,sizeof(s->video_host))){
        status(s,3,"Video CDN: FAILED (see log)");s->video_opened=1;return -1;}
    s->video_opened=1;
    unsigned measured_fps=0,measured_den=0;
    if(wili_mp4_dash_probe_rate(&s->video,0,&measured_fps,&measured_den)){
        s->log("BILI_SESSION_FRAME_RATE_FAILED invalid_sample_timing");status(s,3,"Video CDN: invalid sample timing");return -1;
    }
    snprintf(line,sizeof(line),"BILI_SESSION_FRAME_RATE measured=%u/%u ceiling=none",measured_fps,measured_den);s->log(line);
    status(s,3,"Video CDN: %llu bytes / %ux%u / %u fps",(unsigned long long)s->video_size,
           wili_mp4_dash_info(&s->video)->coded_width,wili_mp4_dash_info(&s->video)->coded_height,s->advertised_fps);
    if(cancel && cancel(arg))return -1;
    if(track(s,"Audio",s->audio_url,cookie,ca,cancel,arg,1,&s->audio_range,&s->audio,&s->audio_size,s->audio_host,sizeof(s->audio_host))){
        status(s,4,"Audio CDN: FAILED (see log)");s->audio_opened=1;return -1;}
    s->audio_opened=1;
    const wili_dash_info *vi=wili_mp4_dash_info(&s->video),*ai=wili_mp4_dash_info(&s->audio);
    uint64_t video_ms=vi->sidx_duration_ticks*1000/(vi->sidx_timescale ? vi->sidx_timescale : 1);
    uint64_t audio_ms=ai->sidx_duration_ticks*1000/(ai->sidx_timescale ? ai->sidx_timescale : 1);
    s->duration_ms=video_ms<audio_ms ? video_ms : audio_ms;
    if(!s->duration_ms)s->duration_ms=(uint64_t)s->dash_duration_ms;
    status(s,4,"Audio CDN: %llu bytes / %u Hz",(unsigned long long)s->audio_size,ai->sample_rate);
    snprintf(line,sizeof(line),"BILI_SESSION_READY bvid=%s cid=%llu requested_qn=%u returned_qn=%u advertised_fps=%u size=%ux%u coded=%ux%u level=%u profile=%u refs=%u reorder_cto=%u sample_rate=%u video_bytes=%llu audio_bytes=%llu sidx_video_ms=%llu sidx_audio_ms=%llu dash_ms=%u page_duration=%u view_duration=%u duration_ms=%llu",
             s->bvid,(unsigned long long)s->cid,req_qn,s->returned_qn,s->advertised_fps,s->width,s->height,vi->coded_width,vi->coded_height,
             vi->level,vi->sps.profile,vi->refs,vi->max_cto_ticks,ai->sample_rate,
             (unsigned long long)s->video_size,(unsigned long long)s->audio_size,
             (unsigned long long)video_ms,(unsigned long long)audio_ms,s->dash_duration_ms,s->page_duration,s->view_duration,
             (unsigned long long)s->duration_ms);log(line);
    status(s,5,"READY: CROSS pause / CIRCLE exit");
    s->ready=1;return 0;
}
int wili_bili_session_open(wili_bili_session *s,const char *bvid,const char *ca,
                           int (*cancel)(void*),void *arg,void (*log)(const char*))
{
    return wili_bili_session_open_ex(s,bvid,32,NULL,ca,cancel,arg,log);
}
void wili_bili_session_close(wili_bili_session *s,void (*log)(const char*))
{
    if(!s)return;
    if(s->video_opened && s->video_range.easy)wili_http_range_close(&s->video_range);
    if(s->audio_opened && s->audio_range.easy)wili_http_range_close(&s->audio_range);
    s->video_opened=s->audio_opened=0;s->ready=0;
    if(log)log("BILI_SESSION_CLOSED");
}
unsigned wili_bili_session_revision(const wili_bili_session *s){return s ? s->revision : 0;}
void wili_bili_session_line(const wili_bili_session *s,unsigned row,char *out,size_t cap)
{
    snprintf(out,cap,"%s",s && row<8 ? s->lines[row] : "");
}
