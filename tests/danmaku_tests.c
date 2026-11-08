#include "danmaku.h"
#include "danmaku_retry.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t vi(unsigned char *p,uint64_t v){size_t n=0;do{p[n]=(v&127)|(v>127?128:0);v>>=7;n++;}while(v);return n;}
static size_t event(unsigned char *dst,unsigned ms,unsigned mode,const char *text)
{
    unsigned char b[1024];size_t n=0;b[n++]=16;n+=vi(b+n,ms);b[n++]=24;n+=vi(b+n,mode);b[n++]=42;n+=vi(b+n,0); // unknown empty length-delimited field
    b[n++]=58;n+=vi(b+n,strlen(text));memcpy(b+n,text,strlen(text));n+=strlen(text);
    dst[0]=10;size_t prefix=1+vi(dst+1,n);memcpy(dst+prefix,b,n);return prefix+n;
}
int main(int argc,char **argv)
{
    wili_dm_retry retry={0};
    assert(wili_dm_retry_due(&retry,1,1,0));
    assert(wili_dm_retry_fail(&retry,1,1,0,0)==5);
    assert(!wili_dm_retry_due(&retry,1,1,4999999) && wili_dm_retry_due(&retry,1,1,5000000));
    assert(wili_dm_retry_due(&retry,2,1,1) && wili_dm_retry_due(&retry,1,2,1));
    assert(wili_dm_retry_fail(&retry,1,1,5000000,500)==10);
    assert(wili_dm_retry_fail(&retry,1,1,15000000,500)==20);
    assert(wili_dm_retry_fail(&retry,1,1,35000000,500)==40);
    assert(wili_dm_retry_fail(&retry,1,1,75000000,500)==60);
    assert(wili_dm_retry_fail(&retry,1,1,135000000,500)==60);
    assert(wili_dm_retry_fail(&retry,2,1,0,412)==60);
    memset(&retry,0,sizeof(retry));assert(wili_dm_retry_due(&retry,2,1,0));
    puts("PASS retry backoff/cap, HTTP 412 cooldown, generation and segment isolation");
    unsigned char data[8192];size_t n=0;
    for(unsigned i=0;i<20;i++)n+=event(data+n,i*600,i%5+1,"中文弹幕 test");
    n+=event(data+n,90000,7,"advanced ignored");
    wili_dm_segment *s=calloc(1,sizeof(*s));assert(s);assert(!wili_dm_parse(data,n,1,s) && s->count==20);
    for(unsigned level=0;level<=6;level++){
        wili_dm_player player={0};wili_dm_draw a={0},b={0};
        for(unsigned t=0;t<15000;t+=100){wili_dm_step(&player,s,t,level);wili_dm_geometry(&player,t,&a);assert(a.count<=wili_dm_limit(level));}
        wili_dm_step(&player,s,1200,level);wili_dm_geometry(&player,1200,&a);wili_dm_step(&player,s,1200,level);wili_dm_geometry(&player,1200,&b);
        assert(a.count==b.count && !memcmp(a.quads,b.quads,a.count*sizeof(a.quads[0])));
        wili_dm_step(&player,s,1300,0);wili_dm_geometry(&player,1300,&a);assert(!a.count);
    }
    assert(s->unsupported==1);
    wili_dm_segment copied={0};assert(!wili_dm_segment_copy(&copied,s));assert(copied.events!=s->events && copied.count==s->count);wili_dm_segment_free(&copied);
    // All same-timestamp standard types survive parsing and full-mode bursts.
    n=0;for(unsigned i=0;i<48;i++)n+=event(data+n,1000,i%6+1,"同一时刻完整显示");
    assert(!wili_dm_parse(data,n,1,s) && s->count==48);
    wili_dm_player full={0};wili_dm_draw geo={0};
    for(unsigned t=1000;t<1200;t+=20)wili_dm_step(&full,s,t,6);
    wili_dm_geometry(&full,1200,&geo);assert(geo.count==48 && full.cursor==48 && !full.filtered && !full.expired);
    unsigned types=0;for(unsigned i=0;i<48;i++){types|=1u<<full.lanes[i].event.mode;assert(geo.quads[i].u0>=0 && geo.quads[i].u1<=1 && geo.quads[i].v1<=1);}
    assert(types==126);
    wili_dm_geometry(&full,1300,&geo);float reverse=geo.quads[5].x,forward=geo.quads[0].x;
    wili_dm_geometry(&full,1400,&geo);assert(geo.quads[5].x>reverse && geo.quads[0].x<forward);
    wili_dm_player frozen=full;wili_dm_step(&full,s,1180,6);assert(!memcmp(&full,&frozen,sizeof(full)));
    // The old parser kept at most one event per half second; retain 2,000 now.
    unsigned char *many=malloc(1024*1024);assert(many);size_t many_n=0;
    for(unsigned i=0;i<2000;i++)many_n+=event(many+many_n,1000,i%6+1,"dense");
    assert(!wili_dm_parse(many,many_n,1,s) && s->count==2000 && !s->overflow);
    memset(&full,0,sizeof(full));for(unsigned t=1000;t<1500;t+=20)wili_dm_step(&full,s,t,6);
    assert(full.cursor==WILI_DM_LANES && !full.filtered && !full.expired); // bounded queue, no frame stalls
    free(many);
    for(size_t i=0;i<n;i++)(void)wili_dm_parse(data,i,1,s);
    unsigned char bad[]={10,255,255,255,255,255,255,255,255,255,127};assert(wili_dm_parse(bad,sizeof(bad),1,s)<0);
    unsigned seed=1;for(unsigned i=0;i<2000;i++){for(unsigned j=0;j<128;j++){seed=seed*1664525+1013904223;data[j]=seed>>24;}(void)wili_dm_parse(data,i%128,1,s);}
    if(argc==2){FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long bytes=ftell(f);assert(bytes>=0 && bytes<=1024*1024);rewind(f);
        void *body=malloc((size_t)bytes+1);assert(body);assert(fread(body,1,(size_t)bytes,f)==(size_t)bytes);fclose(f);
        assert(!wili_dm_parse(body,(size_t)bytes,1,s));printf("PASS live public protobuf segment: %u bounded events\n",s->count);free(body);}
    wili_dm_segment_free(s);free(s);puts("PASS protobuf bounds/UTF-8, same-time events retained, four standard types, density 0..6, bounded bursts, reverse motion, pause identity and seek reset");
}
