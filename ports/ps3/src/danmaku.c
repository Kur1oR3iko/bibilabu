// SPDX-License-Identifier: GPL-3.0-or-later
#include "danmaku.h"
#include "search_text.h"
#include <string.h>
#include <stdlib.h>
typedef struct {const uint8_t *at,*end;} pb;
static int varint(pb *p,uint64_t *v)
{
    *v=0;for(unsigned i=0;i<10;i++){if(p->at==p->end)return -1;unsigned b=*p->at++;
        if(i==9 && b>1)return -1;
        *v|=(uint64_t)(b&127)<<(i*7);if(!(b&128))return 0;}return -1;
}
static int field(pb *p,unsigned *tag,unsigned *wire,uint64_t *value,pb *slice)
{
    uint64_t key=0;if(varint(p,&key) || !key || key>>3>0x1fffffff)return -1;
    *tag=(unsigned)(key>>3);*wire=key&7;*value=0;slice->at=slice->end=NULL;
    if(*wire==0)return varint(p,value);
    uint64_t n=*wire==1?8:*wire==5?4:0;
    if(*wire==2){if(varint(p,&n))return -1;}else if(*wire!=1 && *wire!=5)return -1;
    if(n>(uint64_t)(p->end-p->at))return -1;
    slice->at=p->at;slice->end=p->at+(size_t)n;p->at=slice->end;return 0;
}
static int element(pb p,wili_dm_event *e)
{
    memset(e,0,sizeof(*e));e->mode=1;e->color=0xffffff;e->size=24;
    while(p.at<p.end){unsigned tag,wire;uint64_t v;pb s;if(field(&p,&tag,&wire,&v,&s))return -1;
        if(tag==2 && wire==0){if(v>86400000)return -1;e->ms=(uint32_t)v;}
        else if(tag==3 && wire==0){if(v>UINT32_MAX)return 1;e->mode=(uint32_t)v;}
        else if(tag==4 && wire==0)e->size=v<12?12:v>32?32:(uint32_t)v;
        else if(tag==5 && wire==0)e->color=(uint32_t)v&0xffffff;
        else if(tag==7 && wire==2){size_t n=(size_t)(s.end-s.at);if(n>=sizeof(e->text)){n=sizeof(e->text)-1;e->truncated=1;}
            if(n<(size_t)(s.end-s.at))while(n && (s.at[n]&0xc0)==0x80)n--;
            memcpy(e->text,s.at,n);e->text[n]=0;for(size_t i=0;i<n;i++)if((unsigned char)e->text[i]<32)e->text[i]=' ';
        }
    }
    uint16_t utf16[512];
    return e->text[0] && wili_utf8_to_utf16(e->text,utf16,512)>=0 && e->mode>=1 && e->mode<=6?0:1;
}
static int by_time(const void *a,const void *b)
{
    const wili_dm_event *x=a,*y=b;
    if(x->ms!=y->ms)return x->ms<y->ms?-1:1;
    return x->order<y->order?-1:x->order>y->order;
}
void wili_dm_segment_free(wili_dm_segment *s)
{if(s){free(s->events);memset(s,0,sizeof(*s));}}
int wili_dm_segment_copy(wili_dm_segment *dst,const wili_dm_segment *src)
{
    wili_dm_event *events=NULL;
    if(src->count){events=malloc(src->count*sizeof(*events));if(!events)return -1;memcpy(events,src->events,src->count*sizeof(*events));}
    wili_dm_segment_free(dst);*dst=*src;dst->events=events;return 0;
}
int wili_dm_parse(const void *data,size_t bytes,unsigned segment,wili_dm_segment *out)
{
    if(!data || !out || !segment || segment>240 || bytes>WILI_DM_RESPONSE_BYTES)return -1;
    wili_dm_segment result={0};result.index=segment;
    uint32_t start=(segment-1)*360000;unsigned capacity=0,order=0;
    pb p={data,(const uint8_t*)data+bytes};
    while(p.at<p.end){unsigned tag,wire;uint64_t v;pb slice;if(field(&p,&tag,&wire,&v,&slice))goto fail;
        if(tag!=1 || wire!=2)continue;
        wili_dm_event e;int rc=element(slice,&e);if(rc<0)goto fail;
        if(e.ms<start || e.ms>=start+360000)continue;
        if(rc){result.unsupported++;continue;}
        if(result.count==WILI_DM_EVENTS){result.overflow++;continue;}
        if(result.count==capacity){
            unsigned next=capacity?capacity*2:64;if(next>WILI_DM_EVENTS)next=WILI_DM_EVENTS;
            void *grown=realloc(result.events,next*sizeof(e));if(!grown)goto fail;
            result.events=grown;capacity=next;
        }
        result.truncated+=e.truncated;e.order=order++;result.events[result.count++]=e;
    }
    if(result.count)qsort(result.events,result.count,sizeof(result.events[0]),by_time);
    wili_dm_segment_free(out);*out=result;return 0;
fail:
    wili_dm_segment_free(&result);return -1;
}
unsigned wili_dm_limit(unsigned density)
{static const unsigned limits[]={0,2,4,8,16,32,WILI_DM_LANES};return limits[density>6?6:density];}
static unsigned lifetime(const wili_dm_event *e)
{return e->mode==4 || e->mode==5?4000:7000;}
void wili_dm_step(wili_dm_player *p,const wili_dm_segment *s,uint32_t ms,unsigned density)
{
    if(!p)return;
    if(density>6)density=6;
    int seek=p->initialized && (ms<p->last_ms || ms-p->last_ms>1000);
    unsigned index=s?s->index:0;
    if(p->initialized && ms==p->last_ms && density==p->density && index==p->segment)return;
    if(seek || !density){memset(p->lanes,0,sizeof(p->lanes));p->last_spawn=0;p->spawn_ready=0;}
    if(!p->initialized || seek || index!=p->segment){
        p->cursor=0;p->segment=index;
        if(s)while(p->cursor<s->count && s->events[p->cursor].ms+lifetime(&s->events[p->cursor])<=ms)p->cursor++;
    }
    p->last_ms=ms;p->initialized=1;p->density=density;unsigned active=0,limit=wili_dm_limit(density);
    for(unsigned i=0;i<WILI_DM_LANES;i++){
        if(p->lanes[i].serial && (ms>=p->lanes[i].end || active>=limit))p->lanes[i].serial=0;
        if(p->lanes[i].serial)active++;
    }
    if(!density || !s)return;
    static const unsigned gaps[]={0,1200,600,300,150,60,0};
    unsigned spawned=0,budget=density==6?8:4;
    while(p->cursor<s->count && s->events[p->cursor].ms<=ms){
        const wili_dm_event *e=&s->events[p->cursor];
        if(e->ms+lifetime(e)<=ms){p->cursor++;p->expired++;continue;}
        // Full mode queues bursts without the old per-half-second sampling.
        if(density==6 && (active>=limit || spawned>=budget))break;
        p->cursor++;
        if(density<6 && (active>=limit || (p->spawn_ready && ms-p->last_spawn<gaps[density]))){p->filtered++;continue;}
        for(unsigned i=0;i<WILI_DM_LANES;i++)if(!p->lanes[i].serial){
            p->lanes[i].event=*e;p->serial++;if(!p->serial)p->serial++;
            p->lanes[i].serial=p->serial;p->lanes[i].end=e->ms+lifetime(e);
            p->lanes[i].width=512;p->last_spawn=ms;p->spawn_ready=1;active++;spawned++;break;
        }
        if(spawned>=budget)break;
    }
}
void wili_dm_geometry(const wili_dm_player *p,uint32_t ms,wili_dm_draw *out)
{
    out->count=0;for(unsigned i=0;i<WILI_DM_LANES;i++){
        const wili_dm_lane *l=&p->lanes[i];if(!l->serial || ms<l->event.ms || ms>=l->end)continue;
        float w=(float)l->width,x=(1280-w)/2,y=18+(i%13)*50;
        float progress=(float)(ms-l->event.ms)/7000.0f;
        if(l->event.mode<=3)x=1280-(1280+w)*progress;
        else if(l->event.mode==6)x=-w+(1280+w)*progress;
        else if(l->event.mode==4)y=660-(i%8)*42;
        else y=18+(i%8)*42;
        unsigned col=i%2,row=i/2;
        out->quads[out->count++]=(wili_dm_quad){x,y,w,40,col/2.0f,row/32.0f,(col*1024+w)/2048.0f,(row+1)/32.0f};
    }
}
