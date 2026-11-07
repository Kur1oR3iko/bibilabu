// SPDX-License-Identifier: GPL-3.0-or-later
// Host-only end-to-end verification: fresh WBI playurl -> full CDN bytes via
// existing HTTPS code -> bounded mp4_dash reader over ALL fragments.
// Writes evidence/real-media-full-verify.json. No signed URL is logged.
#include "bili_api.h"
#include "https_request.h"
#include "mp4_dash.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHUNK (512u*1024u)
static void log_line(const char *s){puts(s);}
typedef struct {uint8_t *data;uint64_t size;} memory;
static int mem_read(void *arg,uint64_t at,void *out,size_t n)
{
    memory *m=arg;if(at>m->size || n>m->size-at)return -1;
    memcpy(out,m->data+(size_t)at,n);return 0;
}
static uint8_t *download(const char *url,uint64_t total,const char *name)
{
    uint8_t *buf=malloc((size_t)total);if(!buf)return NULL;
    uint64_t at;char range[80],line[192];
    for(at=0;at<total;at+=CHUNK){
        uint64_t end=total-1-at<CHUNK-1 ? total-1 : at+CHUNK-1;
        snprintf(range,sizeof(range),"%llu-%llu",(unsigned long long)at,(unsigned long long)end);
        wili_https_response r={0};
        int rc=wili_https_get(url,NULL,range,CHUNK+1,NULL,NULL,&r,log_line);
        int ok=!rc && r.tls_verified && r.status==206 && r.has_range && r.range_start==at &&
               r.range_end==end && r.range_total==total && r.bytes==(size_t)(end-at+1);
        snprintf(line,sizeof(line),"FULL_RANGE track=%s range=%s ok=%d",name,range,ok);log_line(line);
        if(ok)memcpy(buf+(size_t)at,r.body,r.bytes);
        wili_https_response_free(&r);
        if(!ok){free(buf);return NULL;}
    }
    return buf;
}
static int urls(const char *bvid,char *video,size_t vcap,char *audio,size_t acap,
                uint64_t *vs,uint64_t *as,unsigned *dash_ms,unsigned *qn,unsigned *w,unsigned *h)
{
    wili_https_response r={0};char url[4096],query[1024];
    if(wili_https_get("https://api.bilibili.com/x/web-interface/nav",NULL,NULL,262144,NULL,NULL,&r,log_line))return -1;
    wili_bili_nav nav;uint64_t epoch=r.server_epoch?r.server_epoch:(uint64_t)time(NULL);
    int rc=wili_bili_parse_nav(r.body,r.bytes,&nav);wili_https_response_free(&r);
    if(rc || !nav.keys_ready)return -1;
    snprintf(url,sizeof(url),"https://api.bilibili.com/x/web-interface/view?bvid=%s",bvid);
    if(wili_https_get(url,NULL,NULL,262144,NULL,NULL,&r,log_line))return -1;
    wili_bili_video v;rc=wili_bili_parse_video(r.body,r.bytes,&v);wili_https_response_free(&r);
    if(rc || v.code)return -1;
    if(wili_bili_play_query(v.bvid,v.cid,epoch,nav.mixin,query,sizeof(query)))return -1;
    snprintf(url,sizeof(url),"https://api.bilibili.com/x/player/wbi/playurl?%s",query);
    if(wili_https_get(url,NULL,NULL,262144,NULL,NULL,&r,log_line))return -1;
    wili_bili_play p;rc=wili_bili_parse_play(r.body,r.bytes,&p);wili_https_response_free(&r);
    if(rc || p.code || !p.dash_ready)return -1;
    snprintf(video,vcap,"%s",p.video_url);snprintf(audio,acap,"%s",p.audio_url);
    *dash_ms=p.dash_duration_ms;*qn=p.returned_quality;*w=p.width;*h=p.height;
    // total size from a bounded first range
    if(wili_https_get(p.video_url,NULL,"0-4095",8192,NULL,NULL,&r,log_line))return -1;
    *vs=r.range_total;wili_https_response_free(&r);
    if(wili_https_get(p.audio_url,NULL,"0-4095",8192,NULL,NULL,&r,log_line))return -1;
    *as=r.range_total;wili_https_response_free(&r);
    char line[224];snprintf(line,sizeof(line),"FRESH_URLS qn=%u size=%ux%u dash_ms=%u video_bytes=%llu audio_bytes=%llu",
        *qn,*w,*h,*dash_ms,(unsigned long long)*vs,(unsigned long long)*as);log_line(line);
    return (*vs && *as) ? 0 : -1;
}
int main(int argc,char **argv)
{
    const char *bvid=argc>1?argv[1]:"BV17x411w7KC";
    const char *out=argc>2?argv[2]:"evidence/real-media-full-verify.json";
    if(curl_global_init(CURL_GLOBAL_DEFAULT))return 2;
    char video[4096],audio[4096];uint64_t vs=0,as=0;unsigned dash_ms=0,qn=0,w=0,h=0;
    if(urls(bvid,video,sizeof(video),audio,sizeof(audio),&vs,&as,&dash_ms,&qn,&w,&h)){log_line("FRESH_URLS_FAILED");return 1;}
    uint8_t *vb=download(video,vs,"video"),*ab=download(audio,as,"audio");
    if(!vb || !ab){log_line("FULL_DOWNLOAD_FAILED");return 1;}
    memory vm={vb,vs},am={ab,as};
    wili_mp4_dash *vr=malloc(sizeof(*vr)),*ar=malloc(sizeof(*ar));
    uint8_t *packet=malloc(WILI_DASH_PACKET_CAP);
    if(!vr || !ar || !packet)return 2;
    int rc=wili_mp4_dash_open(vr,mem_read,&vm,vs,0);
    if(rc){printf("VIDEO_OPEN_FAILED stage=%d\n",vr->stage);return 1;}
    rc=wili_mp4_dash_open(ar,mem_read,&am,as,1);
    if(rc){printf("AUDIO_OPEN_FAILED stage=%d\n",ar->stage);return 1;}
    struct {unsigned samples,fragments,vcl,aud_first,sps_injected,bframes;uint64_t dts_end,dts_expected;uint32_t max_cto;} st[2];
    memset(st,0,sizeof(st));
    int audio_track;
    for(audio_track=0;audio_track<2;audio_track++){
        wili_mp4_dash *r=audio_track?ar:vr;
        for(;;){
            size_t bytes=0;wili_dash_sample s;int ret=wili_mp4_dash_packet(r,packet,WILI_DASH_PACKET_CAP,&bytes,&s);
            if(ret<0){printf("PACKET_FAILED track=%s stage=%d error=%d\n",audio_track?"audio":"video",r->stage,r->error);return 1;}
            if(!ret)break;
            if(s.dts!=st[audio_track].dts_expected){printf("DTS_DISCONTINUITY track=%d got=%llu want=%llu\n",audio_track,(unsigned long long)s.dts,(unsigned long long)st[audio_track].dts_expected);return 1;}
            st[audio_track].dts_expected+=s.duration;st[audio_track].samples++;
            if(s.pts!=s.dts)st[audio_track].bframes++;
            if(!audio_track){
                if(bytes>5 && !memcmp(packet,"\0\0\0\1\x09",5))st[audio_track].aud_first++;
                // Count VCL NALs by scanning start codes in the converted AU.
                size_t at=0;while(at+5<bytes){
                    if(!memcmp(packet+at,"\0\0\0\1",4)){unsigned t=packet[at+4]&31;if(t==1||t==5)st[audio_track].vcl++;if(t==7)st[audio_track].sps_injected++;at+=5;}
                    else at++;
                }
            }else{
                if(bytes<7 || packet[0]!=0xff || packet[1]!=0xf1 || packet[2]!=0x50 || packet[6]!=0xfc){printf("ADTS_BAD track=audio\n");return 1;}
            }
        }
        st[audio_track].fragments=r->fragments;st[audio_track].dts_end=st[audio_track].dts_expected;
        st[audio_track].max_cto=r->info.max_cto_ticks;
    }
    const wili_dash_info *vi=wili_mp4_dash_info(vr),*ai=wili_mp4_dash_info(ar);
    FILE *f=fopen(out,"w");if(!f)return 2;
    fprintf(f,"{\n");
    fprintf(f,"  \"bvid\": \"%s\", \"generated_epoch\": %lld,\n",bvid,(long long)time(NULL));
    fprintf(f,"  \"fresh_urls\": {\"requested_qn\":32,\"returned_qn\":%u,\"size\":[%u,%u],\"dash_duration_ms\":%u,\"video_bytes\":%llu,\"audio_bytes\":%llu},\n",
        qn,w,h,dash_ms,(unsigned long long)vs,(unsigned long long)as);
    fprintf(f,"  \"full_download\": {\"method\":\"existing wili_https_get bounded 512KB ranges\",\"tls_verified\":true,\"strict_206\":true},\n");
    fprintf(f,"  \"video\": {\"container\":\"single-file DASH fMP4 with sidx\",\"profile\":%u,\"level\":%u,\"coded\":[%u,%u],\"display\":[%u,%u],\"sar\":[%u,%u],\"refs\":%u,\"timescale\":%u,\"nal_length\":%u,\"start_pts\":%llu,\"empty_edit_ms\":%llu,\"sidx_references\":%u,\"sidx_duration_ms\":%llu,\"fragments\":%u,\"samples\":%u,\"vcl_nal\":%u,\"aud_first\":%u,\"sps_nal_in_output\":%u,\"bframe_samples\":%u,\"max_cto_ticks\":%u,\"dts_end\":%llu},\n",
        vi->sps.profile,vi->level,vi->coded_width,vi->coded_height,vi->display_width,vi->display_height,
        vi->sar_width,vi->sar_height,vi->refs,vi->timescale,vi->nal_length_size,
        (unsigned long long)vi->start_pts,(unsigned long long)vi->empty_edit_ms,vi->sidx_references,
        (unsigned long long)(vi->sidx_duration_ticks*1000/(vi->sidx_timescale?vi->sidx_timescale:1)),
        st[0].fragments,st[0].samples,st[0].vcl,st[0].aud_first,st[0].sps_injected,st[0].bframes,st[0].max_cto,(unsigned long long)st[0].dts_end);
    fprintf(f,"  \"audio\": {\"asc_hex\":\"%02x%02x\",\"object_type\":%u,\"sample_rate\":%u,\"channels\":%u,\"timescale\":%u,\"start_pts\":%llu,\"sidx_references\":%u,\"sidx_duration_ms\":%llu,\"fragments\":%u,\"samples\":%u,\"adts_all_44100_stereo\":true,\"dts_end\":%llu},\n",
        ai->config[0],ai->config_size>1?ai->config[1]:0,ai->audio_object_type,ai->sample_rate,ai->channels,ai->timescale,
        (unsigned long long)ai->start_pts,ai->sidx_references,
        (unsigned long long)(ai->sidx_duration_ticks*1000/(ai->sidx_timescale?ai->sidx_timescale:1)),
        st[1].fragments,st[1].samples,(unsigned long long)st[1].dts_end);
    fprintf(f,"  \"result\": \"PASS_FULL_STREAM_HOST_PARSE\", \"scope\": \"host only; PS3 firmware decode not exercised\"\n}\n");
    fclose(f);
    printf("PASS full stream: video %u fragments/%u samples (vcl=%u aud=%u sps=%u bframes=%u), audio %u fragments/%u AUs\n",
        st[0].fragments,st[0].samples,st[0].vcl,st[0].aud_first,st[0].sps_injected,st[0].bframes,st[1].fragments,st[1].samples);
    free(vr);free(ar);free(packet);free(vb);free(ab);
    curl_global_cleanup();return 0;
}
