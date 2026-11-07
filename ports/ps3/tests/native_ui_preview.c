// SPDX-License-Identifier: GPL-3.0-or-later
// Host render of the exact console compositor, with a synthetic cover fixture.
#include "native_ui.h"
#include "ui_font.h"
#include "ui_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
int ui_image_draw(uint32_t *fb,unsigned fw,unsigned fh,int x,int y,int w,int h,const char *url)
{
    (void)url;
    for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++){
        unsigned c=yy<h/2?0x00325272:0x0019273d;
        if(yy>h/2+abs(xx-w/2)/3)c=0x002b4b60;
        if(yy>h*3/4+xx/10)c=0x00101c2d;
        if(x+xx>=0 && x+xx<(int)fw && y+yy>=0 && y+yy<(int)fh)fb[(y+yy)*fw+x+xx]=c;
    }
    if(w>=300 && w<1000)ui_font_draw_text(fb,fw,fh,x+28,y+28,28,"山海之间",0x00ffffff);
    if(w>=300 && w<1000)ui_font_draw_text(fb,fw,fh,x+30,y+65,17,"界面预览 · 示例封面",0x00bacfe4);
    return 1;
}
static void save(const char *name,uint32_t *fb)
{
    FILE *f=fopen(name,"wb");assert(f);fprintf(f,"P6\n%u %u\n255\n",UI_WIDTH,UI_HEIGHT);
    for(unsigned i=0;i<UI_WIDTH*UI_HEIGHT;i++){unsigned char rgb[]={fb[i]>>16,fb[i]>>8,fb[i]};assert(fwrite(rgb,1,3,f)==3);}
    fclose(f);
}
static void compose(uint32_t *rgb,const uint32_t *layer,const uint32_t *video,unsigned fade)
{
    for(unsigned i=0;i<UI_WIDTH*UI_HEIGHT;i++){
        uint32_t src=layer[i],bg=video[i];unsigned a=(src>>24)*fade/255,inv=255-a;
        unsigned r=(((src>>16)&255)*fade+127)/255+(((bg>>16)&255)*inv+127)/255;
        unsigned g=(((src>>8)&255)*fade+127)/255+(((bg>>8)&255)*inv+127)/255;
        unsigned b=((src&255)*fade+127)/255+((bg&255)*inv+127)/255;
        assert(r<=255 && g<=255 && b<=255);rgb[i]=(r<<16)|(g<<8)|b;
    }
}
int main(void)
{
    assert(!ui_font_init("assets/switch_font.ttf"));
    uint32_t *fb=calloc(UI_WIDTH*UI_HEIGHT,4),*bg=calloc(UI_WIDTH*UI_HEIGHT,4);assert(fb && bg);
    native_ui_background(bg);
    bili_feed_state_t feed={0};wili_qr_login qr={0};feed.item_count=8;
    const char *titles[]={"城市的一天：从清晨走到日落","在星空下，听一场安静的演奏","山海之间 · 一段不赶时间的旅程","那些值得反复打开的游戏原声","从零开始，认识我们的太阳系","周末厨房：简单的一餐","打开旧相机，重新看见生活","漫步雨后的街道"};
    for(int i=0;i<8;i++){snprintf(feed.items[i].title,256,"%s",titles[i]);snprintf(feed.items[i].author,64,"影像记录");feed.items[i].duration_sec=325;feed.items[i].view_count=128560;}
    native_ui_feed(fb,bg,&feed,&qr,2,0,"");save("build/native-ui-home.ppm",fb);
    feed.current_tab=FEED_TAB_USER;native_ui_feed(fb,bg,&feed,&qr,0,1,"");save("build/native-ui-account.ppm",fb);
    uint32_t *video=calloc(UI_WIDTH*UI_HEIGHT,4),*layer=calloc(UI_WIDTH*UI_HEIGHT,4);assert(video && layer);
    ui_image_draw(video,UI_WIDTH,UI_HEIGHT,0,0,UI_WIDTH,UI_HEIGHT,"");
    native_ui_player(layer,titles[2],64,125000,325000,1,0,0,7);
    uint32_t *cached=malloc(UI_WIDTH*UI_HEIGHT*4);assert(cached);
    native_ui_player(layer,titles[2],64,137000,325000,2,0,0,7);memcpy(cached,layer,UI_WIDTH*UI_HEIGHT*4);
    native_ui_release_player_cache();native_ui_player(layer,titles[2],64,137000,325000,2,0,0,7);
    assert(!memcmp(cached,layer,UI_WIDTH*UI_HEIGHT*4));free(cached);
    puts("PASS cached player glass matches fresh composition after progress and pause changes");
    native_ui_player(layer,titles[2],64,125000,325000,1,0,0,7);
    for(unsigned i=0;i<UI_WIDTH*UI_HEIGHT;i++){unsigned a=layer[i]>>24;assert(((layer[i]>>16)&255)<=a && ((layer[i]>>8)&255)<=a && (layer[i]&255)<=a);}
    assert(layer[10*UI_WIDTH+10]==0);assert(layer[635*UI_WIDTH+200]==0); // clear panel interior, no gray tint
    compose(fb,layer,video,255);save("build/native-ui-player.ppm",fb);
    compose(fb,layer,video,0);assert(!memcmp(fb,video,UI_WIDTH*UI_HEIGHT*4));
    for(unsigned i=0;i<9;i++){compose(fb,layer,video,i*255/8);char file[80];snprintf(file,sizeof(file),"build/native-aero-fade-%02u.ppm",i);save(file,fb);}
    native_ui_player(layer,titles[2],32,125000,325000,1,1,0,3);compose(fb,layer,video,255);save("build/native-ui-quality-480.ppm",fb);
    bili_detail_state detail={0};snprintf(detail.title,sizeof(detail.title),"%s",titles[2]);snprintf(detail.up_name,sizeof(detail.up_name),"示例 UP 主");
    snprintf(detail.up_face,sizeof(detail.up_face),"fixture://avatar");snprintf(detail.status,sizeof(detail.status),"布局预览 · 示例数据");
    detail.views=128560;detail.likes=2408;detail.replies=96;detail.mid=42;detail.fans=12800;detail.ready=1;
    detail.related_count=detail.creator_count=8;detail.comment_count=4;
    for(int i=0;i<8;i++){detail.related[i]=detail.creator[i]=feed.items[i];snprintf(detail.related[i].pic_url,512,"fixture://cover/%d",i);snprintf(detail.creator[i].pic_url,512,"fixture://cover/%d",i);}
    const char *messages[]={"这一段的镜头和音乐很搭，适合在电视上慢慢看。","现在可以一边看视频，一边浏览右侧评论区。","这条长评论用于展示全文弹窗和中文回复，先编辑，再确认发布。","弹幕密度可以从0到4调整，0表示关闭。"};
    for(int i=0;i<4;i++){snprintf(detail.comments[i].author,64,"评论用户 %d",i+1);snprintf(detail.comments[i].message,768,"%s",messages[i]);detail.comments[i].id=i+1;}
    uint32_t *detail_background=malloc(UI_WIDTH*UI_HEIGHT*4),*before_scroll=malloc(UI_WIDTH*UI_HEIGHT*4);assert(detail_background && before_scroll);
    memcpy(detail_background,bg,UI_WIDTH*UI_HEIGHT*4);
    for(unsigned y=0;y<DETAIL_VIDEO_H;y++)for(unsigned x=0;x<DETAIL_VIDEO_W;x++)detail_background[(y+DETAIL_VIDEO_Y)*UI_WIDTH+x+DETAIL_VIDEO_X]=video[(y*UI_HEIGHT/DETAIL_VIDEO_H)*UI_WIDTH+x*UI_WIDTH/DETAIL_VIDEO_W];
    native_ui_reset_detail_scroll();native_ui_detail(layer,&detail,titles[2],0,0,0,2,125000,325000,1);assert(layer[100*UI_WIDTH+100]==0);
    assert(layer[120*UI_WIDTH+840]==0); // wallpaper remains unobscured between columns
    compose(fb,layer,detail_background,255);save("build/native-ui-detail.ppm",fb);
    native_ui_focus_comments(4,0,1);native_ui_detail(layer,&detail,titles[2],DETAIL_COMMENT_FOCUS,1,0,2,125000,325000,1);
    memcpy(before_scroll,layer,UI_WIDTH*UI_HEIGHT*4);compose(fb,layer,detail_background,255);save("build/native-ui-comments.ppm",fb);
    native_ui_detail_animate(DETAIL_COMMENT_FOCUS+3,4,0,1000000);native_ui_detail_animate(DETAIL_COMMENT_FOCUS+3,4,0,1250000);
    native_ui_detail(layer,&detail,titles[2],DETAIL_COMMENT_FOCUS+3,1,0,2,125000,325000,1);
    assert(!memcmp(before_scroll,layer,DETAIL_BODY_Y*UI_WIDTH*4));
    for(unsigned y=0;y<UI_HEIGHT;y++)assert(!memcmp(before_scroll+y*UI_WIDTH+850,layer+y*UI_WIDTH+850,(UI_WIDTH-850)*4));
    assert(memcmp(before_scroll+DETAIL_BODY_Y*UI_WIDTH,layer+DETAIL_BODY_Y*UI_WIDTH,DETAIL_BODY_H*UI_WIDTH*4));
    compose(fb,layer,detail_background,255);save("build/native-ui-comments-scrolled.ppm",fb);
    snprintf(detail.creator_status,sizeof(detail.creator_status),"UP主近期投稿");
    native_ui_detail(layer,&detail,titles[2],3,0,1,2,125000,325000,2);assert(layer[400*UI_WIDTH+700]==0);compose(fb,layer,detail_background,255);save("build/native-ui-creator.ppm",fb);
    native_ui_detail(layer,&detail,titles[2],5,1,0,2,125000,325000,2);
    native_ui_confirm(layer,"确认发布评论？","这是一条中文评论草稿。确认后才会提交。",0);compose(fb,layer,detail_background,255);save("build/native-ui-comment-confirm.ppm",fb);
    free(detail_background);free(before_scroll);
    puts("PASS clear wallpaper layer and scroll clipping: video/header/related pixels unchanged while comments move");
    puts("PASS detail/creator/comments compositor, transparent video window and confirmation preview");
    native_ui_loading(fb,bg,1000000,0);memcpy(layer,fb,UI_WIDTH*UI_HEIGHT*4);
    native_ui_loading(fb,bg,1160000,0);assert(memcmp(layer,fb,UI_WIDTH*UI_HEIGHT*4));
    save("build/native-ui-loading.ppm",fb);
    native_ui_loading(fb,bg,1160000,1);assert(memcmp(layer,fb,UI_WIDTH*UI_HEIGHT*4));
    memcpy(fb,video,UI_WIDTH*UI_HEIGHT*4);native_ui_frost_exit(fb);
    for(unsigned y=0;y<UI_HEIGHT;y++)for(unsigned x=0;x<UI_WIDTH;x++)
        if(x<400 || x>=880 || y<250 || y>=470)assert(fb[y*UI_WIDTH+x]==video[y*UI_WIDTH+x]);
    puts("PASS loading animation/cancellation and exit blur bounds");
    free(video);free(layer);
    puts("PASS Aero premultiplied-alpha bounds, real transparency and zero-opacity identity");
    feed.current_tab=FEED_TAB_RECOMMEND;native_ui_feed(fb,bg,&feed,&qr,2,0,"");native_ui_exit(fb,0);save("build/native-ui-exit.ppm",fb);
    feed.current_tab=FEED_TAB_SEARCH;int saved_count=feed.item_count;feed.item_count=0;native_ui_feed(fb,bg,&feed,&qr,0,0,"");feed.item_count=saved_count;
    native_ui_keyboard(fb,"PS3",15,0);save("build/native-ui-keyboard.ppm",fb);
    feed.current_tab=FEED_TAB_RECOMMEND;
    native_ui_animate(&feed,2,1000000);
    for(int frame=0;frame<12;frame++){
        int target=frame<6?3:2;uint64_t tick=1000001+(uint64_t)frame*36000;
        native_ui_animate(&feed,target,tick);native_ui_feed(fb,bg,&feed,&qr,target,0,"");
        char file[80];snprintf(file,sizeof(file),"build/native-motion-%02d.ppm",frame);save(file,fb);
    }
    free(fb);free(bg);native_ui_release_player_cache();ui_font_shutdown();puts("PASS native compositor render: home/account/player");
}

unsigned ui_image_revision(void){return 0;}
