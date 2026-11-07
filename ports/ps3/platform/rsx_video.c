// SPDX-License-Identifier: GPL-3.0-or-later
// Native RSX textured quads. No CPU RGB conversion, GL, Vulkan or Cg runtime.
#include "rsx_video.h"
#include "native_ui.h"
#include "rsx_shaders.h"
#include <string.h>
#include <stdio.h>
static unsigned screen_w,screen_h;
static unsigned box_x,box_y,box_w=1280,box_h=720;
static uint32_t wallpaper_offset;static int wallpaper_enabled;
void rsx_video_set_background(uint32_t offset,int enabled){wallpaper_offset=offset;wallpaper_enabled=enabled;}
void rsx_video_set_box(unsigned x,unsigned y,unsigned w,unsigned h){box_x=x;box_y=y;box_w=w;box_h=h;}
static int ready,glass_controls,glass_quality,glass_exit;
static void *glass_memory;static u32 glass_offset;
void rsx_video_set_glass(int controls,int quality,int exit_dialog)
{glass_controls=controls;glass_quality=quality;glass_exit=exit_dialog;}

static void *depth_buffer,*fragment_memory[5],*vertex_code;
static u32 depth_offset,fragment_offset[5];
static const rsxVertexProgram *vp;
static const rsxFragmentProgram *fp[5];

int rsx_video_init(unsigned width,unsigned height,void (*log)(const char*))
{
    if(ready)return 0;
    screen_w=width;screen_h=height;
    vp=(const rsxVertexProgram*)shader_quad;
    fp[0]=(const rsxFragmentProgram*)shader_yuv_601_limited;
    fp[1]=(const rsxFragmentProgram*)shader_yuv_601_full;
    fp[2]=(const rsxFragmentProgram*)shader_yuv_709_limited;
    fp[3]=(const rsxFragmentProgram*)shader_yuv_709_full;
    fp[4]=(const rsxFragmentProgram*)shader_overlay;
    if(vp->magic!=0x5650 || vp->num_insn!=3 || vp->input_mask!=0x109)return -1;
    if(vp->ucode_off>sizeof(shader_quad) || vp->num_insn*16u>sizeof(shader_quad)-vp->ucode_off)return -1;
    u32 bytes=0;rsxVertexProgramGetUCode(vp,&vertex_code,&bytes);
    if(!bytes || bytes>4096)return -1;
    glass_memory=rsxMemalign(128,80*48*4);
    if(!glass_memory || rsxAddressToOffset(glass_memory,&glass_offset))goto fail;
    depth_buffer=rsxMemalign(64,width*height*4);
    if(!depth_buffer || rsxAddressToOffset(depth_buffer,&depth_offset))goto fail;
    const size_t sizes[5]={sizeof(shader_yuv_601_limited),sizeof(shader_yuv_601_full),sizeof(shader_yuv_709_limited),sizeof(shader_yuv_709_full),sizeof(shader_overlay)};
    for(unsigned i=0;i<5;i++){
        void *code=NULL;
        if(fp[i]->magic!=0x4650 || fp[i]->num_insn!=(i==4?2:14))goto fail;
        if(fp[i]->ucode_off>sizes[i] || fp[i]->num_insn*16u>sizes[i]-fp[i]->ucode_off)goto fail;
        rsxFragmentProgramGetUCode(fp[i],&code,&bytes);
        if(!code || !bytes || bytes>4096)goto fail;
        fragment_memory[i]=rsxMemalign(64,bytes);
        if(!fragment_memory[i] || rsxAddressToOffset(fragment_memory[i],&fragment_offset[i]))goto fail;
        memcpy(fragment_memory[i],code,bytes);
    }
    __sync_synchronize();ready=1;
    if(log)log("RSX_YUV_READY native_assembly fragment_shaders=5 vertex_programs=1 color=601_709_full_limited alpha=premultiplied");
    return 0;
fail:
    rsx_video_shutdown();if(log)log("RSX_YUV_INIT_FAILED");return -1;
}
void rsx_video_shutdown(void)
{
    // Caller has drained the GPU. Keep allocations on a fence timeout instead.
    for(unsigned i=0;i<5;i++){if(fragment_memory[i])rsxFree(fragment_memory[i]);fragment_memory[i]=NULL;}
    if(glass_memory)rsxFree(glass_memory);
    glass_memory=NULL;
    if(depth_buffer)rsxFree(depth_buffer);
    depth_buffer=NULL;ready=0;
}
static void surface(gcmContextData *ctx,u32 target,u32 pitch)
{
    gcmSurface s;memset(&s,0,sizeof(s));
    s.type=GCM_SURFACE_TYPE_LINEAR;s.antiAlias=GCM_SURFACE_CENTER_1;
    s.colorFormat=GCM_SURFACE_A8R8G8B8;s.colorTarget=GCM_SURFACE_TARGET_0;
    for(unsigned i=0;i<4;i++){s.colorLocation[i]=GCM_LOCATION_RSX;s.colorPitch[i]=i?64:pitch;s.colorOffset[i]=i?0:target;}
    s.depthFormat=GCM_SURFACE_ZETA_Z24S8;s.depthLocation=GCM_LOCATION_RSX;
    s.depthOffset=depth_offset;s.depthPitch=pitch;s.width=(u16)screen_w;s.height=(u16)screen_h;
    rsxSetSurface(ctx,&s);
    float scale[4]={screen_w*0.5f,screen_h*-0.5f,0.5f,0},offset[4]={screen_w*0.5f,screen_h*0.5f,0.5f,0};
    rsxSetViewport(ctx,0,0,(u16)screen_w,(u16)screen_h,0,1,scale,offset);
    rsxSetScissor(ctx,0,0,(u16)screen_w,(u16)screen_h);
    rsxSetColorMask(ctx,GCM_COLOR_MASK_R|GCM_COLOR_MASK_G|GCM_COLOR_MASK_B|GCM_COLOR_MASK_A);
    rsxSetColorMaskMrt(ctx,0);
    rsxSetDepthTestEnable(ctx,GCM_FALSE);rsxSetDepthWriteEnable(ctx,GCM_FALSE);
    rsxSetStencilTestEnable(ctx,GCM_FALSE);rsxSetTwoSidedStencilTestEnable(ctx,GCM_FALSE);
    rsxSetAlphaTestEnable(ctx,GCM_FALSE);rsxSetCullFaceEnable(ctx,GCM_FALSE);
    rsxSetFrontPolygonMode(ctx,GCM_POLYGON_MODE_FILL);rsxSetBackPolygonMode(ctx,GCM_POLYGON_MODE_FILL);
    rsxSetShadeModel(ctx,GCM_SHADE_MODEL_SMOOTH);
    rsxSetZMinMaxControl(ctx,GCM_FALSE,GCM_TRUE,GCM_FALSE);
    rsxSetUserClipPlaneControl(ctx,GCM_USER_CLIP_PLANE_DISABLE,GCM_USER_CLIP_PLANE_DISABLE,
        GCM_USER_CLIP_PLANE_DISABLE,GCM_USER_CLIP_PLANE_DISABLE,GCM_USER_CLIP_PLANE_DISABLE,GCM_USER_CLIP_PLANE_DISABLE);
    rsxSetPointSpriteControl(ctx,GCM_FALSE,0,0);
    for(unsigned i=0;i<8;i++)rsxSetViewportClip(ctx,i,(u16)screen_w,(u16)screen_h);
    rsxLoadVertexProgram(ctx,vp,vertex_code);
}
static void texture(gcmContextData *ctx,unsigned unit,u32 offset,unsigned width,unsigned height,unsigned pitch,int argb)
{
    gcmTexture t;memset(&t,0,sizeof(t));
    t.format=(argb?GCM_TEXTURE_FORMAT_A8R8G8B8:GCM_TEXTURE_FORMAT_B8)|GCM_TEXTURE_FORMAT_LIN;
    t.mipmap=1;t.dimension=GCM_TEXTURE_DIMS_2D;t.width=(u16)width;t.height=(u16)height;t.depth=1;
    t.location=(argb==2?GCM_LOCATION_RSX:GCM_LOCATION_CELL);t.pitch=pitch;t.offset=offset;
    unsigned channels[4]={GCM_TEXTURE_REMAP_COLOR_A,GCM_TEXTURE_REMAP_COLOR_R,GCM_TEXTURE_REMAP_COLOR_G,GCM_TEXTURE_REMAP_COLOR_B};
    unsigned shifts[4]={GCM_TEXTURE_REMAP_COLOR_A_SHIFT,GCM_TEXTURE_REMAP_COLOR_R_SHIFT,GCM_TEXTURE_REMAP_COLOR_G_SHIFT,GCM_TEXTURE_REMAP_COLOR_B_SHIFT};
    unsigned types[4]={GCM_TEXTURE_REMAP_TYPE_A_SHIFT,GCM_TEXTURE_REMAP_TYPE_R_SHIFT,GCM_TEXTURE_REMAP_TYPE_G_SHIFT,GCM_TEXTURE_REMAP_TYPE_B_SHIFT};
    for(unsigned i=0;i<4;i++)t.remap|=(GCM_TEXTURE_REMAP_TYPE_REMAP<<types[i])|((argb?channels[i]:GCM_TEXTURE_REMAP_COLOR_B)<<shifts[i]);
    rsxLoadTexture(ctx,(u8)unit,&t);rsxTextureControl(ctx,(u8)unit,GCM_TRUE,0,0,GCM_TEXTURE_MAX_ANISO_1);
    rsxTextureFilter(ctx,(u8)unit,0,GCM_TEXTURE_LINEAR,GCM_TEXTURE_LINEAR,GCM_TEXTURE_CONVOLUTION_QUINCUNX);
    rsxTextureWrapMode(ctx,(u8)unit,GCM_TEXTURE_CLAMP_TO_EDGE,GCM_TEXTURE_CLAMP_TO_EDGE,GCM_TEXTURE_CLAMP_TO_EDGE,0,GCM_TEXTURE_ZFUNC_LESS,0);
}
static void quad(gcmContextData *ctx,float x,float y,float width,float height,float u0,float v0,float u1,float v1,float opacity)
{
    float xy[4][2]={{x,y},{x+width,y},{x,y+height},{x+width,y+height}};
    float uv[4][2]={{u0,v0},{u1,v0},{u0,v1},{u1,v1}},color[4]={opacity,opacity,opacity,opacity};
    rsxDrawVertexBegin(ctx,GCM_TYPE_TRIANGLE_STRIP);
    for(unsigned i=0;i<4;i++){
        float position[4]={xy[i][0]*2/screen_w-1,1-xy[i][1]*2/screen_h,0,1};
        rsxDrawVertex4f(ctx,GCM_VERTEX_ATTRIB_COLOR0,color);
        rsxDrawVertex2f(ctx,GCM_VERTEX_ATTRIB_TEX0,uv[i]);
        rsxDrawVertex4f(ctx,GCM_VERTEX_ATTRIB_POS,position);
    }
    rsxDrawVertexEnd(ctx);
}
// Downsample only while glass is visible. All pixels remain in VRAM.
static void glass_capture(gcmContextData *ctx,u32 source,u32 pitch)
{
    gcmTransferScale sc;gcmTransferSurface dst;memset(&sc,0,sizeof(sc));memset(&dst,0,sizeof(dst));
    rsxSetWaitForIdle(ctx);
    sc.conversion=GCM_TRANSFER_CONVERSION_TRUNCATE;sc.format=GCM_TRANSFER_SCALE_FORMAT_A8R8G8B8;
    sc.operation=GCM_TRANSFER_OPERATION_SRCCOPY;sc.clipW=sc.outW=80;sc.clipH=sc.outH=45;
    sc.ratioX=(s32)(((u64)screen_w<<20)/80);sc.ratioY=(s32)(((u64)screen_h<<20)/45);
    sc.inW=screen_w;sc.inH=screen_h;sc.pitch=pitch;sc.offset=source;
    sc.origin=GCM_TRANSFER_ORIGIN_CORNER;sc.interp=GCM_TRANSFER_INTERPOLATOR_LINEAR;
    dst.format=GCM_TRANSFER_SURFACE_FORMAT_A8R8G8B8;dst.pitch=320;dst.offset=glass_offset;
    rsxSetTransferScaleMode(ctx,GCM_TRANSFER_LOCAL_TO_LOCAL,GCM_TRANSFER_SURFACE);
    rsxSetTransferScaleSurface(ctx,&sc,&dst);rsxSetWaitForIdle(ctx);
    rsxInvalidateTextureCache(ctx,GCM_INVALIDATE_TEXTURE);
}
static void glass_panel(gcmContextData *ctx,int x,int y,int w,int h,float opacity)
{
    texture(ctx,0,glass_offset,80,45,320,2);
    rsxLoadFragmentProgramLocation(ctx,fp[4],fragment_offset[4],GCM_LOCATION_RSX);
    rsxSetBlendEnable(ctx,GCM_TRUE);rsxSetBlendEquation(ctx,GCM_FUNC_ADD,GCM_FUNC_ADD);
    rsxSetBlendFunc(ctx,GCM_ONE,GCM_ONE_MINUS_SRC_ALPHA,GCM_ONE,GCM_ONE_MINUS_SRC_ALPHA);
    // Nine bilinear samples form a soft low-resolution blur without CPU readback.
    rsxSetScissor(ctx,x*screen_w/1280,y*screen_h/720,w*screen_w/1280,h*screen_h/720);
    for(int i=0;i<9;i++){
        float du=(i%3-1)*0.65f/80,dv=(i/3-1)*0.65f/45;
        float a=opacity/(i+1);
        quad(ctx,0,0,screen_w,screen_h,du,dv,1+du,1+dv,a);
    }
    rsxSetScissor(ctx,0,0,screen_w,screen_h);rsxSetBlendEnable(ctx,GCM_FALSE);
}
int rsx_video_draw(gcmContextData *ctx,u32 target,u32 pitch,const wili_yuv_surface *f,u32 overlay,float opacity,const wili_dm_draw *dm)
{
    if(!ready)return -1;
    surface(ctx,target,pitch);rsxSetBlendEnable(ctx,GCM_FALSE);
    rsxSetClearColor(ctx,0xff000000);rsxClearSurface(ctx,GCM_CLEAR_R|GCM_CLEAR_G|GCM_CLEAR_B|GCM_CLEAR_A);
    rsxInvalidateTextureCache(ctx,GCM_INVALIDATE_TEXTURE);
    if(wallpaper_enabled){
        texture(ctx,0,wallpaper_offset,UI_WIDTH,UI_HEIGHT,UI_WIDTH*4,1);
        rsxLoadFragmentProgramLocation(ctx,fp[4],fragment_offset[4],GCM_LOCATION_RSX);
        quad(ctx,0,0,(float)screen_w,(float)screen_h,0,0,1,1,1);
    }
    if(f){
        const wili_yuv_layout *l=&f->layout;
        if(!l->width || !l->height || !f->visible_width || !f->visible_height || f->crop_x+f->visible_width>l->width || f->crop_y+f->visible_height>l->height)return -1;
        for(unsigned i=0;i<3;i++)texture(ctx,i,f->rsx_base+l->offset[i],i?l->width/2:l->width,i?l->height/2:l->height,l->pitch[i],0);
        unsigned mode=(f->bt709?2:0)+(f->full_range?1:0);
        rsxLoadFragmentProgramLocation(ctx,fp[mode],fragment_offset[mode],GCM_LOCATION_RSX);
        float dar=f->aspect_den?(float)f->aspect_num/f->aspect_den:(float)f->visible_width/f->visible_height;
        if(dar<0.05f || dar>20.0f)dar=(float)f->visible_width/f->visible_height;
        float bw=(float)box_w*screen_w/1280,bh=(float)box_h*screen_h/720;
        float dw=bw,dh=dw/dar;if(dh>bh){dh=bh;dw=dh*dar;}
        quad(ctx,(float)box_x*screen_w/1280+(bw-dw)/2,(float)box_y*screen_h/720+(bh-dh)/2,dw,dh,(float)f->crop_x/l->width,(float)f->crop_y/l->height,
             (float)(f->crop_x+f->visible_width)/l->width,(float)(f->crop_y+f->visible_height)/l->height,1);
    }
    if((glass_controls || glass_quality || glass_exit) && opacity>0){
        glass_capture(ctx,target,pitch);
        if(glass_controls){glass_panel(ctx,44,32,1192,88,opacity);glass_panel(ctx,44,548,1192,132,opacity);}
        if(glass_quality)glass_panel(ctx,420,220,440,266,opacity);
        if(glass_exit)glass_panel(ctx,400,250,480,220,1);
    }
    if(dm && dm->count){
        texture(ctx,0,dm->atlas_offset,WILI_DM_ATLAS_WIDTH,WILI_DM_ATLAS_HEIGHT,WILI_DM_ATLAS_WIDTH*4,1);
        rsxLoadFragmentProgramLocation(ctx,fp[4],fragment_offset[4],GCM_LOCATION_RSX);
        rsxSetBlendEnable(ctx,GCM_TRUE);rsxSetBlendEquation(ctx,GCM_FUNC_ADD,GCM_FUNC_ADD);
        rsxSetBlendFunc(ctx,GCM_ONE,GCM_ONE_MINUS_SRC_ALPHA,GCM_ONE,GCM_ONE_MINUS_SRC_ALPHA);
        rsxSetScissor(ctx,(u16)(box_x*screen_w/1280),(u16)(box_y*screen_h/720),(u16)(box_w*screen_w/1280),(u16)(box_h*screen_h/720));
        for(unsigned i=0;i<dm->count && i<WILI_DM_LANES;i++){
            const wili_dm_quad *q=&dm->quads[i];
            quad(ctx,(box_x+q->x*box_w/1280.0f)*screen_w/1280.0f,(box_y+q->y*box_h/720.0f)*screen_h/720.0f,
                q->w*box_w*screen_w/(1280.0f*1280),q->h*box_h*screen_h/(720.0f*720),q->u0,q->v0,q->u1,q->v1,1);
        }
        rsxSetScissor(ctx,0,0,(u16)screen_w,(u16)screen_h);rsxSetBlendEnable(ctx,GCM_FALSE);
    }
    if(opacity>0){
        if(opacity>1)opacity=1;
        texture(ctx,0,overlay,UI_WIDTH,UI_HEIGHT,UI_WIDTH*4,1);
        rsxLoadFragmentProgramLocation(ctx,fp[4],fragment_offset[4],GCM_LOCATION_RSX);
        rsxSetBlendEnable(ctx,GCM_TRUE);
        rsxSetBlendEquation(ctx,GCM_FUNC_ADD,GCM_FUNC_ADD);
        rsxSetBlendFunc(ctx,GCM_ONE,GCM_ONE_MINUS_SRC_ALPHA,GCM_ONE,GCM_ONE_MINUS_SRC_ALPHA);
        quad(ctx,0,0,(float)screen_w,(float)screen_h,0,0,1,1,opacity);
        rsxSetBlendEnable(ctx,GCM_FALSE);
    }
    return 0;
}
