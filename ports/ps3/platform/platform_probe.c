// SPDX-License-Identifier: GPL-3.0-or-later
// wiliwili for PlayStation 3 - Native TV Client (1.2.0)
#include <sys/process.h>
#include <sys/file.h>
#include <sys/systime.h>
#include <sys/thread.h>
#include "runtime_log.h"
#include "native_ui.h"
#include "search_osk.h"
#include "wallpaper.h"
#include "ui_sound.h"
#include "rsx_video.h"
#include "danmaku_runtime.h"
#include "history_sync.h"
#include "ui_motion.h"
#include "video_geometry.h"
#include "bili_detail.h"
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <io/pad.h>
#include <rsx/rsx.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "probe_identity.h"
#include "native_video_probe.h"
#include "native_audio_probe.h"
#include "network_source.h"
#include "bili_play.h"
#include "bili_session.h"
#include "qr_login.h"
#include "bili_feed.h"
#include "ui_font.h"
#include "ui_image.h"
#include "ui_core.h"
#include "../third_party/qrcodegen/qrcodegen.h"

SYS_PROCESS_PARAM(1001, 0x100000);
extern void wili_platform_exit(void) __attribute__((noreturn));
extern void wili_early_checkpoint(const char *message);

static uint32_t *ui_surface,*ui_background;
static u32 ui_offset;
static u32 background_offset;static int background_mapped;
static u64 last_scroll_animation;
static int ui_dirty=1,player_drawn,ui_ready,gpu_fault;
static char ui_message[160];
static unsigned image_revision,rendered_generation;
static int last_overlay=-1;
static volatile int running = 1, menu_open;
static unsigned system_draw,menu_paused;
static u64 last_present_us;
static int animation_active,net_ok,exit_confirm,exit_choice,ui_sound_ready,video_renderer_ready;
static float osd_opacity;static u64 opacity_clock;
static unsigned known_quality_mask;
static bili_detail_state g_detail;
static int detail_mode=1,detail_focus,detail_comments,detail_creator,comment_open=-1;
static int video_page_pending;
static int comment_page_pending;
static int action_confirm,action_choice,osk_purpose,dm_dirty;
static unsigned dm_revision;
static uint64_t comment_parent;
static char comment_draft[1024];
static char quality_bvid[16];
static u64 next_reconnect,last_link_check;
static bili_feed_tab_t reconnect_tab;
static int refresh_after_network;static bili_feed_tab_t refresh_network_tab;
static int quick_search,quick_key,quick_lower=1;
static char quick_text[256];
static u64 frequency, started;
static gcmContextData *context;
static unsigned width, height, pitch;
static volatile u32 *buffers[2];
static u32 buffer_offsets[2];
static u32 command_arena_offset;
static gcmControlRegister volatile *command_control;
static unsigned command_recycles;
static u32 ticket;
static unsigned display_buffer, frame_count;
static struct {u64 video_us,compose_us,gpu_us,flip_us,max_video_us;unsigned renders,repeats,composes;} perf;
static int rsx_ready, pad_ready, callback_ready, first_flip = 1;
static s32 pad_init_rc;
static u32 previous_buttons[MAX_PORT_NUM];
static u64 direction_repeat[MAX_PORT_NUM],input_after;

#define BTN_UP        ((u32)PAD_CTRL_UP << 16)
#define BTN_DOWN      ((u32)PAD_CTRL_DOWN << 16)
#define BTN_LEFT      ((u32)PAD_CTRL_LEFT << 16)
#define BTN_RIGHT     ((u32)PAD_CTRL_RIGHT << 16)
#define BTN_START     ((u32)PAD_CTRL_START << 16)
#define BTN_SELECT    ((u32)PAD_CTRL_SELECT << 16)

#define BTN_SQUARE    ((u32)PAD_CTRL_SQUARE)
#define BTN_CROSS     ((u32)PAD_CTRL_CROSS)
#define BTN_CIRCLE    ((u32)PAD_CTRL_CIRCLE)
#define BTN_TRIANGLE  ((u32)PAD_CTRL_TRIANGLE)
#define BTN_R1        ((u32)PAD_CTRL_R1)
#define BTN_L1        ((u32)PAD_CTRL_L1)
#define BTN_R2        ((u32)PAD_CTRL_R2)
#define BTN_L2        ((u32)PAD_CTRL_L2)

// App Views & State
typedef enum {
    VIEW_FEED = 0,    // Main TV browsing view (Tabs: 推荐/热门/历史/收藏/搜索/用户 + Video Grid)
    VIEW_PLAYER = 1   // Video player view (Full screen + OSD + Quality switcher)
} app_view_t;

static app_view_t g_view = VIEW_FEED;
static bili_feed_state_t g_feed;
static wili_qr_login g_qr;

static int g_focused_card = 0;       // 0..item_count-1
static int g_tab_focus = 0;          // 0 = in card grid, 1 = in top tab bar
static int g_osd_visible = 1;
static u64 g_last_osd_action = 0;
static int g_quality_menu_open = 0;  // in player: 1 = quality menu open
static int g_quality_menu_sel = 0;   // selected quality index in menu
static unsigned g_current_qn = 64;   // 64 = 720P, 32 = 480P, 16 = 360P, 80 = 1080P
static char g_active_bvid[16];

static void text_log(const char *text){runtime_log_write(text);}

static void result_log(const char *name, s32 result)
{
    char line[112];
    snprintf(line,sizeof(line),"%s rc=0x%08x",name,(u32)result);
    text_log(line);
}

static void open_logs(void)
{
    if(runtime_log_init())wili_early_checkpoint("BIBI_RUNTIME_LOG_INIT_FAILED");
    text_log(WILI_PROBE_ID " MAIN_REACHED");
    text_log("scope=xmb_clear_2.1.0 pinned_video left_comment_scroll");
}

static u64 now_us(void)
{
    if(!frequency)return 0;
    u64 ticks;
    __asm__ volatile("mftb %0":"=r"(ticks));
    return (ticks/frequency)*1000000ULL+((ticks%frequency)*1000000ULL)/frequency;
}

static void callback(u64 status,u64 param,void *userdata)
{
    (void)param;(void)userdata;
    char line[112];snprintf(line,sizeof(line),"SYSUTIL_EVENT status=%04llx param=%llu",(unsigned long long)status,(unsigned long long)param);text_log(line);
    search_osk_event(status);
    if(status==SYSUTIL_EXIT_GAME)running=0;
    if(status==SYSUTIL_MENU_OPEN)menu_open=1;
    if(status==SYSUTIL_MENU_CLOSE){menu_open=0;system_draw=0;ui_dirty=1;input_after=now_us()+200000;}
    if(status==SYSUTIL_OSK_UNLOADED){if(!menu_open)system_draw=0;ui_dirty=1;input_after=now_us()+200000;}
    if(status==SYSUTIL_DRAW_BEGIN)system_draw=1;
    if(status==SYSUTIL_DRAW_END){system_draw=0;ui_dirty=1;}

}

static int command_wait_get(u32 target)
{
    u64 begin=now_us();
    while(command_control->get!=target){
        if(now_us()-begin>500000ULL){text_log("RSX_COMMAND_GET_TIMEOUT");return -1;}
        sysUsleep(100);
    }
    __sync_synchronize();
    return 0;
}

static int install_command_arena(void)
{
    text_log("RSX_CUSTOM_ARENA_ALLOC_BEGIN");
    void *arena=memalign(1024*1024,1024*1024);
    if(!arena){text_log("RSX_CUSTOM_ARENA_ALLOC_FAILED");return -1;}
    int rc=gcmMapMainMemory(arena,1024*1024,&command_arena_offset);
    result_log("RSX_CUSTOM_ARENA_MAP",rc);if(rc){free(arena);return rc;}
    command_control=gcmGetControlRegister(context);
    if(context->end-context->current<16){text_log("RSX_INITIAL_JUMP_RESERVE_LOW");return -1;}
    rsxSetJumpCommand(context,command_arena_offset);
    __sync_synchronize();command_control->put=command_arena_offset;
    if(command_wait_get(command_arena_offset))return -1;
    rsxSetCurrentBuffer(&context,arena,64*1024);
    context->callback=NULL;
    text_log("RSX_CUSTOM_CONTEXT_READY bytes=65536 default_rollover_callback=disabled");
    return 0;
}

__attribute__((unused)) static int recycle_command_arena(void)
{
    text_log("RSX_COMMAND_RECYCLE_BEGIN");
    if(command_wait_get(command_control->put))return -1;
    if(context->end-context->current<16){text_log("RSX_RECYCLE_JUMP_RESERVE_LOW");return -1;}
    rsxSetJumpCommand(context,command_arena_offset);
    __sync_synchronize();command_control->put=command_arena_offset;
    if(command_wait_get(command_arena_offset))return -1;
    context->current=context->begin;
    command_recycles++;
    char line[112];snprintf(line,sizeof(line),"RSX_COMMAND_RECYCLE_DONE count=%u",command_recycles);text_log(line);
    return 0;
}

static int init_display(void)
{
    videoState state;
    videoResolution resolution;
    videoConfiguration config;
    int i,rc;
    text_log("RSX_IO_ALLOC_BEGIN");
    void *io=memalign(1024*1024,1024*1024);
    if(!io){text_log("RSX_IO_ALLOC_FAILED");return -1;}
    text_log("RSX_INIT_BEGIN");
    rc=rsxInit(&context,0x10000,1024*1024,io);
    result_log("RSX_INIT",rc);if(rc)return rc;
    if(install_command_arena())return -1;
    memset(&state,0,sizeof(state));
    rc=videoGetState(VIDEO_PRIMARY,0,&state);
    result_log("VIDEO_STATE",rc);if(rc)return rc;
    rc=videoGetResolution(state.displayMode.resolution,&resolution);
    result_log("VIDEO_RESOLUTION",rc);if(rc)return rc;
    width=resolution.width;height=resolution.height;
    if(width<320 || width>1920 || height<240 || height>1080){text_log("UNEXPECTED_VIDEO_DIMENSIONS");return -1;}
    pitch=width*4;
    memset(&config,0,sizeof(config));
    config.resolution=state.displayMode.resolution;
    config.format=VIDEO_BUFFER_FORMAT_XRGB;
    config.aspect=state.displayMode.aspect;
    config.pitch=pitch;
    text_log("VIDEO_CONFIGURE_BEGIN");
    rc=videoConfigure(VIDEO_PRIMARY,&config,0,1);
    result_log("VIDEO_CONFIGURE",rc);if(rc)return rc;
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    for(i=0;i<2;i++) {
        u32 offset=0;
        buffers[i]=rsxMemalign(64,pitch*height);
        if(!buffers[i]){text_log("DISPLAY_BUFFER_ALLOC_FAILED");return -1;}
        rc=rsxAddressToOffset((const void*)buffers[i],&offset);
        result_log("DISPLAY_BUFFER_OFFSET",rc);if(rc)return rc;
        buffer_offsets[i]=offset;
        rc=gcmSetDisplayBuffer((u8)i,offset,pitch,width,height);
        result_log("DISPLAY_BUFFER_REGISTER",rc);if(rc)return rc;
    }
    char line[100];
    snprintf(line,sizeof(line),"DISPLAY_READY width=%u height=%u pitch=%u",width,height,pitch);
    text_log(line);
    *gcmGetLabelAddress(64)=0;__sync_synchronize();
    rsx_ready=1;
    return 0;
}

static int wait_flip(unsigned timeout_ms)
{
    unsigned elapsed;
    for(elapsed=0;elapsed<timeout_ms;elapsed++) {
        if(gcmGetFlipStatus()==0)return 0;
        if(callback_ready)sysUtilCheckCallback();
        sysUsleep(1000);
    }
    text_log("FLIP_WAIT_TIMEOUT");
    return -1;
}

int wili_probe_network_cancel(void)
{
    return !running;
}

// A single short-lived request worker owns session setup or a private QR copy.
// The main thread joins it before publishing results or touching its session.
static sys_ppu_thread_t request_thread;
static volatile u32 request_done,request_cancel;
static int request_kind,request_result;
static char request_bvid[16],request_cookie[1024];
static char request_error[160];
static unsigned request_qn;
static u64 request_seek_ms;
static int request_exact;
static wili_qr_login request_qr;
static u64 request_time;
static int api_cancel(void *arg){(void)arg;return !running || __sync_add_and_fetch(&request_cancel,0);}
static unsigned quality_bit(unsigned qn){return qn==80?8u:qn==64?4u:qn==32?2u:qn==16?1u:0u;}
static int open_decoder_compatible_session(void)
{
    wili_bili_session *s=wili_bili_play_session();
    int rc=wili_bili_session_open_ex(s,request_bvid,request_qn,request_cookie[0]?request_cookie:NULL,
        "/dev_hdd0/game/WILIPRB01/USRDIR/cacert.pem",api_cancel,NULL,text_log);
    if(rc || request_cancel || !running)return rc?rc:-1;
    unsigned excluded=0;
    for(;;){
        unsigned level=0;const wili_dash_info *video=wili_mp4_dash_info(&s->video);
        int compatible=video && !wili_avc_ps3_level(&video->sps,&level);
        if(compatible && !wili_video_is_720(video->coded_width,video->coded_height)){
            unsigned num=0,den=0;
            compatible=!wili_mp4_dash_probe_rate(&s->video,30,&num,&den);
            char rate_line[192];snprintf(rate_line,sizeof(rate_line),"AVC_RATE_PREFLIGHT quality=%u sample_fps=%u/%u limit=30 result=%s vui_fixed=%u",
                s->returned_qn,num,den,compatible?"accepted":"rejected",video->sps.fixed_frame_rate);text_log(rate_line);
            if(!compatible)snprintf(request_error,sizeof(request_error),"片源超过30帧或帧时间无法验证，请选720P");
        }else if(!compatible)snprintf(request_error,sizeof(request_error),"片源编码超出PS3解码条件，请选择较低画质");
        if(compatible){
            s->quality_mask&=~excluded;
            request_error[0]=0;
            {char line[128];snprintf(line,sizeof(line),"AVC_PS3_PREFLIGHT quality=%u level=%u coded=%ux%u refs=%u",s->returned_qn,level,video->coded_width,video->coded_height,video->refs);text_log(line);}
            return 0;
        }
        unsigned old=s->returned_qn;excluded|=quality_bit(old);
        char line[160];snprintf(line,sizeof(line),"AVC_PS3_PREFLIGHT_REJECT quality=%u encoded=%ux%u SPS_or_DPB_unsupported",old,video?video->coded_width:0,video?video->coded_height:0);text_log(line);
        s->quality_mask&=~excluded;
        if(request_exact || old<=16 || request_cancel || !running)return -2;
        unsigned next=old>=80 && (s->quality_mask&4)?64:old>=64 && (s->quality_mask&2)?32:16;
        if(!(s->quality_mask&quality_bit(next)))return -2;
        wili_bili_session_close(s,text_log);
        snprintf(line,sizeof(line),"AVC_PS3_PREFLIGHT_FALLBACK requested=%u next=%u",old,next);text_log(line);
        rc=wili_bili_session_open_ex(s,request_bvid,next,request_cookie[0]?request_cookie:NULL,
            "/dev_hdd0/game/WILIPRB01/USRDIR/cacert.pem",api_cancel,NULL,text_log);
        if(rc || request_cancel || !running){s->quality_mask&=~excluded;return rc?rc:-1;}
    }
}
static void request_worker(void *arg)
{
    (void)arg;
    if(request_kind==1){
        wili_bili_session *session=wili_bili_play_session();request_result=open_decoder_compatible_session();
        if(!request_result && request_exact && session->returned_qn!=request_qn){
            request_result=-2;
            char line[128];snprintf(line,sizeof(line),"BILI_QUALITY_UNAVAILABLE requested=%u returned=%u mask=%u",request_qn,session->returned_qn,session->quality_mask);text_log(line);
            snprintf(request_error,sizeof(request_error),"当前视频或账号未返回所选画质，请选其他档位");
        }
        if(!request_result && request_seek_ms){
            uint64_t target=request_seek_ms<session->duration_ms?request_seek_ms:(session->duration_ms>1000?session->duration_ms-1000:0),unused;
            if(wili_mp4_dash_seek(&session->video,target,&unused) || wili_mp4_dash_seek(&session->audio,target,&unused))request_result=-3;
            else session->seek_ms=target;
        }
    }
    else if(request_kind==2)request_result=wili_qr_login_generate(&request_qr);
    else if(request_kind==3)request_result=wili_qr_login_poll(&request_qr,request_time);
    else if(request_kind==6){
        bili_feed_shutdown(&g_feed);ui_image_shutdown();bili_detail_shutdown();danmaku_shutdown();history_sync_shutdown(0);wili_network_shutdown(text_log);
        request_result=wili_network_prepare(text_log);
    }else request_result=wili_qr_login_logout(&request_qr);
    __sync_lock_test_and_set(&request_done,1);sysThreadExit(0);
}
static int launch_request(int kind)
{
    if(request_kind)return -1;
    if(kind==4)history_sync_account(NULL);
    request_kind=kind;request_qr=g_qr;request_qr.cancel=api_cancel;request_qr.cancel_arg=NULL;request_time=now_us();request_done=0;request_cancel=0;
    request_error[0]=0;
    int rc=sysThreadCreate(&request_thread,request_worker,NULL,1050,512*1024,THREAD_JOINABLE,"bibi-request");
    if(rc){request_kind=0;result_log("REQUEST_WORKER_CREATE",rc);return -1;}
    ui_dirty=1;return 0;
}
static void reconnect_network(void);
static int stop_playback(void)
{
    if(g_view==VIEW_PLAYER)history_sync_end(wili_video_playback_time_us()/1000,(wili_video_state()&15)==3);
    // Every submitted transfer has completed its fence before control gets here.
    if(wili_video_stop_safe()){
        text_log("PLAYER_STOP_UNSAFE process_exit_required");running=0;return -1;
    }
    g_view=VIEW_FEED;ui_dirty=1;player_drawn=0;last_overlay=-1;
    danmaku_stop();bili_detail_cancel();comment_open=-1;detail_creator=action_confirm=comment_page_pending=video_page_pending=0;comment_draft[0]=0;
    ui_sound_set_muted(0);
    ui_image_pause(0);return 0;
}
static void reconnect_network(void);
static void start_playback_at(const char *bvid,unsigned qn,u64 seek_ms,int exact)
{
    if(request_kind)return;
    if(!net_ok){reconnect_network();return;}
    if(!video_renderer_ready){if(rsx_video_init(width,height,text_log)){snprintf(ui_message,sizeof(ui_message),"播放器图形初始化失败，请看日志");ui_dirty=1;return;}video_renderer_ready=1;}
    char copy[16];snprintf(copy,sizeof(copy),"%s",bvid);
    if(stop_playback())return;
    if(!exact || strcmp(copy,quality_bvid)){known_quality_mask=0;snprintf(quality_bvid,sizeof(quality_bvid),"%s",copy);}
    snprintf(request_bvid,sizeof(request_bvid),"%s",copy);request_qn=qn;request_seek_ms=seek_ms;request_exact=exact;
    snprintf(request_cookie,sizeof(request_cookie),"%s",g_qr.is_logged_in?g_qr.cookie:"");
    ui_image_pause(1);
    ui_sound_set_muted(1);
    snprintf(ui_message,sizeof(ui_message),"正在连接视频…  圆圈键可取消");
    if(launch_request(1)){ui_image_pause(0);snprintf(ui_message,sizeof(ui_message),"无法启动播放请求，请重试");}
}
static void start_playback(const char *bvid,unsigned qn)
{detail_mode=1;detail_focus=detail_comments=0;start_playback_at(bvid,qn,0,0);}
static unsigned quality_values(unsigned out[4])
{
    unsigned n=0,mask=known_quality_mask|wili_bili_play_session()->quality_mask;
    if(mask&4)out[n++]=64;
    if(mask&2)out[n++]=32;
    if(mask&1)out[n++]=16;
    if(!n)out[n++]=g_current_qn;
    return n;
}
static void reconnect_network(void)
{
    if(request_kind || g_view==VIEW_PLAYER)return;
    reconnect_tab=g_feed.current_tab;
    g_feed.item_count=0;g_feed.loading=1;
    snprintf(g_feed.status_msg,sizeof(g_feed.status_msg),"正在恢复网络连接，账号将保留…");
    next_reconnect=now_us()+15000000ULL;
    if(launch_request(6)){g_feed.loading=0;net_ok=0;}
    ui_dirty=1;
}
static void ask_exit(void){exit_confirm=1;exit_choice=0;ui_dirty=1;}
static void seek_relative(int seconds)
{
    int64_t target=(int64_t)(wili_video_playback_time_us()/1000)+(int64_t)seconds*1000;
    if(target<0)target=0;
    int rc=wili_video_seek((uint64_t)target,now_us());
    if(rc==-2){running=0;return;}
    if(rc){stop_playback();snprintf(ui_message,sizeof(ui_message),"跳转失败，可重新播放此视频");}
    g_last_osd_action=now_us();g_osd_visible=1;opacity_clock=g_last_osd_action;player_drawn=0;rendered_generation=0;ui_dirty=1;
}
static void pump_request(void)
{
    if(!request_kind || !__sync_add_and_fetch(&request_done,0))return;
    u64 result;sysThreadJoin(request_thread,&result);int kind=request_kind;request_kind=0;
    if(kind==6){
        net_ok=!request_result;const char *ca="/dev_hdd0/game/WILIPRB01/USRDIR/cacert.pem";
        g_feed.loading=0;g_feed.network_error=0;
        if(net_ok){
            if(bili_detail_init(ca,text_log))text_log("DETAIL_INIT_FAILED");
            if(danmaku_init(ca,text_log))text_log("DANMAKU_INIT_FAILED");
        native_ui_dm_density(danmaku_get_density());
            if(history_sync_init(ca,text_log))text_log("HISTORY_INIT_FAILED");
            bili_feed_init(&g_feed,g_qr.is_logged_in?g_qr.cookie:NULL,ca,text_log);ui_image_init(ca,text_log);
            bili_feed_load_tab(&g_feed,reconnect_tab);wallpaper_poll(now_us(),1,text_log);
            if(refresh_after_network && refresh_network_tab==reconnect_tab)bili_feed_refresh(&g_feed);
            refresh_after_network=0;
            ui_message[0]=0;text_log("NETWORK_REINITIALIZED account_preserved");
        }else snprintf(g_feed.status_msg,sizeof(g_feed.status_msg),"网络未就绪，稍后自动重试；三角键重连");
        next_reconnect=now_us()+15000000ULL;
    }else if(kind==1){
        if(!request_exact && !request_result)known_quality_mask=wili_bili_play_session()->quality_mask;
        else known_quality_mask|=wili_bili_play_session()->quality_mask;
        if(request_result || request_cancel || !running){
            if(request_result==-2)known_quality_mask=wili_bili_play_session()->quality_mask;
            if(wili_bili_play_session()->network_error)g_feed.network_error=1;
            wili_bili_session_close(wili_bili_play_session(),text_log);ui_image_pause(0);
            ui_sound_set_muted(0);
            snprintf(ui_message,sizeof(ui_message),"%s",request_cancel?"已取消播放":request_error[0]?request_error:request_result==-2?"该视频当前没有所选清晰度，请选择可用档位":request_result==-3?"该片源暂不支持跳转":"视频连接失败，请重试或选择其他视频");
        }else{
            snprintf(g_active_bvid,sizeof(g_active_bvid),"%s",request_bvid);
            g_current_qn=wili_bili_play_session()->returned_qn;
            wili_video_start(now_us(),text_log);g_view=VIEW_PLAYER;player_drawn=0;
            native_ui_reset_detail_scroll();
            memset(&g_detail,0,sizeof(g_detail));
            snprintf(g_detail.bvid,sizeof(g_detail.bvid),"%s",request_bvid);
            snprintf(g_detail.title,sizeof(g_detail.title),"%s",wili_bili_play_session()->title);
            bili_detail_open(request_bvid,g_qr.is_logged_in?g_qr.cookie:NULL);
            danmaku_start(wili_bili_play_session()->cid);
            history_sync_begin(wili_bili_play_session()->aid,wili_bili_play_session()->cid,wili_video_duration_ms(),g_qr.is_logged_in?g_qr.cookie:NULL);
            rendered_generation=0;last_overlay=-1;g_osd_visible=1;g_quality_menu_open=0;
            g_last_osd_action=now_us();opacity_clock=g_last_osd_action;osd_opacity=0;ui_message[0]=0;
        }
    }else{
        if(g_qr.is_logged_in!=request_qr.is_logged_in || strcmp(g_qr.dede_user_id,request_qr.dede_user_id)){known_quality_mask=0;quality_bvid[0]=0;}
        g_qr=request_qr;
        history_sync_account(g_qr.is_logged_in?g_qr.cookie:NULL);
        if(g_qr.is_logged_in || kind==4)bili_feed_set_cookie(g_qr.is_logged_in?g_qr.cookie:NULL);
        if(kind==4 && request_result)snprintf(ui_message,sizeof(ui_message),"退出登录未能保存，请检查存储设备");
    }
    ui_dirty=1;
}
static void cache_flush(const void *ptr,size_t bytes)
{
    for(size_t i=0;i<bytes;i+=128)__asm__ volatile("dcbst 0,%0"::"r"((const char*)ptr+i):"memory");
    __sync_synchronize();
}
static int init_ui(void)
{
    ui_surface=memalign(1048576,4*1048576);ui_background=memalign(1048576,4*1048576);
    if(!ui_surface || !ui_background)return -1;
    s32 rc=gcmMapMainMemory(ui_surface,4*1048576,&ui_offset);result_log("UI_RSX_MAP",rc);if(rc)return -1;
    rc=gcmMapMainMemory(ui_background,4*1048576,&background_offset);result_log("WALLPAPER_RSX_MAP",rc);if(rc)return -1;
    background_mapped=1;native_ui_background(ui_background);cache_flush(ui_background,UI_WIDTH*UI_HEIGHT*4);ui_ready=1;return 0;
}
static void transfer(u32 source,unsigned source_pitch,unsigned sw,unsigned sh,int dx,int dy,unsigned dw,unsigned dh)
{
    gcmTransferScale scale;gcmTransferSurface surface;memset(&scale,0,sizeof(scale));memset(&surface,0,sizeof(surface));
    scale.conversion=GCM_TRANSFER_CONVERSION_TRUNCATE;scale.format=GCM_TRANSFER_SCALE_FORMAT_A8R8G8B8;
    scale.operation=GCM_TRANSFER_OPERATION_SRCCOPY;
    scale.clipX=scale.outX=(s16)dx;scale.clipY=scale.outY=(s16)dy;
    scale.clipW=scale.outW=(u16)dw;scale.clipH=scale.outH=(u16)dh;
    scale.ratioX=(s32)(((u64)sw<<20)/dw);scale.ratioY=(s32)(((u64)sh<<20)/dh);
    scale.inW=(u16)sw;scale.inH=(u16)sh;scale.pitch=(u16)source_pitch;
    scale.origin=GCM_TRANSFER_ORIGIN_CORNER;scale.interp=GCM_TRANSFER_INTERPOLATOR_LINEAR;scale.offset=source;
    surface.format=GCM_TRANSFER_SURFACE_FORMAT_A8R8G8B8;surface.pitch=(u16)pitch;surface.offset=buffer_offsets[display_buffer];
    rsxSetTransferScaleMode(context,GCM_TRANSFER_MAIN_TO_LOCAL,GCM_TRANSFER_SURFACE);
    rsxSetTransferScaleSurface(context,&scale,&surface);
}
static void transfer_ui(int x,int y,unsigned w,unsigned h)
{
    unsigned x1=(unsigned)x*width/UI_WIDTH,y1=(unsigned)y*height/UI_HEIGHT;
    unsigned x2=((unsigned)x+w)*width/UI_WIDTH,y2=((unsigned)y+h)*height/UI_HEIGHT;
    transfer(ui_offset+((unsigned)y*UI_WIDTH+(unsigned)x)*4,UI_WIDTH*4,w,h,(int)x1,(int)y1,x2-x1,y2-y1);
}
static int repeat_display(void)
{
    if(first_flip || !rsx_ready || gpu_fault || gcmGetFlipStatus()!=0)return 0;
    if(context->end-context->current<64 && recycle_command_arena())return -1;
    gcmResetFlipStatus();s32 rc=gcmSetFlip(context,(u8)(display_buffer^1));
    if(rc){result_log("CACHED_FLIP",rc);return -1;}
    rsxFlushBuffer(context);last_present_us=now_us();frame_count++;perf.repeats++;return 0;
}
static ui_loading_clock loading_clock;
static ui_buffer_notice buffer_notice;
static int draw(u64 now)
{
    unsigned state=g_view==VIEW_PLAYER?wili_video_state():0;
    if(g_view!=VIEW_PLAYER)buffer_notice=(ui_buffer_notice){0};
    int sustained_buffer=g_view==VIEW_PLAYER && ui_buffer_notice_step(&buffer_notice,(state>>4)==5,now);
    int overlay_target=g_view==VIEW_PLAYER && (exit_confirm || g_quality_menu_open || (state&15)==3 || (state&15)==4 || sustained_buffer ||
                (g_osd_visible && ((state&15)==2 || (now>=g_last_osd_action && now-g_last_osd_action<5000000))));
    if(g_view==VIEW_PLAYER){
        float elapsed=opacity_clock && now>=opacity_clock?(float)(now-opacity_clock):0;opacity_clock=now;
        osd_opacity+=overlay_target?elapsed/140000.0f:-elapsed/200000.0f;
        if(osd_opacity<0)osd_opacity=0;
        if(osd_opacity>1)osd_opacity=1;
    }else{opacity_clock=now;osd_opacity=0;}
    int overlay=overlay_target || osd_opacity>0;
    unsigned generation=g_view==VIEW_PLAYER?wili_video_generation():0;
    wili_yuv_surface frame;int available=g_view==VIEW_PLAYER?wili_video_surface(&frame):0;
    int first_frame_wait=g_view==VIEW_PLAYER && !available && (state&15)!=3 && (state&15)!=4;
    if(ui_loading_redraw(&loading_clock,request_kind==1 || first_frame_wait,now))ui_dirty=1;
    if(available && !player_drawn)ui_dirty=1;
    if(overlay!=last_overlay)ui_dirty=1;
    if(g_view==VIEW_FEED && animation_active){animation_active=native_ui_animate(&g_feed,g_focused_card,now);ui_dirty=1;}

    int fading=g_view==VIEW_PLAYER && osd_opacity>0 && osd_opacity<1;
    // Keep flips alive for system UI, but avoid re-sampling the same YUV frame
    // and waiting on a redundant GPU fence at every 60Hz display refresh.
    if(!ui_dirty && !dm_dirty && generation==rendered_generation && !fading && !menu_open && !system_draw && !search_osk_active()){
        if(now-last_present_us<16667)return 0;
        return repeat_display();
    }
    u64 phase=now_us();if(!first_flip && wait_flip(1000))return -1;perf.flip_us+=now_us()-phase;
    // Reserve the maximum glass + overlay commands and all danmaku quads.
    if(context->end-context->current<(int)(4096+WILI_DM_LANES*64) && recycle_command_arena())return -1;
    phase=now_us();int composing=ui_dirty;
    if(g_view==VIEW_FEED){
        if(ui_dirty){
            if(request_kind==1)native_ui_loading(ui_surface,ui_background,now,request_cancel);
            else native_ui_feed(ui_surface,ui_background,&g_feed,&g_qr,g_focused_card,g_tab_focus,ui_message);
            if(quick_search)native_ui_keyboard(ui_surface,quick_text,quick_key,quick_lower);
            if(exit_confirm){native_ui_frost_exit(ui_surface);native_ui_alpha_target(1);native_ui_exit(ui_surface,exit_choice);native_ui_alpha_target(0);}
            cache_flush(ui_surface,UI_WIDTH*UI_HEIGHT*4);}
        transfer_ui(0,0,UI_WIDTH,UI_HEIGHT);
    }else if(first_frame_wait && !exit_confirm && !action_confirm){
        native_ui_loading(ui_surface,ui_background,now,0);
        cache_flush(ui_surface,UI_WIDTH*UI_HEIGHT*4);transfer_ui(0,0,UI_WIDTH,UI_HEIGHT);
    }else{
        const wili_bili_session *session=wili_bili_play_session();
        if(ui_dirty){
            memset(ui_surface,0,UI_WIDTH*UI_HEIGHT*4);
            if(detail_mode)native_ui_detail(ui_surface,&g_detail,session->title,(unsigned)detail_focus,detail_comments,detail_creator,danmaku_get_density(),wili_video_playback_time_us()/1000,wili_video_duration_ms(),state);
            else if(overlay)native_ui_player(ui_surface,session->title,g_current_qn,wili_video_playback_time_us()/1000,
                wili_video_duration_ms(),state,g_quality_menu_open,g_quality_menu_sel,known_quality_mask|session->quality_mask);
            if(comment_open>=0 && comment_open<g_detail.comment_count)native_ui_comment_view(ui_surface,g_detail.comments[comment_open].author,g_detail.comments[comment_open].message);
            if(action_confirm){
                const char *title=action_confirm==BILI_DETAIL_COMMENT?(comment_parent?"确认发布回复？":"确认发布评论？"):action_confirm==BILI_DETAIL_FOLLOW?(g_detail.following?"确认取消关注？":"确认关注UP主？"):(g_detail.liked?"确认取消点赞？":"确认点赞？");
                native_ui_confirm(ui_surface,title,action_confirm==BILI_DETAIL_COMMENT?comment_draft:action_confirm==BILI_DETAIL_FOLLOW?g_detail.up_name:session->title,action_choice);
            }
            if(exit_confirm){native_ui_alpha_target(1);native_ui_exit(ui_surface,exit_choice);native_ui_alpha_target(0);}
            cache_flush(ui_surface,UI_WIDTH*UI_HEIGHT*4);
        }

        if(detail_mode)rsx_video_set_box(DETAIL_VIDEO_X,DETAIL_VIDEO_Y,DETAIL_VIDEO_W,DETAIL_VIDEO_H);else rsx_video_set_box(0,0,1280,720);
        rsx_video_set_background(background_offset,detail_mode && background_mapped);
        rsx_video_set_glass(!detail_mode && overlay,g_quality_menu_open,exit_confirm);
        if(rsx_video_draw(context,buffer_offsets[display_buffer],pitch,available?&frame:NULL,ui_offset,detail_mode || action_confirm || comment_open>=0?1:osd_opacity,danmaku_draw_list())){
            text_log("RSX_YUV_DRAW_FAILED");return -1;
        }
    }
    if(composing){perf.compose_us+=now_us()-phase;perf.composes++;}
    volatile u32 *fence=gcmGetLabelAddress(64);
    rsxSetWaitForIdle(context);rsxSetWriteBackendLabel(context,64,++ticket);rsxFlushBuffer(context);
    u64 begin=now_us();
    while(*fence!=ticket){if(now_us()-begin>500000){text_log("RSX_BLIT_FENCE_TIMEOUT");gpu_fault=1;return -1;}sysUsleep(100);}
    perf.gpu_us+=now_us()-begin;perf.renders++;
    gcmResetFlipStatus();s32 rc=gcmSetFlip(context,(u8)display_buffer);if(rc){result_log("SET_FLIP",rc);return -1;}
    rsxFlushBuffer(context);last_present_us=now;display_buffer^=1;frame_count++;ui_dirty=0;dm_dirty=0;last_overlay=overlay;rendered_generation=generation;
    if(first_flip){text_log("FIRST_FLIP_SUBMITTED");first_flip=0;}
    if(g_view==VIEW_PLAYER && available){player_drawn=1;if(wili_video_presented())ui_dirty=1;}
    return 0;
}
static void change_tab(int delta)
{
    static int saved_card[FEED_TAB_COUNT],saved_focus[FEED_TAB_COUNT];
    bili_feed_tab_t old=g_feed.current_tab;
    saved_card[old]=g_focused_card;saved_focus[old]=g_tab_focus;
    bili_feed_load_tab(&g_feed,(bili_feed_tab_t)((old+delta+FEED_TAB_COUNT)%FEED_TAB_COUNT));
    if(old!=g_feed.current_tab && ui_sound_ready)ui_sound_click();
    g_focused_card=saved_card[g_feed.current_tab];g_tab_focus=saved_focus[g_feed.current_tab];
    if(g_feed.item_count && g_focused_card>=g_feed.item_count)g_focused_card=0;
    ui_message[0]=0;ui_dirty=1;
}
static void open_search(void)
{
    osk_purpose=0;
    if(search_osk_open(g_feed.search_keyword,text_log)){
        quick_search=1;quick_key=0;snprintf(quick_text,sizeof(quick_text),"%s",g_feed.search_keyword);ui_message[0]=0;
        text_log("SEARCH_BUILTIN_KEYBOARD_OPEN");
    }
    ui_dirty=1;
}
static void change_density(void)
{
    unsigned level=(danmaku_get_density()+1)%7;danmaku_density(level);native_ui_dm_density(level);ui_dirty=dm_dirty=1;
}
static void compose_comment(uint64_t parent)
{
    if(!g_qr.is_logged_in){snprintf(g_detail.status,sizeof(g_detail.status),"请先回账号栏目扫码登录");return;}
    if(!g_detail.ready || g_detail.busy){snprintf(g_detail.status,sizeof(g_detail.status),"详情仍在加载，请稍后编辑评论");return;}
    comment_parent=parent;osk_purpose=1;comment_draft[0]=0;
    if(search_osk_open_text("",parent?"回复评论 · 支持中文":"发表评论 · 支持中文",1,text_log)){
        osk_purpose=0;snprintf(g_detail.status,sizeof(g_detail.status),"系统输入法打开失败，请稍后重试");
    }
}
static void detail_input(u32 pressed)
{
    if(detail_creator){
        int n=g_detail.creator_count;
        if((pressed&BTN_UP) && detail_focus>0)detail_focus--;
        if((pressed&BTN_DOWN) && detail_focus+1<n)detail_focus++;
        if(pressed&(BTN_CIRCLE|BTN_LEFT)){detail_creator=0;detail_focus=1;}
        if(pressed&BTN_SELECT){detail_creator=0;detail_mode=0;}
        if(pressed&BTN_TRIANGLE){if(!bili_detail_action(BILI_DETAIL_CREATOR,NULL,0))video_page_pending=1;}
        if(pressed&BTN_R2){if(!bili_detail_action(BILI_DETAIL_CREATOR_NEXT,NULL,0))video_page_pending=1;}
        if(pressed&BTN_L2){if(!bili_detail_action(BILI_DETAIL_CREATOR_PREV,NULL,0))video_page_pending=1;}
        if((pressed&BTN_CROSS) && detail_focus<n)start_playback(g_detail.creator[detail_focus].bvid,64);
        return;
    }
    if(pressed&BTN_LEFT)detail_focus=(int)detail_navigate((unsigned)detail_focus,DETAIL_NAV_LEFT,(unsigned)g_detail.related_count,(unsigned)g_detail.comment_count);
    if(pressed&BTN_RIGHT)detail_focus=(int)detail_navigate((unsigned)detail_focus,DETAIL_NAV_RIGHT,(unsigned)g_detail.related_count,(unsigned)g_detail.comment_count);
    if(pressed&BTN_UP)detail_focus=(int)detail_navigate((unsigned)detail_focus,DETAIL_NAV_UP,(unsigned)g_detail.related_count,(unsigned)g_detail.comment_count);
    if(pressed&BTN_DOWN)detail_focus=(int)detail_navigate((unsigned)detail_focus,DETAIL_NAV_DOWN,(unsigned)g_detail.related_count,(unsigned)g_detail.comment_count);
    if(pressed&BTN_SELECT){detail_mode=0;g_osd_visible=1;g_last_osd_action=now_us();}
    if(pressed&BTN_TRIANGLE)wili_video_pause(now_us());
    if(pressed&BTN_SQUARE)change_density();
    if(pressed&BTN_L1)seek_relative(-10);
    if(pressed&BTN_R1)seek_relative(10);
    int related_focus=detail_focus>=(int)DETAIL_RELATED_FOCUS && detail_focus<(int)DETAIL_COMMENT_FOCUS;
    if(related_focus && (pressed&BTN_R2)){if(!bili_detail_action(BILI_DETAIL_RELATED_NEXT,NULL,0))video_page_pending=2;}
    if(related_focus && (pressed&BTN_L2)){if(!bili_detail_action(BILI_DETAIL_RELATED_PREV,NULL,0))video_page_pending=2;}
    if(!related_focus && (pressed&BTN_L2)){detail_comments=1;detail_focus=DETAIL_COMMENT_FOCUS;native_ui_focus_comments((unsigned)g_detail.comment_count,now_us(),0);if(!bili_detail_action(BILI_DETAIL_COMMENTS_FIRST,NULL,0))comment_page_pending=1;}
    if((pressed&BTN_R2) && detail_focus>=(int)DETAIL_COMMENT_FOCUS){if(!bili_detail_action(BILI_DETAIL_COMMENTS_NEXT,NULL,0))comment_page_pending=1;}
    if(pressed&BTN_CIRCLE){stop_playback();return;}
    if(pressed&BTN_CROSS){
        if(detail_focus==0){detail_mode=0;g_osd_visible=1;g_last_osd_action=now_us();}
        else if(detail_focus==1){if(g_detail.creator_ready || !bili_detail_action(BILI_DETAIL_CREATOR,NULL,0)){detail_creator=1;detail_focus=0;}}
        else if(detail_focus==2 || detail_focus==3){
            if(!g_qr.is_logged_in)snprintf(g_detail.status,sizeof(g_detail.status),"请先回账号栏目扫码登录");
            else{action_confirm=detail_focus==2?BILI_DETAIL_FOLLOW:BILI_DETAIL_LIKE;action_choice=0;}
        }
        else if(detail_focus==4){detail_comments=1;detail_focus=DETAIL_COMMENT_FOCUS;native_ui_focus_comments((unsigned)g_detail.comment_count,now_us(),0);}
        else if(detail_focus==5)compose_comment(0);
        else if(detail_focus==6)change_density();
        else if(detail_focus>=(int)DETAIL_COMMENT_FOCUS && detail_focus-(int)DETAIL_COMMENT_FOCUS<g_detail.comment_count)comment_open=detail_focus-DETAIL_COMMENT_FOCUS;
        else if(detail_focus>=(int)DETAIL_RELATED_FOCUS && detail_focus-(int)DETAIL_RELATED_FOCUS<g_detail.related_count)start_playback(g_detail.related[detail_focus-DETAIL_RELATED_FOCUS].bvid,64);
    }
}
static void input(void)
{
    if(menu_open || system_draw || search_osk_active() || now_us()<input_after)return;
    padInfo2 info;if(!pad_ready || ioPadGetInfo2(&info))return;
    for(unsigned i=0;i<MAX_PORT_NUM;i++){
        if(!(info.port_status[i]&1)){previous_buttons[i]=0;continue;}
        padData data;memset(&data,0,sizeof(data));if(ioPadGetData(i,&data))continue;
        u32 cur=data.len>=4?((u32)data.button[2]<<16)|data.button[3]:previous_buttons[i];
        if(data.len>=8){
            if(data.button[6]<64)cur|=BTN_LEFT;else if(data.button[6]>192)cur|=BTN_RIGHT;
            if(data.button[7]<64)cur|=BTN_UP;else if(data.button[7]>192)cur|=BTN_DOWN;
        }
        u32 pressed=cur&~previous_buttons[i],directions=cur&(BTN_LEFT|BTN_RIGHT|BTN_UP|BTN_DOWN);
        u64 input_now=now_us();
        if(directions!=(previous_buttons[i]&(BTN_LEFT|BTN_RIGHT|BTN_UP|BTN_DOWN)))direction_repeat[i]=input_now+350000;
        else if(directions && input_now>=direction_repeat[i]){pressed|=directions;direction_repeat[i]=input_now+110000;}
        previous_buttons[i]=cur;
        if(!pressed)continue;
        ui_dirty=1;
        if(exit_confirm){
            if(pressed&(BTN_LEFT|BTN_UP))exit_choice=0;
            if(pressed&(BTN_RIGHT|BTN_DOWN))exit_choice=1;
            if(pressed&BTN_CIRCLE)exit_confirm=0;
            if(pressed&BTN_CROSS){if(exit_choice){running=0;__sync_lock_test_and_set(&request_cancel,1);}exit_confirm=0;}
            continue;
        }
        if(action_confirm){
            if(pressed&(BTN_LEFT|BTN_UP))action_choice=0;
            if(pressed&(BTN_RIGHT|BTN_DOWN))action_choice=1;
            if(pressed&BTN_CIRCLE)action_confirm=0;
            if(pressed&BTN_CROSS){
                if(action_choice && bili_detail_action(action_confirm,comment_draft,comment_parent))snprintf(g_detail.status,sizeof(g_detail.status),"暂不能提交，请等待加载完成或重新登录");
                action_confirm=0;
            }
            continue;
        }
        if(comment_open>=0){
            if(pressed&BTN_CIRCLE)comment_open=-1;
            else if((pressed&(BTN_CROSS|BTN_SQUARE)) && comment_open<g_detail.comment_count){uint64_t parent=g_detail.comments[comment_open].id;comment_open=-1;compose_comment(parent);}
            continue;
        }
        if(quick_search){
            int count=(int)strlen(UI_SEARCH_KEYS);
            if((pressed&BTN_LEFT) && quick_key>0)quick_key--;
            if((pressed&BTN_RIGHT) && quick_key+1<count)quick_key++;
            if((pressed&BTN_UP) && quick_key>=10)quick_key-=10;
            if((pressed&BTN_DOWN) && quick_key+10<count)quick_key+=10;
            if(pressed&BTN_L1)quick_lower^=1;
            if(pressed&BTN_CROSS){size_t n=strlen(quick_text);if(n+1<sizeof(quick_text)){char c=UI_SEARCH_KEYS[quick_key];if(quick_lower && c>='A' && c<='Z')c+=32;quick_text[n]=c;quick_text[n+1]=0;}}
            if(pressed&BTN_SQUARE){size_t n=strlen(quick_text);if(n){n--;while(n && ((unsigned char)quick_text[n]&0xc0)==0x80)n--;quick_text[n]=0;}}
            if((pressed&(BTN_START|BTN_TRIANGLE)) && quick_text[0]){bili_feed_search(&g_feed,quick_text);g_focused_card=0;g_tab_focus=0;quick_search=0;text_log("SEARCH_BUILTIN_SUBMITTED");}
            if(pressed&BTN_CIRCLE)quick_search=0;
            continue;
        }
        if(pressed&BTN_START){ask_exit();return;}
        if(request_kind==6)continue;
        if(request_kind==1){if(pressed&BTN_CIRCLE){__sync_lock_test_and_set(&request_cancel,1);snprintf(ui_message,sizeof(ui_message),"正在取消…");}continue;}
        if(g_view==VIEW_FEED){
            if(pressed&(BTN_LEFT|BTN_L1))change_tab(-1);
            if(pressed&(BTN_RIGHT|BTN_R1))change_tab(1);
            if(pressed&BTN_UP){int old=g_focused_card,focus=g_tab_focus;if(g_focused_card>0)g_focused_card--;else g_tab_focus=1;if(ui_sound_ready && (old!=g_focused_card || focus!=g_tab_focus))ui_sound_click();}
            if(pressed&BTN_DOWN){int old=g_focused_card,focus=g_tab_focus;if(g_tab_focus)g_tab_focus=0;else if(g_focused_card+1<g_feed.item_count)g_focused_card++;if(ui_sound_ready && (old!=g_focused_card || focus!=g_tab_focus))ui_sound_click();}
            if(pressed&BTN_TRIANGLE){
                if(!net_ok || g_feed.network_error){refresh_after_network=1;refresh_network_tab=g_feed.current_tab;reconnect_network();}else bili_feed_refresh(&g_feed);
                g_focused_card=0;ui_message[0]=0;
            }
            if(pressed&BTN_SELECT){wallpaper_poll(now_us(),1,text_log);}
            if(pressed&BTN_R2){if(!bili_feed_next_page(&g_feed)){g_focused_card=0;if(ui_sound_ready)ui_sound_click();}}
            if((pressed&BTN_SQUARE) && g_feed.current_tab==FEED_TAB_SEARCH)open_search();
            if(pressed&BTN_CROSS){
                if(g_feed.current_tab==FEED_TAB_USER && !g_qr.is_logged_in){if(net_ok)launch_request(2);else reconnect_network();}
                else if(g_feed.current_tab==FEED_TAB_SEARCH && (!g_feed.item_count || g_tab_focus))open_search();
                else if(g_feed.item_count && g_focused_card<g_feed.item_count){
                    if(g_feed.items[g_focused_card].is_folder){bili_feed_open_folder(&g_feed,g_focused_card);g_focused_card=0;g_tab_focus=0;}
                    else start_playback(g_feed.items[g_focused_card].bvid,64);
                }
            }
            if((pressed&BTN_SQUARE) && g_feed.current_tab==FEED_TAB_USER && g_qr.is_logged_in)launch_request(4);
            if(pressed&BTN_CIRCLE){if(g_feed.current_tab==FEED_TAB_FAVORITE && g_feed.favorite_id){bili_feed_load_tab(&g_feed,FEED_TAB_FAVORITE);g_focused_card=0;}else if(!g_tab_focus)g_tab_focus=1;else ask_exit();}
        }else{
            g_last_osd_action=now_us();
            if(detail_mode){detail_input(pressed);continue;}
            if(pressed&BTN_SELECT){detail_mode=1;g_quality_menu_open=0;continue;}
            if(g_quality_menu_open){
                if((pressed&BTN_UP) && g_quality_menu_sel>0)g_quality_menu_sel--;
                unsigned qualities[4];unsigned count=quality_values(qualities);
                if((pressed&BTN_DOWN) && g_quality_menu_sel+1<(int)count)g_quality_menu_sel++;
                if(pressed&BTN_CIRCLE)g_quality_menu_open=0;
                if(pressed&BTN_CROSS)start_playback_at(g_active_bvid,qualities[g_quality_menu_sel],wili_video_playback_time_us()/1000,1);
            }else{
                if(pressed&BTN_CROSS){wili_video_pause(now_us());g_osd_visible=1;}
                if(pressed&BTN_TRIANGLE)g_osd_visible=osd_opacity>0.5f?0:1;
                if(pressed&(BTN_UP|BTN_DOWN)){g_quality_menu_open=1;g_quality_menu_sel=0;unsigned qs[4],n=quality_values(qs);for(unsigned j=0;j<n;j++)if(qs[j]==g_current_qn)g_quality_menu_sel=(int)j;}
                if(pressed&BTN_L1)seek_relative(-10);
                if(pressed&BTN_R1)seek_relative(10);
                if(pressed&BTN_SQUARE)change_density();
                if(pressed&BTN_CIRCLE){detail_mode=1;detail_focus=0;}
            }
        }
    }
}
static volatile u32 cleanup_done,cleanup_stage;
static void cleanup_worker(void *arg)
{
    (void)arg;
    __sync_lock_test_and_set(&cleanup_stage,1);
    if(request_kind){u64 result;sysThreadJoin(request_thread,&result);request_kind=0;}
    text_log("EXIT_STEP request_joined");
    __sync_lock_test_and_set(&cleanup_stage,2);bili_feed_shutdown(&g_feed);
    __sync_lock_test_and_set(&cleanup_stage,3);ui_image_shutdown();
    ui_image_end_session();
    text_log("EXIT_STEP background_joined");
    __sync_lock_test_and_set(&cleanup_stage,4);
    if(gpu_fault || wili_video_stop_safe()){
        text_log("CLEANUP_RETAINED live_decoder_or_worker process_teardown");goto finished;
    }
    wili_bili_session_close(wili_bili_play_session(),text_log);
    text_log("EXIT_STEP codecs_closed");
    bili_detail_shutdown();danmaku_shutdown();history_sync_shutdown(2000);text_log("EXIT_STEP detail_danmaku_history_joined");
    __sync_lock_test_and_set(&cleanup_stage,5);
    if(ui_sound_ready){int sound_rc=ui_sound_stop();result_log("UI_SOUND_STOP",sound_rc);ui_sound_ready=0;}
    __sync_lock_test_and_set(&cleanup_stage,6);wili_network_shutdown(text_log);
    __sync_lock_test_and_set(&cleanup_stage,7);
    if(video_renderer_ready){rsx_video_shutdown();video_renderer_ready=0;}
    if(ui_ready){s32 unmap=gcmUnmapIoAddress(ui_offset);result_log("UI_UNMAP",unmap);if(!unmap)free(ui_surface);}
    if(background_mapped){s32 unmap=gcmUnmapIoAddress(background_offset);result_log("WALLPAPER_RSX_UNMAP",unmap);if(!unmap)free(ui_background);}
    else free(ui_background);
    native_ui_release_player_cache();ui_font_shutdown();
    if(pad_ready)result_log("PAD_END",ioPadEnd());
finished:
    __sync_lock_test_and_set(&cleanup_done,1);sysThreadExit(0);
}
int main(void)
{
    wili_early_checkpoint(WILI_PROBE_ID " MAIN_BEFORE_OPEN_LOGS");open_logs();
    frequency=sysGetTimebaseFrequency();if(!frequency)goto done;started=now_us();
    s32 rc=sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0,callback,0);result_log("CALLBACK_INIT",rc);callback_ready=!rc;
    if(init_display() || init_ui())goto done;
    pad_init_rc=ioPadInit(MAX_PORT_NUM);pad_ready=!pad_init_rc;result_log("PAD_INIT",pad_init_rc);
    if(ui_font_init("/dev_hdd0/game/WILIPRB01/USRDIR/font.ttf")){text_log("FONT_INIT_FAILED");goto done;}
    ui_sound_ready=!ui_sound_start(text_log);
    if(!ui_sound_ready)text_log("UI_SOUND_UNAVAILABLE muted_navigation");
    const char *ca="/dev_hdd0/game/WILIPRB01/USRDIR/cacert.pem";
    wili_qr_login_init(&g_qr,ca,text_log);
    snprintf(g_feed.status_msg,sizeof(g_feed.status_msg),"正在初始化网络…");if(draw(now_us()))goto done;
    net_ok=!wili_network_prepare(text_log);next_reconnect=now_us()+5000000;
    if(net_ok){
        if(bili_detail_init(ca,text_log))text_log("DETAIL_INIT_FAILED");
        if(danmaku_init(ca,text_log))text_log("DANMAKU_INIT_FAILED");
        native_ui_dm_density(danmaku_get_density());
            if(history_sync_init(ca,text_log))text_log("HISTORY_INIT_FAILED");
        if(bili_feed_init(&g_feed,g_qr.is_logged_in?g_qr.cookie:NULL,ca,text_log))text_log("FEED_INIT_FAILED");
        if(ui_image_init(ca,text_log))text_log("IMAGE_INIT_FAILED");
        wallpaper_poll(now_us(),1,text_log);
        bili_feed_load_tab(&g_feed,FEED_TAB_RECOMMEND);
    }else snprintf(g_feed.status_msg,sizeof(g_feed.status_msg),"未连接网络，请检查 PS3 网络设置");
    ui_dirty=1;u64 last_log=0,last_osd=0;
    while(running){
        u64 now=now_us();if(callback_ready)sysUtilCheckCallback();if(!running)break;
        char query[1024];int search_result=search_osk_tick(query,sizeof(query));
        if(search_result>0){
            if(osk_purpose==1){snprintf(comment_draft,sizeof(comment_draft),"%s",query);action_confirm=BILI_DETAIL_COMMENT;action_choice=0;}
            else{bili_feed_search(&g_feed,query);g_focused_card=0;g_tab_focus=0;ui_message[0]=0;}
            osk_purpose=0;ui_dirty=1;
        }else if(search_result<0){osk_purpose=0;ui_dirty=1;}
        int system_active=menu_open || system_draw || search_osk_active() || exit_confirm || action_confirm;
        if(system_active && g_view==VIEW_PLAYER && !wili_video_is_paused() && (wili_video_state()&15)==1){wili_video_pause(now);menu_paused=1;}
        if(!system_active && menu_paused){if(wili_video_is_paused())wili_video_pause(now);menu_paused=0;}
        pump_request();
        if(request_kind!=6 && now-last_link_check>=1000000){
            if(net_ok && !wili_network_link_ready()){net_ok=0;text_log("NETWORK_LINK_LOST credentials_retained");next_reconnect=now+1000000;}
            last_link_check=now;
        }
        input();if(!running)break;
        if(request_kind!=6 && bili_feed_pump(&g_feed)){ui_dirty=1;if(g_focused_card>=g_feed.item_count)g_focused_card=0;}
        if(g_view==VIEW_FEED && !request_kind && !system_active && (!net_ok || g_feed.network_error) && now>=next_reconnect)reconnect_network();
        if(g_view==VIEW_FEED && !request_kind && !system_active)wallpaper_poll(now,0,text_log);
        if((g_view==VIEW_FEED || detail_mode) && !request_kind && !system_active){
            if(ui_image_take_wallpaper(ui_background)){text_log("WALLPAPER_APPLIED");cache_flush(ui_background,UI_WIDTH*UI_HEIGHT*4);ui_dirty=1;}
        }
        unsigned rev=ui_image_revision();if(rev!=image_revision){image_revision=rev;if(g_view==VIEW_FEED || detail_mode)ui_dirty=1;}
        if(request_kind!=6 && bili_detail_pump(&g_detail)){
            ui_dirty=1;
            if(video_page_pending && !g_detail.busy){
                if(video_page_pending==1 && detail_creator)detail_focus=0;
                else if(video_page_pending==2 && !detail_creator)detail_focus=DETAIL_RELATED_FOCUS;
                video_page_pending=0;
            }
            if(comment_page_pending && !g_detail.busy){comment_page_pending=0;detail_focus=DETAIL_COMMENT_FOCUS;native_ui_focus_comments((unsigned)g_detail.comment_count,now,1);}
            if(detail_creator && detail_focus>=g_detail.creator_count)detail_focus=0;
            else if(!detail_creator && detail_focus>=(int)DETAIL_COMMENT_FOCUS && detail_focus>=(int)DETAIL_COMMENT_FOCUS+g_detail.comment_count)detail_focus=DETAIL_COMMENT_FOCUS;
        }
        if(g_view==VIEW_PLAYER){
            u64 phase=now_us();wili_video_tick(now);if(player_drawn){
                wili_video_presented();history_sync_tick(wili_video_playback_time_us()/1000,wili_video_is_paused(),(wili_video_state()&15)==3,now);
            }
            u64 spent=now_us()-phase;perf.video_us+=spent;if(spent>perf.max_video_us)perf.max_video_us=spent;
            int allow=wili_video_buffer_ms()>=2000 || wili_video_is_paused() || (wili_video_state()&15)==3;
            bili_detail_allow_network(allow);ui_image_pause(!detail_mode || !allow);
            unsigned dm=danmaku_tick((uint32_t)(wili_video_playback_time_us()/1000),allow);
            if(dm!=dm_revision){dm_revision=dm;dm_dirty=1;}
            if(detail_mode && !system_active && (ui_dirty || now-last_scroll_animation>=33333)){
                if(native_ui_detail_animate((unsigned)detail_focus,(unsigned)g_detail.comment_count,detail_creator,now))ui_dirty=1;
                last_scroll_animation=now;
            }
            if(now-last_osd>=1000000){if(detail_mode || g_osd_visible || g_quality_menu_open)ui_dirty=1;last_osd=now;}
        }else if(net_ok && !request_kind && g_feed.current_tab==FEED_TAB_USER && !g_qr.is_logged_in &&
                 (g_qr.state==WILI_QR_NEED_SCAN || g_qr.state==WILI_QR_NEED_CONFIRM) &&
                 now-g_qr.last_poll_us>=2500000)launch_request(3);
        if(g_view==VIEW_FEED && !system_active)animation_active=native_ui_animate(&g_feed,g_focused_card,now) || animation_active;
        if(draw(now))break;
        if(now-last_log>=5000000){
            char line[192];size_t vb=0,ab=0;wili_video_get_buffer_status(&vb,&ab);
            snprintf(line,sizeof(line),"ALIVE seconds=%u draws=%u view=%d items=%d v_bytes=%u a_bytes=%u recycle=%u menu=%d draw=%u osk=%d",
                (unsigned)((now-started)/1000000),frame_count,g_view,g_feed.item_count,(unsigned)vb,(unsigned)ab,command_recycles,menu_open,system_draw,search_osk_active());
            text_log(line);last_log=now;
            snprintf(line,sizeof(line),"PERF window_ms=5000 renders=%u cached_flips=%u ui_rebuilds=%u video_ms=%u video_max_us=%u compose_ms=%u gpu_wait_ms=%u flip_wait_ms=%u",
                perf.renders,perf.repeats,perf.composes,(unsigned)(perf.video_us/1000),(unsigned)perf.max_video_us,
                (unsigned)(perf.compose_us/1000),(unsigned)(perf.gpu_us/1000),(unsigned)(perf.flip_us/1000));text_log(line);
            memset(&perf,0,sizeof(perf));
            if(g_view==VIEW_PLAYER)wili_video_log_clock();
        }
        sysUsleep(1000);
    }
done:
    text_log("CLEANUP_BEGIN");running=0;
    if(g_view==VIEW_PLAYER)history_sync_end(wili_video_playback_time_us()/1000,(wili_video_state()&15)==3);
    history_sync_close_begin(2000);
    if(search_osk_active()){
        search_osk_abort();u64 limit=now_us()+2000000;char discarded[256];
        while(search_osk_active() && now_us()<limit){sysUtilCheckCallback();search_osk_tick(discarded,sizeof(discarded));if(!gpu_fault)draw(now_us());sysUsleep(1000);}
    }
    // Cancel all producers together. Keep the last completed VRAM framebuffer
    // flipping while cleanup waits; no new draw may sample freed main memory.
    __sync_lock_test_and_set(&request_cancel,1);bili_feed_cancel();ui_image_cancel();bili_detail_cancel();danmaku_stop();
    ui_sound_set_muted(1);wili_video_cancel_workers();
    sys_ppu_thread_t cleaner;cleanup_done=cleanup_stage=0;
    int clean_rc=sysThreadCreate(&cleaner,cleanup_worker,NULL,1050,262144,THREAD_JOINABLE,"bibi-exit");
    if(!clean_rc){
        u64 begin=now_us();unsigned waits=0;
        while(!__sync_add_and_fetch(&cleanup_done,0) && now_us()-begin<3000000 && waits++<3000){
            if(callback_ready)sysUtilCheckCallback();
            if(!gpu_fault && now_us()-last_present_us>=16667 && repeat_display())gpu_fault=1;
            sysUsleep(1000);
        }
        if(cleanup_done){u64 result;sysThreadJoin(cleaner,&result);}
        char line[128];snprintf(line,sizeof(line),"EXIT_CLEANUP elapsed_ms=%u done=%u stage=%u",
            (unsigned)((now_us()-begin)/1000),cleanup_done,cleanup_stage);text_log(line);
    }else result_log("EXIT_WORKER_CREATE",clean_rc);
    if(callback_ready)result_log("CALLBACK_END",sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0));
    text_log("EXIT_REQUESTED process_syscall=3 fallback=22");runtime_log_shutdown();wili_platform_exit();
}

