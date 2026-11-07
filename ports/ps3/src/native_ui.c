// SPDX-License-Identifier: GPL-3.0-or-later
// Native, bounded 1280x720 software composition in cached main memory.
// RSX transfers the resulting surface. No OpenGL, Vulkan or Borealis dependency.
#include "native_ui.h"
#include "ui_core.h"
#include "ui_font.h"
#include "ui_image.h"
#include "ui_motion.h"
#include "user_settings.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#define WHITE 0x00edf5ffu
#define MUTED 0x0094b1cfu
#define LIGHT 0x00bbdfffU
#define PANEL 0x0012243bu
static ui_motion motion;
static int scene_x,scene_y,transparent_target;
void native_ui_alpha_target(int enabled){transparent_target=enabled!=0;ui_core_alpha_target(enabled);ui_font_alpha_target(enabled);}
int native_ui_animate(const bili_feed_state_t *feed,int selected,uint64_t now)
{
    return ui_motion_step(&motion,(int)feed->current_tab,selected,feed->page,feed->favorite_id,feed->item_count,now);
}
static uint32_t focus_color(float weight)
{
    unsigned a=(unsigned)(weight*255);if(a>255)a=255;
    unsigned r=(0x94*(255-a)+0xed*a)/255,g=(0xb1*(255-a)+0xf5*a)/255,b=(0xcf*(255-a)+0xff*a)/255;
    return (r<<16)|(g<<8)|b;
}
static const char *tabs[]={"推荐","热门","历史","收藏","搜索","账号"};
static void rect(uint32_t *fb,int x,int y,int w,int h,uint32_t color){ui_draw_fill_rect(fb,UI_WIDTH,UI_HEIGHT,x+scene_x,y+scene_y,w,h,color);}
static void text(uint32_t *fb,int x,int y,int size,const char *s,uint32_t color)
{
    ui_font_draw_text(fb,UI_WIDTH,UI_HEIGHT,x+scene_x+2,y+scene_y+2,size,s,transparent_target?0x70040910u:0x00040910u);
    ui_font_draw_text(fb,UI_WIDTH,UI_HEIGHT,x+scene_x,y+scene_y,size,s,color);
}
static void stroke(uint32_t *fb,int x,int y,int w,int h,uint32_t color){ui_draw_stroke_rect(fb,UI_WIDTH,UI_HEIGHT,x+scene_x,y+scene_y,w,h,1,color);}
static void focus_glow(uint32_t *fb,int x,int y,int w,int h)
{
    x+=scene_x;y+=scene_y;
    for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++){
        int px=x+xx,py=y+yy;if(px<0 || px>=(int)UI_WIDTH || py<0 || py>=(int)UI_HEIGHT)continue;
        unsigned edge=(unsigned)(xx<w/2?xx:w-1-xx);if(edge>30)edge=30;
        unsigned vy=(unsigned)(yy<h/2?yy:h-1-yy);if(vy>14)vy=14;
        unsigned a=edge*vy/18;uint32_t old=fb[py*UI_WIDTH+px];
        unsigned r=(((old>>16)&255)*(255-a)+80*a)/255;
        unsigned g=(((old>>8)&255)*(255-a)+125*a)/255;
        unsigned b=((old&255)*(255-a)+170*a)/255;
        fb[py*UI_WIDTH+px]=(r<<16)|(g<<8)|b;
    }
}
static void label(uint32_t *fb,int x,int y,int size,int width,int lines,const char *s,uint32_t color)
{
    if(!s)return;
    for(int line=0;line<lines && *s;line++){
        char out[512]={0};size_t n=0;int w=0,h=0;
        while(*s && n<sizeof(out)-8){
            const char *next=s;ui_utf8_next(&next);size_t len=(size_t)(next-s);
            char glyph[5]={0};if(len>4)break;memcpy(glyph,s,len);int advance=0;
            ui_font_measure_text(size,glyph,&advance,&h);
            if(w+advance>width-16)break;
            memcpy(out+n,s,len);out[n+len]=0;w+=advance;
            n+=len;s=next;
        }
        if(*s && line==lines-1)strcat(out,"…");
        text(fb,x,y+line*(size+7),size,out,color);
        if(!n)break;
    }
}
void native_ui_background(uint32_t *fb)
{
    for(unsigned y=0;y<UI_HEIGHT;y++)for(unsigned x=0;x<UI_WIDTH;x++){
        int dx=(int)x-860,dy=(int)y-390;
        unsigned glow=(unsigned)((dx*dx/3+dy*dy)/4000);
        if(glow>24)glow=24;
        unsigned v=24-glow;
        fb[y*UI_WIDTH+x]=((7+v/4)<<16)|((18+v/2)<<8)|(35+v);
    }
    // A static ribbon, computed once: the PS3 visual rhythm without a per-frame shader.
    for(int x=0;x<(int)UI_WIDTH;x++){
        float wave=sinf(x*0.004f)*28.0f+sinf(x*0.0017f)*22.0f;
        for(int band=0;band<3;band++){
            int y=447+(int)wave+band*17;
            rect(fb,x,y,1,1,band==0?0x0027415d:0x001d344d);
        }
    }
}
static void line(uint32_t *fb,int x0,int y0,int x1,int y1,int thick,uint32_t c)
{
    int dx=x1-x0,dy=y1-y0,steps=abs(dx)>abs(dy)?abs(dx):abs(dy);
    if(!steps)steps=1;
    for(int i=0;i<=steps;i++)rect(fb,x0+dx*i/steps,y0+dy*i/steps,thick,thick,c);
}
static void ring(uint32_t *fb,int cx,int cy,int r,int thick,uint32_t c)
{
    for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++){
        int d=x*x+y*y;if(d<=r*r && d>=(r-thick)*(r-thick))rect(fb,cx+x,cy+y,1,1,c);
    }
}
static void icon_shape(uint32_t *fb,int x,int y,int size,int kind,uint32_t c)
{
    int t=size/18;if(t<2)t=2;
    if(kind==0){
        ui_draw_stroke_rect(fb,UI_WIDTH,UI_HEIGHT,x+scene_x,y+scene_y+size/7,size,size*2/3,t,c);
        for(int i=0;i<size/3;i++)line(fb,x+size/3+i,y+size/3+i/2,x+size/3+i,y+size*2/3-i/2,1,c);
    }else if(kind==1){
        line(fb,x+size/2,y,x+size/5,y+size*3/5,t,c);line(fb,x+size/5,y+size*3/5,x+size*3/4,y+size*3/5,t,c);
        line(fb,x+size*3/4,y+size*3/5,x+size/2,y+size,t,c);line(fb,x+size/2,y+size,x+size/2,y+size/3,t,c);
    }else if(kind==2){
        ring(fb,x+size/2,y+size/2,size/2,t,c);line(fb,x+size/2,y+size/5,x+size/2,y+size/2,t,c);
        line(fb,x+size/2,y+size/2,x+size*3/4,y+size/2,t,c);
    }else if(kind==3){
        int cx=x+size/2,cy=y+size/2;
        for(int i=0;i<10;i++){
            float a=(float)i*0.6283185f-1.5707963f,b=(float)(i+1)*0.6283185f-1.5707963f;
            int r=(i%2)?size/5:size/2,r2=((i+1)%2)?size/5:size/2;
            line(fb,cx+(int)(cosf(a)*r),cy+(int)(sinf(a)*r),cx+(int)(cosf(b)*r2),cy+(int)(sinf(b)*r2),t,c);
        }
    }else if(kind==4){ring(fb,x+size*2/5,y+size*2/5,size/3,t,c);line(fb,x+size*3/5,y+size*3/5,x+size,y+size,t,c);}
    else {ring(fb,x+size/2,y+size/4,size/5,t,c);line(fb,x+size/4,y+size*3/5,x,y+size,t,c);
        line(fb,x+size*3/4,y+size*3/5,x+size,y+size,t,c);line(fb,x,y+size,x+size,y+size,t,c);line(fb,x+size/4,y+size*3/5,x+size*3/4,y+size*3/5,t,c);}
}
static void icon(uint32_t *fb,int x,int y,int size,int kind,uint32_t c)
{
    icon_shape(fb,x+2,y+3,size,kind,0x0009121e);icon_shape(fb,x,y,size,kind,c);
}
void native_ui_feed(uint32_t *fb,const uint32_t *background,const bili_feed_state_t *feed,
                    const wili_qr_login *qr,int selected,int tab_focus,const char *message)
{
    (void)tab_focus;memcpy(fb,background,UI_WIDTH*UI_HEIGHT*4);
    text(fb,66,44,23,"bibilabu",WHITE);text(fb,1080,48,17,qr->is_logged_in?"已登录":"访客模式",MUTED);
    scene_x=-28;scene_y=-20;
    // XMB cross: the selected category stays anchored; the strip moves around it.
    for(int i=0;i<FEED_TAB_COUNT;i++){
        float delta=(float)i-(motion.initialized?motion.category.value:(float)feed->current_tab);
        while(delta< -1.5f)delta+=FEED_TAB_COUNT;
        while(delta>4.5f)delta-=FEED_TAB_COUNT;
        float focus=1-fabsf(delta);if(focus<0)focus=0;
        int x=320+(int)(delta*170),sz=47+(int)(19*focus),y=211-(int)(10*focus);
        if(x>1190 || x<70)continue;
        if(focus>0.5f)focus_glow(fb,x-28,y-23,122,112);
        icon(fb,x,y,sz,i,focus_color(focus));
        if(i==(int)feed->current_tab)text(fb,x+7,282,25,tabs[i],WHITE);
    }
    if(feed->current_tab==FEED_TAB_USER){
        icon(fb,324,391,57,5,WHITE);
        text(fb,430,390,30,qr->is_logged_in?"你的账号":"登录哔哩哔哩",WHITE);
        text(fb,432,437,20,qr->is_logged_in?"方块键退出登录":"叉键获取二维码，在手机上确认",MUTED);
        if(!qr->is_logged_in && (qr->state==WILI_QR_NEED_SCAN || qr->state==WILI_QR_NEED_CONFIRM)){
            int scale=240/(qr->size+8),box=(qr->size+8)*scale;
            if(scale>0){int x=946+(240-box)/2,y=356;
                rect(fb,x,y,box,box,0x00ffffff);
                for(int yy=0;yy<qr->size;yy++)for(int xx=0;xx<qr->size;xx++)
                    if(qrcodegen_getModule(qr->qrcode,xx,yy))rect(fb,x+(xx+4)*scale,y+(yy+4)*scale,scale,scale,0x00000000);
            }
            text(fb,946,614,18,qr->state==WILI_QR_NEED_CONFIRM?"请在手机上确认":"等待扫码",WHITE);
        }else if(qr->is_logged_in){char uid[80];snprintf(uid,sizeof(uid),"UID  %s",qr->dede_user_id);text(fb,432,488,20,uid,LIGHT);}
        else if(qr->state==WILI_QR_ERROR)text(fb,432,488,20,"暂时无法获取二维码，请重试",LIGHT);
    }else if(feed->item_count>0){
        if(selected<0 || selected>=feed->item_count)selected=0;
        if(feed->current_tab==FEED_TAB_SEARCH){char query[320];snprintf(query,sizeof(query),"搜索  %s   [方块键修改]",feed->search_keyword);label(fb,430,322,19,760,1,query,MUTED);}
        if(feed->current_tab==FEED_TAB_FAVORITE && feed->favorite_id)label(fb,460,322,18,725,1,feed->favorite_title,MUTED);
        for(int i=0;i<feed->item_count;i++){
            float distance=(float)i-(motion.initialized?motion.selection.value:(float)selected);
            int cy=406+(int)(distance*94);if(cy<305 || cy>611 || ((feed->current_tab==FEED_TAB_SEARCH || feed->favorite_id) && cy<380))continue;
            float weight=1-fabsf(distance);if(weight<0)weight=0;
            int focus=i==selected,tw=66+(int)(70*weight),th=tw*9/16,tx=350-tw/2,ty=cy-(int)(12*weight);
            const bili_feed_item_t *item=&feed->items[i];
            if(!ui_image_draw(fb,UI_WIDTH,UI_HEIGHT,tx+scene_x,ty+scene_y,tw,th,item->pic_url))icon(fb,tx+10,ty,th,item->is_folder?3:0,focus_color(weight));
            if(focus){stroke(fb,tx-1,ty-1,tw+2,th+2,LIGHT);label(fb,460,cy,28,715,1,item->title,focus_color(weight));
                char detail[192];
                if(item->is_folder)snprintf(detail,sizeof(detail),"%u 个视频  ·  叉键打开收藏夹",item->view_count);
                else if(feed->current_tab==FEED_TAB_HISTORY){
                    if(item->progress_sec<0)snprintf(detail,sizeof(detail),"%s  ·  已看完",item->author);
                    else snprintf(detail,sizeof(detail),"%s  ·  看到 %u:%02u / %u:%02u",item->author,(unsigned)item->progress_sec/60,(unsigned)item->progress_sec%60,item->duration_sec/60,item->duration_sec%60);
                }else snprintf(detail,sizeof(detail),"%s  ·  %u:%02u  ·  %u 次播放",item->author,item->duration_sec/60,item->duration_sec%60,item->view_count);
                if(weight>0.65f)label(fb,462,cy+39,18,713,1,detail,MUTED);
            }else label(fb,460,cy+5,21,715,1,item->title,focus_color(weight));
        }
        char count[96];snprintf(count,sizeof(count),"%d / %d   ·   第 %u 页%s",selected+1,feed->item_count,feed->page?feed->page:1,feed->has_more?"   ·   R2 下一页":"");text(fb,435,641,16,count,MUTED);
    }else {
        icon(fb,325,396,54,feed->current_tab,WHITE);
        text(fb,430,393,28,feed->loading?"正在连接…":feed->current_tab==FEED_TAB_SEARCH?"搜索视频":"暂无内容",WHITE);
        label(fb,432,442,21,725,2,feed->status_msg,MUTED);
    }
    scene_x=scene_y=0;
    text(fb,66,683,16,"左右 分类    上下 选择    叉键 确认    圆圈 返回    △ 刷新    SELECT 壁纸    START 退出",MUTED);
    if(message && *message){rect(fb,300,604,890,30,0x0013253a);label(fb,315,609,17,860,1,message,WHITE);}
}
static void aero_round(uint32_t *fb,int x,int y,int w,int h,int radius,uint32_t color)
{
    if(w<=0 || h<=0)return;
    if(radius>w/2)radius=w/2;
    if(radius>h/2)radius=h/2;
    for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++){
        int px=x+xx,py=y+yy;if(px<0 || px>=(int)UI_WIDTH || py<0 || py>=(int)UI_HEIGHT)continue;
        int dx=xx<radius?radius-xx:xx>=w-radius?xx-(w-radius-1):0;
        int dy=yy<radius?radius-yy:yy>=h-radius?yy-(h-radius-1):0;
        float coverage=1;
        if(dx && dy){coverage=radius+0.5f-sqrtf((float)(dx*dx+dy*dy));if(coverage<=0)continue;if(coverage>1)coverage=1;}
        unsigned alpha=(unsigned)((color>>24)*coverage);
        fb[py*UI_WIDTH+px]=ui_rgba_over(fb[py*UI_WIDTH+px],(color&0x00ffffff)|(alpha<<24));
    }
}
static void aero_panel(uint32_t *fb,int x,int y,int w,int h,int radius)
{
    // Clear XMB-style frame: no tinted or gray panel fill.
    rect(fb,x+radius,y,w-radius*2,1,0x90ffffff);rect(fb,x+radius,y+h-1,w-radius*2,1,0x40ffffff);
    rect(fb,x,y+radius,1,h-radius*2,0x50ffffff);rect(fb,x+w-1,y+radius,1,h-radius*2,0x50ffffff);
    for(int dy=0;dy<radius;dy++)for(int dx=0;dx<radius;dx++){
        float distance=sqrtf((float)((radius-dx)*(radius-dx)+(radius-dy)*(radius-dy)));
        float cover=1.0f-fabsf(distance-radius);if(cover<=0)continue;
        unsigned alpha=(unsigned)(112*cover);int xx[2]={x+dx,x+w-1-dx},yy[2]={y+dy,y+h-1-dy};
        for(int a=0;a<2;a++)for(int b=0;b<2;b++)if(xx[a]>=0 && xx[a]<(int)UI_WIDTH && yy[b]>=0 && yy[b]<(int)UI_HEIGHT){unsigned at=(unsigned)yy[b]*UI_WIDTH+xx[a];fb[at]=ui_rgba_over(fb[at],(alpha<<24)|0xffffff);}
    }
}
static void clock_text(char *out,size_t cap,uint64_t milliseconds)
{
    unsigned total=(unsigned)(milliseconds/1000);
    if(total>=3600)snprintf(out,cap,"%u:%02u:%02u",total/3600,total/60%60,total%60);
    else snprintf(out,cap,"%02u:%02u",total/60,total%60);
}
static uint32_t *player_base_cache,*detail_body_cache;
static detail_scroll detail_scroll_state;
static int player_base_kind;
static unsigned ui_dm_density=WILI_DEFAULT_DANMAKU_DENSITY;
static char player_base_title[256];
static unsigned player_base_quality;
void native_ui_release_player_cache(void){free(player_base_cache);free(detail_body_cache);player_base_cache=detail_body_cache=NULL;player_base_title[0]=0;player_base_kind=0;}
void native_ui_dm_density(unsigned density){if(ui_dm_density!=density){ui_dm_density=density;player_base_kind=0;}}
static void player_base(uint32_t *fb,const char *title,unsigned quality)
{
    memset(fb,0,UI_WIDTH*UI_HEIGHT*4);
    aero_panel(fb,44,32,1192,88,18);
    label(fb,70,51,27,980,1,title,WHITE);
    text(fb,72,88,15,"bibilabu",0xd0d4d4d4);
    char dm[64];if(ui_dm_density==6)snprintf(dm,sizeof(dm),"弹幕 6 / 6 · 全量");else snprintf(dm,sizeof(dm),"弹幕密度 %u / 6",ui_dm_density);text(fb,180,88,15,dm,0xd0d4d4d4);
    aero_panel(fb,1090,52,118,40,12);
    text(fb,1114,62,20,quality>=64?"720P":quality>=32?"480P":"360P",0xfff1f2f3);

    aero_panel(fb,44,548,1192,132,22);
    aero_round(fb,78,570,1124,4,2,0x40ffffff);
    text(fb,841,611,18,"L1  −10 秒     R1  +10 秒",0xe6e0e0e0);
    text(fb,141,649,15,"叉键 暂停   上下 清晰度   △ 控制栏   □ 弹幕密度   ○ 详情",0xced0d0d0);
}
void native_ui_player(uint32_t *fb,const char *title,unsigned quality,uint64_t elapsed,
                      uint64_t duration,unsigned state,int quality_menu,int quality_selected,unsigned quality_mask)
{
    native_ui_alpha_target(1);if(!title)title="";
    if(!player_base_cache || player_base_kind!=1 || player_base_quality!=quality || strcmp(player_base_title,title)){
        if(!player_base_cache)player_base_cache=malloc(UI_WIDTH*UI_HEIGHT*4);
        if(player_base_cache){player_base(player_base_cache,title,quality);player_base_quality=quality;player_base_kind=1;snprintf(player_base_title,sizeof(player_base_title),"%s",title);}
    }
    if(player_base_cache)memcpy(fb,player_base_cache,UI_WIDTH*UI_HEIGHT*4);
    else player_base(fb,title,quality);
    int track_width=1124;
    unsigned pos=duration?(unsigned)((elapsed>duration?duration:elapsed)*track_width/duration):0;
    if(pos)aero_round(fb,78,570,(int)pos,4,2,0xffe2e5e8);
    aero_round(fb,78+(int)pos-7,565,14,14,7,0x30ffffff);
    aero_round(fb,78+(int)pos-3,569,6,6,3,0xfffcfcfc);

    aero_panel(fb,76,599,44,44,22);
    if((state&15)==2){
        for(int x=0;x<13;x++)rect(fb,93+x,610+x/2,1,22-x,0xffedfbff);
    }else{rect(fb,91,612,4,18,0xffedfbff);rect(fb,101,612,4,18,0xffedfbff);}
    char elapsed_text[32],duration_text[32],time_line[72];clock_text(elapsed_text,sizeof(elapsed_text),elapsed);clock_text(duration_text,sizeof(duration_text),duration);
    snprintf(time_line,sizeof(time_line),"%s  /  %s",elapsed_text,duration_text);text(fb,140,606,24,time_line,WHITE);
    const char *status=(state&15)==4?"播放失败 · 可返回重试":(state&15)==3?"播放结束":(state&15)==2?"已暂停":(state>>4)==5?"正在缓冲":"正在播放";
    text(fb,470,611,18,status,0xffd1d4d8);
    if(quality_menu){
        aero_panel(fb,420,220,440,266,20);text(fb,448,243,27,"播放清晰度",WHITE);
        text(fb,450,280,15,"当前视频可用的清晰度",0xcab9d0e0);
        unsigned qns[4],count=0;
        if(quality_mask&4)qns[count++]=64;
        if(quality_mask&2)qns[count++]=32;
        if(quality_mask&1)qns[count++]=16;
        if(!count)qns[count++]=quality;
        for(unsigned i=0;i<count;i++){
            if((int)i==quality_selected){text(fb,438,318+(int)i*51,24,"›",WHITE);rect(fb,463,350+(int)i*51,350,1,0xb0ffffff);}
            text(fb,467,320+(int)i*51,23,qns[i]>=64?"720P  高清":qns[i]>=32?"480P  标准":"360P  流畅",(int)i==quality_selected?WHITE:0xb8b5cbdc);
        }
    }
    native_ui_alpha_target(0);
}

void native_ui_keyboard(uint32_t *fb,const char *value,int selected,int lowercase)
{
    rect(fb,140,196,1000,440,0x00112134);stroke(fb,140,196,1000,440,0x00435e78);
    text(fb,174,222,28,"搜索视频",WHITE);
    text(fb,174,262,17,"简易键盘 · 可输入英文、数字或 BV 号",MUTED);
    rect(fb,170,298,940,43,0x000a1828);label(fb,183,307,23,910,1,value,WHITE);
    const char *keys=UI_SEARCH_KEYS;
    for(int i=0;keys[i];i++){
        int x=174+(i%10)*93,y=358+(i/10)*51;
        if(i==selected){rect(fb,x-5,y-4,79,43,0x00304862);stroke(fb,x-5,y-4,79,43,LIGHT);}
        char key[2]={keys[i],0};if(lowercase && key[0]>='A' && key[0]<='Z')key[0]+=32;
        text(fb,x+23,y+3,25,key[0]==' '?"_":key,i==selected?WHITE:MUTED);
    }
    text(fb,174,582,18,"叉键 输入   方块 删除   L1 大小写   START / △ 搜索   圆圈 取消",MUTED);
}

void native_ui_frost_exit(uint32_t *fb)
{
    uint32_t small[30*14];
    for(int y=0;y<14;y++)for(int x=0;x<30;x++){
        unsigned r=0,g=0,b=0,n=0;
        for(int dy=-8;dy<24;dy+=4)for(int dx=-8;dx<24;dx+=4){
            unsigned c=fb[(250+y*16+dy)*UI_WIDTH+400+x*16+dx];
            r+=(c>>16)&255;g+=(c>>8)&255;b+=c&255;n++;
        }
        small[y*30+x]=0xff000000|(r/n<<16)|(g/n<<8)|b/n;
    }
    for(int y=0;y<220;y++)for(int x=0;x<480;x++){
        int ix=x/16,iy=y/16,jx=ix<29?ix+1:ix,jy=iy<13?iy+1:iy;
        unsigned out=0xff000000;
        for(int ch=0;ch<24;ch+=8){
            unsigned a=(small[iy*30+ix]>>ch)&255,b=(small[iy*30+jx]>>ch)&255;
            unsigned c=(small[jy*30+ix]>>ch)&255,d=(small[jy*30+jx]>>ch)&255;
            out|=(((a*(16-x%16)+b*(x%16))*(16-y%16)+(c*(16-x%16)+d*(x%16))*(y%16))/256)<<ch;
        }
        fb[(250+y)*UI_WIDTH+400+x]=out;
    }
}
void native_ui_loading(uint32_t *fb,const uint32_t *background,uint64_t now,int canceling)
{
    native_ui_alpha_target(0);memcpy(fb,background,UI_WIDTH*UI_HEIGHT*4);
    for(unsigned i=0;i<UI_WIDTH*UI_HEIGHT;i++)fb[i]|=0xff000000;
    // XMB-like orbit, driven by time rather than network progress.
    native_ui_alpha_target(1);
    unsigned phase=(unsigned)(now/80000)%12;
    for(unsigned i=0;i<12;i++){
        float angle=(float)i*6.2831853f/12;
        unsigned alpha=40+((i+12-phase)%12)*18;
        aero_round(fb,634+(int)(cosf(angle)*36),296+(int)(sinf(angle)*36),8,8,4,(alpha<<24)|0xffffff);
    }
    text(fb,542,378,26,canceling?"正在取消…":"正在打开视频",WHITE);
    text(fb,550,427,18,"圆圈键取消并返回",0xb0ffffff);
    native_ui_alpha_target(0);
}
void native_ui_exit(uint32_t *fb,int choice)
{
    if(transparent_target)aero_panel(fb,400,250,480,220,20);
    else {native_ui_frost_exit(fb);native_ui_alpha_target(1);aero_panel(fb,400,250,480,220,20);native_ui_alpha_target(0);}
    int title_width=0;ui_font_measure_text(30,"确认退出吗",&title_width,NULL);
    text(fb,400+(480-title_width)/2,284,30,"确认退出吗",WHITE);
    const char *names[]={"取消","退出"};
    for(int i=0;i<2;i++){
        int x=444+i*210;if(i==choice)rect(fb,x,420,182,1,transparent_target?0xd0ffffff:WHITE);
        int width=0;ui_font_measure_text(24,names[i],&width,NULL);
        text(fb,x+(182-width)/2,379,24,names[i],i==choice?WHITE:MUTED);
    }
}

static void detail_image(uint32_t *fb,int x,int y,int w,int h,const char *url)
{
    if(!url || !*url || !ui_image_draw(fb,UI_WIDTH,UI_HEIGHT,x,y,w,h,url))return;
    for(int yy=y;yy<y+h && yy<(int)UI_HEIGHT;yy++)for(int xx=x;xx<x+w && xx<(int)UI_WIDTH;xx++)if(xx>=0 && yy>=0)fb[yy*UI_WIDTH+xx]|=0xff000000u;
}

void native_ui_reset_detail_scroll(void){memset(&detail_scroll_state,0,sizeof(detail_scroll_state));}
int native_ui_detail_animate(unsigned focus,unsigned comments,int creator,uint64_t now)
{return creator?0:detail_scroll_step(&detail_scroll_state,focus,comments,now);}
unsigned native_ui_detail_scroll_offset(void){return (unsigned)(detail_scroll_state.value+0.5f);}
void native_ui_focus_comments(unsigned comments,uint64_t now,int immediate)
{
    unsigned maximum=detail_content_height(comments)-DETAIL_BODY_H;
    float target=DETAIL_COMMENT_Y<maximum?DETAIL_COMMENT_Y:maximum;
    detail_scroll_state.initialized=1;detail_scroll_state.from=immediate?target:detail_scroll_state.value;
    detail_scroll_state.target=target;detail_scroll_state.begin=now;if(immediate)detail_scroll_state.value=target;
}
static void detail_shell(uint32_t *fb,const bili_detail_state *s,int creator)
{
    memset(fb,0,UI_WIDTH*UI_HEIGHT*4);native_ui_alpha_target(1);scene_x=scene_y=0;
    text(fb,32,14,19,creator?"bibilabu  ›  视频  ›  UP 主":"bibilabu  ›  视频",0xfff5f7fa);
    label(fb,410,17,14,420,1,s->status,0xc0ffffff);
    stroke(fb,30,46,804,454,0x50ffffff);
    rect(fb,850,46,1,632,0x30ffffff);
    text(fb,873,48,23,creator?"UP 主投稿":"相关推荐",WHITE);
    const bili_feed_item_t *items=creator?s->creator:s->related;
    int count=creator?s->creator_count:s->related_count;
    for(int i=0;i<count;i++){
        int y=84+i*72;detail_image(fb,873,y+6,96,54,items[i].pic_url);
        label(fb,979,y+7,17,260,2,items[i].title,WHITE);
        rect(fb,873,y+67,372,1,0x20ffffff);
    }
    if(!count)label(fb,878,104,18,350,4,creator?(s->creator_status[0]?s->creator_status:"正在读取投稿…"):(s->busy?"正在读取相关推荐…":"暂无相关推荐"),0xd0ffffff);
    char pages[96];unsigned page=creator?s->creator_page:s->related_page;
    if(creator)snprintf(pages,sizeof(pages),"第 %u 页   L2 上页   R2 下页",page?page:1);
    else snprintf(pages,sizeof(pages),"第 %u / %u 页   L2 上页   R2 下页",page?page:1,s->related_pages?s->related_pages:1);
    text(fb,873,660,14,pages,0xe0ffffff);
    text(fb,873,688,14,creator?"↑↓ 选择投稿   △ 刷新   ○ 返回":"↑↓ 选择推荐   叉键 播放",0xc0ffffff);
    native_ui_alpha_target(0);
}
static void detail_body(uint32_t *fb,const bili_detail_state *s,const char *title,int creator,unsigned density)
{
    memset(fb,0,UI_WIDTH*UI_HEIGHT*4);native_ui_alpha_target(1);scene_x=scene_y=0;
    label(fb,32,0,22,800,1,creator?(s->up_name[0]?s->up_name:"UP 主资料"):s->title[0]?s->title:title,WHITE);
    detail_image(fb,40,30,40,40,s->up_face);label(fb,94,32,19,175,1,s->up_name[0]?s->up_name:"读取UP主…",WHITE);
    if(!creator)text(fb,94,55,13,"打开UP主主页",0xb0ffffff);
    if(creator){
        char info[160];snprintf(info,sizeof(info),"UID %llu     粉丝 %u     %s",(unsigned long long)s->mid,s->fans,s->following?"已关注":"未关注");text(fb,286,39,17,info,0xe0ffffff);
        label(fb,32,86,18,800,3,s->creator_status[0]?s->creator_status:"正在读取UP主投稿…",0xe0ffffff);
        text(fb,32,150,15,"右侧选择投稿    三角刷新    圆圈返回详情",0xc0ffffff);
        native_ui_alpha_target(0);return;
    }
    const int bx[4]={286,420,535,660},bw[4]={120,100,110,112};
    const char *labels[4]={s->following?"已关注":"关注",s->liked?"已点赞":"点赞","评论区","写评论"};
    for(int i=0;i<4;i++){text(fb,bx[i]+13,40,18,labels[i],WHITE);rect(fb,bx[i],68,bw[i],1,0x30ffffff);}
    char stats[128];snprintf(stats,sizeof(stats),"播放 %u    点赞 %u",s->views,s->likes);text(fb,32,80,15,stats,0xc0ffffff);
    char dm[64];if(density==6)snprintf(dm,sizeof(dm),"弹幕 6 / 6 全量 · 方块切换");else snprintf(dm,sizeof(dm),"弹幕 %u / 6  ·  方块切换",density);text(fb,420,80,15,dm,0xe0ffffff);
    rect(fb,32,102,800,1,0x30ffffff);
    snprintf(stats,sizeof(stats),"评论 %u   ·   第 %u 页   ·   上下滚动",s->replies,s->comment_page?s->comment_page:1);text(fb,32,108,17,stats,WHITE);
    for(int i=0;i<s->comment_count;i++){
        int y=DETAIL_COMMENT_Y+i*DETAIL_COMMENT_ROW;
        label(fb,42,y+4,17,650,1,s->comments[i].author,0xe0ffffff);
        text(fb,734,y+5,13,"查看全文",0x98ffffff);
        label(fb,42,y+29,17,772,2,s->comments[i].message,WHITE);
        rect(fb,32,y+78,800,1,0x28ffffff);
    }
    if(!s->comment_count)label(fb,42,DETAIL_COMMENT_Y+5,18,770,3,s->comment_status[0]?s->comment_status:s->busy?"正在读取评论…":"评论暂不可用",0xd0ffffff);
    native_ui_alpha_target(0);
}
static void detail_refresh_images(uint32_t *shell,uint32_t *body,const bili_detail_state *s,int creator)
{
    detail_image(body,40,30,40,40,s->up_face);
    const bili_feed_item_t *items=creator?s->creator:s->related;
    int count=creator?s->creator_count:s->related_count;
    for(int i=0;i<count;i++)detail_image(shell,873,90+i*72,96,54,items[i].pic_url);
}
static void body_focus(uint32_t *fb,int x,int content_y,int w,int h,unsigned scroll)
{
    int y=(int)DETAIL_BODY_Y+content_y-(int)scroll;
    int top=y>(int)DETAIL_BODY_Y?y:(int)DETAIL_BODY_Y;
    int bottom=y+h<(int)(DETAIL_BODY_Y+DETAIL_BODY_H)?y+h:(int)(DETAIL_BODY_Y+DETAIL_BODY_H);
    for(int yy=top;yy<bottom;yy++)for(int xx=x;xx<x+w;xx++){
        if(xx<0 || xx>=(int)UI_WIDTH)continue;
        if(yy==y || yy==y+h-1 || xx==x || xx==x+w-1)fb[yy*UI_WIDTH+xx]=ui_rgba_over(fb[yy*UI_WIDTH+xx],0xc0ffffff);
    }
}
void native_ui_detail(uint32_t *fb,const bili_detail_state *s,const char *title,unsigned focus,int comments,int creator,unsigned density,uint64_t elapsed,uint64_t duration,unsigned state)
{
    (void)comments;
    static bili_detail_state saved;static unsigned saved_images,saved_density;static int saved_creator;
    unsigned images=ui_image_revision();if(!title)title="";
    if(!player_base_cache)player_base_cache=malloc(UI_WIDTH*UI_HEIGHT*4);
    if(!detail_body_cache)detail_body_cache=malloc(UI_WIDTH*UI_HEIGHT*4);
    if(!player_base_cache || !detail_body_cache){memset(fb,0,UI_WIDTH*UI_HEIGHT*4);return;}
    int dirty=player_base_kind!=2 || memcmp(&saved,s,sizeof(saved)) || saved_density!=density || saved_creator!=creator || strcmp(player_base_title,title);
    if(dirty){detail_shell(player_base_cache,s,creator);detail_body(detail_body_cache,s,title,creator,density);
        saved=*s;saved_images=images;saved_density=density;saved_creator=creator;snprintf(player_base_title,sizeof(player_base_title),"%s",title);player_base_kind=2;}
    else if(saved_images!=images){detail_refresh_images(player_base_cache,detail_body_cache,s,creator);saved_images=images;}
    memcpy(fb,player_base_cache,UI_WIDTH*UI_HEIGHT*4);
    unsigned scroll=creator?0:native_ui_detail_scroll_offset();unsigned maximum=detail_content_height((unsigned)s->comment_count)-DETAIL_BODY_H;if(scroll>maximum)scroll=maximum;
    for(unsigned y=0;y<DETAIL_BODY_H;y++)memcpy(fb+(DETAIL_BODY_Y+y)*UI_WIDTH+DETAIL_BODY_X,detail_body_cache+(scroll+y)*UI_WIDTH+DETAIL_BODY_X,DETAIL_BODY_W*4);
    native_ui_alpha_target(1);
    if(creator || (focus>=DETAIL_RELATED_FOCUS && focus<DETAIL_COMMENT_FOCUS)){
        unsigned i=creator?focus:focus-DETAIL_RELATED_FOCUS;
        if(i<(unsigned)(creator?s->creator_count:s->related_count)){
            unsigned y=84+i*72;text(fb,852,(int)y+14,28,"›",WHITE);rect(fb,873,(int)y+67,372,1,0xd0ffffff);
            stroke(fb,872,(int)y+5,98,56,0x90ffffff);
        }
    }else if(focus==0)stroke(fb,29,45,806,456,0xc0ffffff);
    else if(focus==1)body_focus(fb,32,28,241,44,scroll);
    else if(focus>=2 && focus<=5){const int x[4]={284,418,533,658},w[4]={124,104,114,116};body_focus(fb,x[focus-2],28,w[focus-2],44,scroll);}
    else if(focus==6)body_focus(fb,416,74,360,25,scroll);
    else if(focus>=DETAIL_COMMENT_FOCUS && focus-DETAIL_COMMENT_FOCUS<(unsigned)s->comment_count)body_focus(fb,32,DETAIL_COMMENT_Y+(int)(focus-DETAIL_COMMENT_FOCUS)*DETAIL_COMMENT_ROW,800,DETAIL_COMMENT_ROW,scroll);
    if(!creator){
        unsigned track=DETAIL_BODY_H-8,total=detail_content_height((unsigned)s->comment_count),thumb=track*DETAIL_BODY_H/total;if(thumb<12)thumb=12;
        rect(fb,835,DETAIL_BODY_Y+4,1,(int)track,0x30ffffff);
        unsigned pos=maximum?(track-thumb)*scroll/maximum:0;rect(fb,834,DETAIL_BODY_Y+4+(int)pos,3,(int)thumb,0xb0ffffff);
    }
    char a[32],b[32],line[128];clock_text(a,sizeof(a),elapsed);clock_text(b,sizeof(b),duration);
    snprintf(line,sizeof(line),"%s / %s  %s",a,b,(state&15)==2?"已暂停":(state>>4)==5?"正在缓冲":(state&15)==3?"播放结束":"");text(fb,32,691,14,line,0xe0ffffff);
    text(fb,300,691,13,creator?"SELECT全屏  ○返回":focus>=DETAIL_COMMENT_FOCUS?"↑↓滚动  R2下页  L2刷新  叉键全文":"↓进入评论  SELECT全屏  △暂停  ○返回",0xc0ffffff);
    native_ui_alpha_target(0);
}
void native_ui_confirm(uint32_t *fb,const char *title,const char *body,int choice)
{
    native_ui_alpha_target(1);rect(fb,0,0,1280,720,0x90000000);aero_panel(fb,200,154,880,410,22);
    text(fb,234,183,29,title,WHITE);label(fb,235,238,22,810,8,body,WHITE);
    if(!choice)rect(fb,498,545,160,1,0xd0ffffff);
    text(fb,550,515,20,"取消",WHITE);
    if(choice)rect(fb,690,545,160,1,0xd0ffffff);
    text(fb,742,515,20,"确认",WHITE);native_ui_alpha_target(0);
}
void native_ui_comment_view(uint32_t *fb,const char *author,const char *message)
{
    native_ui_alpha_target(1);rect(fb,0,0,1280,720,0xb0000000);aero_panel(fb,140,64,1000,590,22);
    label(fb,175,91,27,920,1,author,WHITE);label(fb,175,148,22,920,16,message,WHITE);
    text(fb,175,615,19,"叉键 / 方块键 回复    圆圈 返回",WHITE);native_ui_alpha_target(0);
}
