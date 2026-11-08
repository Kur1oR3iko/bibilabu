// SPDX-License-Identifier: GPL-3.0-or-later
#include "bili_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static const char *nav="{\"code\":-101,\"message\":\"账号未登录\",\"data\":{\"wbi_img\":{\"img_url\":\"https://i0.hdslb.com/bfs/wbi/7cd084941338484aae1ad9425b84077c.png\",\"sub_url\":\"https://i0.hdslb.com/bfs/wbi/4932caff0ff746eab6f01bf08b70ac45.png\"}}}";
static const char *view="{\"code\":0,\"data\":{\"bvid\":\"BV17x411w7KC\",\"aid\":170001,\"cid\":279786,\"duration\":2412,\"title\":\"中文测试 😀\"}}";
static const char *play="{\"code\":0,\"data\":{\"quality\":32,\"dash\":{\"video\":["
"{\"id\":32,\"codecid\":12,\"width\":854,\"height\":480,\"bandwidth\":100000,\"baseUrl\":\"https://media.example/hevc\"},"
"{\"id\":32,\"codecid\":7,\"codecs\":\"avc1.6e001f\",\"width\":854,\"height\":480,\"bandwidth\":100000,\"baseUrl\":\"https://media.example/high10\"},"
"{\"id\":32,\"codecid\":7,\"codecs\":\"avc1.64001f\",\"width\":854,\"height\":480,\"bandwidth\":1000000,\"frame_rate\":\"30000/1001\",\"baseUrl\":\"https://media.example/video\"},"
"{\"id\":16,\"codecid\":7,\"width\":640,\"height\":360,\"bandwidth\":500000,\"base_url\":\"https://media.example/lower\"}],"
"\"audio\":[{\"id\":30250,\"bandwidth\":200000,\"baseUrl\":\"https://media.example/dolby\"},{\"id\":30280,\"codecs\":\"mp4a.40.2\",\"bandwidth\":192000,\"base_url\":\"https://media.example/audio\"}]}}}";
int main(void)
{
    wili_bili_nav n;assert(!wili_bili_parse_nav(nav,strlen(nav),&n));assert(n.code==-101 && n.keys_ready);
    assert(!strcmp(n.mixin,"ea1db124af3c7062474693fa704f4ff8"));
    char query[1024];assert(!wili_bili_play_query("BV17x411w7KC",279786,1702204169,n.mixin,query,sizeof(query)));
    assert(!strcmp(query,"bvid=BV17x411w7KC&cid=279786&fnval=16&fnver=0&fourk=0&qn=32&web_location=1315873&wts=1702204169&w_rid=1dedfb0c5bdc535c7479ae070c36d580"));
    char unsigned_query[1024],signed_query[1100];snprintf(unsigned_query,sizeof(unsigned_query),"%s",query);
    char *signature=strstr(unsigned_query,"&w_rid=");assert(signature);*signature=0;
    assert(!wili_bili_sign_sorted(unsigned_query,n.mixin,signed_query,sizeof(signed_query)) && !strcmp(query,signed_query));
    assert(wili_bili_play_query("BV17x411w7KC&x=1",279786,1702204169,n.mixin,query,sizeof(query))<0);
    assert(wili_bili_play_query("BV17x411w7KC",0,1702204169,n.mixin,query,sizeof(query))<0);
    assert(wili_bili_play_query("BV17x411w7KC",279786,0,n.mixin,query,sizeof(query))<0);
    assert(wili_bili_play_query("BV17x411w7KC",279786,1702204169,n.mixin,query,8)<0);
    wili_bili_video v;assert(!wili_bili_parse_video(view,strlen(view),&v));assert(v.cid==279786 && v.aid==170001 && !strcmp(v.title,"中文测试 😀"));
    assert(wili_bili_parse_video("{\"code\":0,\"data\":{}}",20,&v)<0);
    const char *denied="{\"code\":-403,\"message\":\"forbidden\"}";assert(!wili_bili_parse_video(denied,strlen(denied),&v) && v.code==-403);
    wili_bili_play p;assert(!wili_bili_parse_play(play,strlen(play),&p));assert(p.dash_ready && p.height==480 && p.video_id==32 && p.audio_id==30280);
    const char *hd="{\"code\":0,\"data\":{\"quality\":80,\"dash\":{\"video\":["
        "{\"id\":80,\"codecid\":7,\"width\":1920,\"height\":1080,\"bandwidth\":4000000,\"baseUrl\":\"https://media.example/1080\"},"
        "{\"id\":64,\"codecid\":7,\"width\":1280,\"height\":720,\"bandwidth\":2000000,\"frame_rate\":\"30\",\"baseUrl\":\"https://media.example/720\"},"
        "{\"id\":32,\"codecid\":7,\"width\":854,\"height\":480,\"bandwidth\":1000000,\"baseUrl\":\"https://media.example/480\"}],"
        "\"audio\":[{\"id\":30280,\"codecs\":\"mp4a.40.2\",\"bandwidth\":192000,\"baseUrl\":\"https://media.example/audio\"}]}}}";
    wili_bili_play selected;
    assert(!wili_bili_parse_play_limit(hd,strlen(hd),64,&selected));
    assert(selected.dash_ready && selected.height==720 && selected.returned_quality==64);
    assert(!wili_bili_parse_play_limit(hd,strlen(hd),32,&selected));
    assert(selected.height==480 && selected.returned_quality==32);
    assert(!wili_bili_parse_play_limit(hd,strlen(hd),16,&selected));assert(!selected.dash_ready);
    assert(!wili_bili_parse_play_limit(hd,strlen(hd),80,&selected));assert(selected.height==720 && selected.video_id==64);
    puts("PASS requested-quality cap and actual-quality reporting; 1080p remains hidden even when API returns it");
    assert(!strcmp(p.video_url,"https://media.example/video"));
    assert(wili_bili_parse_nav("{\"code\":0} trailing",19,&n)<0);
    const char invalid_utf8[]="{\"code\":0,\"x\":\"\xc0\xaf\"}";assert(wili_bili_parse_nav(invalid_utf8,sizeof(invalid_utf8)-1,&n)<0);
    char fuzz[512];unsigned i;for(i=0;i<strlen(nav);i++){memcpy(fuzz,nav,strlen(nav));fuzz[i]^=0x7f;(void)wili_bili_parse_nav(fuzz,strlen(nav),&n);}
    for(i=0;i<strlen(view);i++)(void)wili_bili_parse_video(view,i,&v);
    assert(!wili_bili_parse_play_limit(hd,strlen(hd),32,&selected));assert(selected.available_mask==6);
    const char *rates[]={"30","30000/1001","60","60000/1001","broken"};
    for(unsigned k=0;k<5;k++){
        char data[768];snprintf(data,sizeof(data),"{\"code\":0,\"data\":{\"dash\":{\"video\":[{\"id\":80,\"codecid\":7,\"width\":1920,\"height\":720,\"bandwidth\":4000000,\"frameRate\":\"%s\",\"baseUrl\":\"https://media.example/wide\"}],\"audio\":[{\"id\":30280,\"bandwidth\":192000,\"baseUrl\":\"https://media.example/audio\"}]}}}",rates[k]);
        assert(!wili_bili_parse_play_limit(data,strlen(data),80,&selected));
        assert(selected.video_id==0 && selected.available_mask==0);
    }
    assert(!wili_bili_parse_play_limit(play,strlen(play),64,&selected));assert(selected.available_mask==3);
    size_t pad=300000;char *large=malloc(pad+256);assert(large);
    strcpy(large,"{\"code\":0,\"data\":{\"bvid\":\"BV17x411w7KC\",\"aid\":170001,\"cid\":279786,\"duration\":2,\"title\":\"large\",\"desc\":\"");
    size_t prefix=strlen(large);memset(large+prefix,'a',pad);strcpy(large+prefix+pad,"\"}}");
    assert(!wili_bili_parse_video(large,strlen(large),&v));assert(v.cid==279786);free(large);
    for(unsigned portrait=0;portrait<2;portrait++)for(unsigned rate=30;rate<=240;rate*=2){
        char data[1024];snprintf(data,sizeof(data),"{\"code\":0,\"data\":{\"dash\":{\"video\":[{\"id\":64,\"codecid\":7,\"codecs\":\"avc1.640033\",\"width\":%u,\"height\":%u,\"bandwidth\":25000000,\"frame_rate\":\"%u\",\"baseUrl\":\"https://media.example/720\"}],\"audio\":[{\"id\":30280,\"bandwidth\":128000,\"baseUrl\":\"https://media.example/audio\"}]}}}",portrait?720:1280,portrait?1280:720,rate);
        assert(!wili_bili_parse_play_limit(data,strlen(data),64,&selected));assert(selected.dash_ready && selected.available_mask==4 && selected.advertised_fps==rate);
    }
    const char *q74="{\"code\":0,\"data\":{\"dash\":{\"video\":[{\"id\":74,\"codecid\":7,\"width\":720,\"height\":1280,\"bandwidth\":5000000,\"frame_rate\":\"60000/1001\",\"baseUrl\":\"https://media.example/720p60\"}],\"audio\":[{\"id\":30280,\"bandwidth\":128000,\"baseUrl\":\"https://media.example/audio\"}]}}}";
    assert(!wili_bili_parse_play_limit(q74,strlen(q74),64,&selected));assert(selected.dash_ready && selected.video_id==74 && selected.returned_quality==64 && selected.available_mask==4);
    assert(!wili_bili_parse_play_limit(q74,strlen(q74),32,&selected));assert(!selected.dash_ready && selected.available_mask==4);
    puts("PASS landscape/portrait 720p selection at 30/60/120/240fps without metadata rate or bandwidth filtering");
    puts("PASS >256KiB view metadata and available-AVC-only quality masks");
    puts("PASS Bili JSON: anonymous nav, strict Unicode/schema/truncation handling, WBI golden signature, AVC/AAC metadata selection and business errors");return 0;
}
