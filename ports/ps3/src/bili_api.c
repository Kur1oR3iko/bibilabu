// SPDX-License-Identifier: GPL-3.0-or-later
// WBI permutation and request conventions follow this repository's util/wbi.cpp.
#include "bili_api.h"
#include "video_geometry.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __lv2ppu__
#include <mbedtls/md5.h>
#else
#include <openssl/evp.h>
#endif
static char *json_ascii(const char *body,size_t n,size_t *length)
{
    // SDK json-c and newer host json-c differ on signed UTF-8 bytes in strict
    // strings. Validate UTF-8 and express raw Unicode as JSON escapes first;
    // keep strict grammar checks enabled on both implementations.
    char *out=malloc(n*3+1);if(!out)return NULL;
    static const char hex[]="0123456789abcdef";size_t i=0,at=0;int quoted=0,escaped=0;
    while(i<n){
        unsigned c=(unsigned char)body[i++];
        if(c<128){
            if(c<32 && (quoted || (c!='\t' && c!='\n' && c!='\r')))goto bad;
            out[at++]=(char)c;
            if(quoted){if(escaped)escaped=0;else if(c=='\\')escaped=1;else if(c=='"')quoted=0;}
            else if(c=='"')quoted=1;
            continue;
        }
        if(!quoted || escaped)goto bad;
        unsigned more,cp,min;
        if(c>=0xc2 && c<=0xdf){more=1;cp=c&31;min=0x80;}
        else if(c>=0xe0 && c<=0xef){more=2;cp=c&15;min=0x800;}
        else if(c>=0xf0 && c<=0xf4){more=3;cp=c&7;min=0x10000;}else goto bad;
        if(more>n-i)goto bad;
        while(more--){unsigned b=(unsigned char)body[i++];if((b&0xc0)!=0x80)goto bad;cp=(cp<<6)|(b&63);}
        if(cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))goto bad;
        unsigned words[2],count=1;
        if(cp<0x10000)words[0]=cp;
        else {cp-=0x10000;words[0]=0xd800+(cp>>10);words[1]=0xdc00+(cp&1023);count=2;}
        unsigned j;for(j=0;j<count;j++){unsigned v=words[j];out[at++]='\\';out[at++]='u';out[at++]=hex[v>>12];out[at++]=hex[(v>>8)&15];out[at++]=hex[(v>>4)&15];out[at++]=hex[v&15];}
    }
    out[at]=0;*length=at;return out;
bad:free(out);return NULL;
}
static struct json_object *parse(const char *body,size_t n)
{
    if(!body || !n || n>2u*1024u*1024u)return NULL;
    size_t normalized_size=0;char *normalized=json_ascii(body,n,&normalized_size);if(!normalized)return NULL;
    struct json_tokener *tok=json_tokener_new_ex(32);if(!tok){free(normalized);return NULL;}
    json_tokener_set_flags(tok,JSON_TOKENER_STRICT);
    struct json_object *root=json_tokener_parse_ex(tok,normalized,(int)normalized_size);
    int bad=json_tokener_get_error(tok)!=json_tokener_success || !json_object_is_type(root,json_type_object);
#if JSON_C_VERSION_NUM >= 0x000f00
    size_t end=json_tokener_get_parse_end(tok);
#else
    size_t end=(size_t)tok->char_offset;
#endif
    while(end<normalized_size && (normalized[end]==' ' || normalized[end]=='\t' || normalized[end]=='\n' || normalized[end]=='\r'))end++;
    if(end!=normalized_size)bad=1;
    json_tokener_free(tok);free(normalized);if(bad){if(root)json_object_put(root);return NULL;}return root;
}
static struct json_object *get(struct json_object *obj,const char *key,enum json_type type)
{
    struct json_object *value=NULL;
    return json_object_is_type(obj,json_type_object) && json_object_object_get_ex(obj,key,&value) && json_object_is_type(value,type) ? value : NULL;
}
static int64_t number(struct json_object *obj,const char *key)
{
    struct json_object *v=get(obj,key,json_type_int);return v ? json_object_get_int64(v) : -1;
}
static const char *string(struct json_object *obj,const char *key,size_t max)
{
    struct json_object *v=get(obj,key,json_type_string);if(!v)return NULL;
    const char *s=json_object_get_string(v);int n=json_object_get_string_len(v);
    if(n<0 || (size_t)n>=max || strlen(s)!=(size_t)n)return NULL;
    return s;
}
static int code(struct json_object *root,int *out)
{
    int64_t c=number(root,"code");if(!get(root,"code",json_type_int) || c< -1000000 || c>1000000)return -1;*out=(int)c;return 0;
}
int wili_bili_valid_bvid(const char *s)
{
    if(!s || strlen(s)!=12 || s[0]!='B' || s[1]!='V')return 0;
    unsigned i;for(i=2;i<12;i++)if(!((s[i]>='0' && s[i]<='9') || (s[i]>='A' && s[i]<='Z') || (s[i]>='a' && s[i]<='z')))return 0;
    return 1;
}
static int hex32(const char *s)
{
    if(!s || strlen(s)!=32)return 0;
    unsigned i;for(i=0;i<32;i++)if(!((s[i]>='0' && s[i]<='9') || (s[i]>='a' && s[i]<='f')))return 0;return 1;
}
int wili_bili_mixin(const char *img,const char *sub,char out[33])
{
    static const unsigned map[32]={46,47,18,2,53,8,23,32,15,50,10,31,58,3,45,35,27,43,5,49,33,9,42,19,29,28,14,39,12,38,41,13};
    if(!out || !hex32(img) || !hex32(sub))return -1;
    char raw[65];memcpy(raw,img,32);memcpy(raw+32,sub,32);raw[64]=0;
    unsigned i;for(i=0;i<32;i++)out[i]=raw[map[i]];out[32]=0;return 0;
}
static int extract(const char *url,char out[33])
{
    if(!url)return -1;
    const char *slash=strrchr(url,'/'),*dot=strrchr(url,'.');
    if(!slash || !dot || dot-slash!=33)return -1;
    memcpy(out,slash+1,32);out[32]=0;return hex32(out) ? 0 : -1;
}
int wili_bili_parse_nav(const char *body,size_t n,wili_bili_nav *out)
{
    if(!out)return -1;
    memset(out,0,sizeof(*out));struct json_object *root=parse(body,n);if(!root)return -1;
    int bad=code(root,&out->code);struct json_object *data=get(root,"data",json_type_object),*images=get(data,"wbi_img",json_type_object);
    char img[33],sub[33];
    if(!bad && images && !extract(string(images,"img_url",2048),img) && !extract(string(images,"sub_url",2048),sub) && !wili_bili_mixin(img,sub,out->mixin))out->keys_ready=1;
    json_object_put(root);return bad ? -1 : 0;
}
int wili_bili_parse_video(const char *body,size_t n,wili_bili_video *out)
{
    if(!out)return -1;
    memset(out,0,sizeof(*out));struct json_object *root=parse(body,n);if(!root)return -1;
    int bad=code(root,&out->code);
    if(!bad && !out->code){
        struct json_object *data=get(root,"data",json_type_object);const char *bv=string(data,"bvid",sizeof(out->bvid));
        int64_t aid=number(data,"aid"),cid=number(data,"cid"),duration=number(data,"duration");
        struct json_object *pages=get(data,"pages",json_type_array);
        if(cid<=0 && pages && json_object_array_length(pages))cid=number(json_object_array_get_idx(pages,0),"cid");
        if(!wili_bili_valid_bvid(bv) || aid<=0 || cid<=0 || duration<0 || duration>86400000)bad=1;
        else {
            out->aid=(uint64_t)aid;out->cid=(uint64_t)cid;out->duration=(unsigned)duration;strcpy(out->bvid,bv);
            // view.data.duration is the whole multi-part video; record the
            // selected CID's own duration separately (handoff 0.9 warning).
            if(pages){
                size_t count=json_object_array_length(pages),i;
                for(i=0;i<count && i<256;i++){
                    struct json_object *page=json_object_array_get_idx(pages,i);
                    int64_t page_cid=number(page,"cid"),page_len=number(page,"duration");
                    if(page_cid==cid && page_len>0 && page_len<=86400000){out->page_duration=(unsigned)page_len;break;}
                }
            }
            const char *title=string(data,"title",65536);
            if(title){size_t len=strlen(title),copy=len<sizeof(out->title)-1 ? len : sizeof(out->title)-1;
                if(copy<len)while(copy && ((unsigned char)title[copy]&0xc0)==0x80)copy--;
                size_t i;for(i=0;i<copy;i++)out->title[i]=(unsigned char)title[i]<32 ? ' ' : title[i];out->title[copy]=0;}
        }
    }
    json_object_put(root);return bad ? -1 : 0;
}
static int safe_media(const char *s)
{
    if(!s || strncmp(s,"https://",8) || !s[8])return 0;
    const char *p;for(p=s;*p;p++)if((unsigned char)*p<=32 || (unsigned char)*p>=127 || *p=='#')return 0;
    for(p=s+8;*p && *p!='/';p++)if(*p=='@')return 0;
    return 1;
}
static int u32(const char **p,uint32_t *out)
{
    uint32_t n=0;unsigned digits=0;
    while(**p>='0' && **p<='9'){unsigned d=(unsigned)(*(*p)++-'0');if(++digits>10 || n>(UINT32_MAX-d)/10)return -1;n=n*10+d;}
    if(!digits)return -1;
    *out=n;return 0;
}
static unsigned fps_value(struct json_object *video,unsigned fallback)
{
    const char *s=string(video,"frame_rate",64);if(!s)s=string(video,"frameRate",64);if(!s)return fallback;
    uint32_t n=0,d=1;if(u32(&s,&n))return 0;
    if(*s=='/'){s++;if(u32(&s,&d) || !d)return 0;}
    else if(*s=='.'){unsigned digits=0;s++;while(*s>='0' && *s<='9' && digits<6){unsigned digit=(unsigned)(*s++-'0');if(n>(UINT32_MAX-digit)/10)return 0;n=n*10+digit;d*=10;digits++;}if(!digits)return 0;}
    if(*s || !d)return 0;
    return n/d+(n%d!=0);
}
static int avc_metadata(const char *codec)
{
    if(!codec)return 1;
    if(strlen(codec)!=11 || strncmp(codec,"avc1.",5))return 0;
    unsigned i,profile=0;
    for(i=5;i<11;i++){
        unsigned char c=(unsigned char)codec[i];unsigned d;
        if(c>='0' && c<='9')d=c-'0';else if(c>='a' && c<='f')d=c-'a'+10;else if(c>='A' && c<='F')d=c-'A'+10;else return 0;
        if(i<7)profile=profile*16+d;
    }
    return profile==66 || profile==77 || profile==88 || profile==100;
}

int wili_bili_parse_play_limit(const char *body,size_t n,unsigned max_qn,wili_bili_play *out)
{
    if(!out)return -1;
    memset(out,0,sizeof(*out));struct json_object *root=parse(body,n);if(!root)return -1;
    int bad=code(root,&out->code);
    if(!bad && !out->code){
        struct json_object *data=get(root,"data",json_type_object),*dash=get(data,"dash",json_type_object);
        int64_t quality=number(data,"quality");if(quality>=0 && quality<=1000)out->returned_quality=(unsigned)quality;
        struct json_object *dash_duration=get(dash,"duration",json_type_double),*dash_duration_int=get(dash,"duration",json_type_int);
        double seconds=dash_duration ? json_object_get_double(dash_duration) : dash_duration_int ? (double)json_object_get_int64(dash_duration_int) : 0;
        if(seconds>0 && seconds<=86400)out->dash_duration_ms=(unsigned)(seconds*1000);
        struct json_object *aq=get(data,"accept_quality",json_type_array);
        if(aq){
            unsigned len=(unsigned)json_object_array_length(aq);if(len>8)len=8;
            out->accept_count=len;
            for(unsigned i=0;i<len;i++)out->accept_qualities[i]=(unsigned)json_object_get_int(json_object_array_get_idx(aq,i));
        }
        struct json_object *ad=get(data,"accept_description",json_type_array);
        if(ad){
            unsigned len=(unsigned)json_object_array_length(ad);if(len>8)len=8;
            for(unsigned i=0;i<len;i++){
                const char *desc=json_object_get_string(json_object_array_get_idx(ad,i));
                if(desc)snprintf(out->accept_descriptions[i],sizeof(out->accept_descriptions[i]),"%s",desc);
            }
        }
        struct json_object *video=get(dash,"video",json_type_array),*audio=get(dash,"audio",json_type_array);
        uint64_t pixels=0,bitrate=UINT64_MAX;unsigned i;
        if(video)for(i=0;i<(unsigned)json_object_array_length(video);i++){
            struct json_object *v=json_object_array_get_idx(video,i);int64_t w=number(v,"width"),h=number(v,"height"),bw=number(v,"bandwidth"),id=number(v,"id");
            const char *url=string(v,"baseUrl",sizeof(out->video_url));if(!url)url=string(v,"base_url",sizeof(out->video_url));
            const char *codec=string(v,"codecs",128);int64_t codecid=number(v,"codecid");
            out->video_entries++;
            if(codecid==12)out->hevc_entries++;
            else if(codecid==7)out->avc_entries++;
            else out->other_codec_entries++;
            // Quality is an orientation-independent envelope. Frame rate is
            // descriptive metadata, never a reason to hide a 720p stream.
            if(w<=0 || h<=0 || w>1280 || h>1280 || !wili_video_is_720((unsigned)w,(unsigned)h) || (id!=16 && id!=32 && id!=64 && id!=74)){out->rejected_geometry++;continue;}
            if(bw<=0){out->rejected_bitrate++;continue;}
            if(codecid!=7 || !safe_media(url))continue;
            if(!avc_metadata(codec)){out->rejected_profile++;continue;}
            unsigned bit=id==16?1u:id==32?2u:(id==64 || id==74)?4u:0u;
            unsigned shorter=w<h?(unsigned)w:(unsigned)h,longer=w>h?(unsigned)w:(unsigned)h;
            if(!bit || ((id==64 || id==74) && shorter<=480 && longer<=854))continue;
            out->available_mask|=bit;
            if((id==74?64:id)>max_qn)continue;
            uint64_t area=(uint64_t)w*(uint64_t)h;
            unsigned rate=fps_value(v,0);
            if(area>pixels || (area==pixels && (rate>out->advertised_fps || (rate==out->advertised_fps && (uint64_t)bw<bitrate)))){pixels=area;bitrate=(uint64_t)bw;out->width=(unsigned)w;out->height=(unsigned)h;out->video_id=(unsigned)id;out->advertised_fps=fps_value(v,0);strcpy(out->video_url,url);
                if(codec)strncpy(out->video_codecs,codec,sizeof(out->video_codecs)-1);}
        }
        int64_t best=0;
        if(audio)for(i=0;i<(unsigned)json_object_array_length(audio);i++){
            struct json_object *a=json_object_array_get_idx(audio,i);int64_t id=number(a,"id"),bw=number(a,"bandwidth");
            const char *url=string(a,"baseUrl",sizeof(out->audio_url));if(!url)url=string(a,"base_url",sizeof(out->audio_url));
            const char *codec=string(a,"codecs",128);
            if((id!=30216 && id!=30232 && id!=30280) || bw<=0 || bw>256000 || (codec && strcmp(codec,"mp4a.40.2")) || !safe_media(url))continue;
            if(bw>best){best=bw;out->audio_id=(unsigned)id;strcpy(out->audio_url,url);
                if(codec)strncpy(out->audio_codecs,codec,sizeof(out->audio_codecs)-1);}
        }
        out->dash_ready=out->video_url[0] && out->audio_url[0];
        if(out->video_url[0])out->returned_quality=out->video_id==74?64:out->video_id;
    }
    json_object_put(root);return bad ? -1 : 0;
}
int wili_bili_parse_play(const char *body,size_t n,wili_bili_play *out)
{
    return wili_bili_parse_play_limit(body,n,32,out);
}
int wili_bili_play_query_qn(const char *bv,uint64_t cid,uint64_t epoch,const char mixin[33],unsigned qn,char *out,size_t cap)
{
    if(!out || !wili_bili_valid_bvid(bv) || !cid || !hex32(mixin) || epoch<1609459200 || epoch>4102444800ULL)return -1;
    char query[512],signed_data[546];int len=snprintf(query,sizeof(query),"bvid=%s&cid=%llu&fnval=16&fnver=0&fourk=0&qn=%u&web_location=1315873&wts=%llu",bv,(unsigned long long)cid,qn,(unsigned long long)epoch);
    if(len<=0 || (size_t)len>=sizeof(query))return -1;
    memcpy(signed_data,query,(size_t)len);memcpy(signed_data+len,mixin,32);unsigned char digest[16];
#ifdef __lv2ppu__
    if(mbedtls_md5_ret((const unsigned char*)signed_data,(size_t)len+32,digest))return -1;
#else
    unsigned digest_size=0;if(!EVP_Digest(signed_data,(size_t)len+32,digest,&digest_size,EVP_md5(),NULL) || digest_size!=16)return -1;
#endif
    static const char hex[]="0123456789abcdef";char rid[33];unsigned i;for(i=0;i<16;i++){rid[i*2]=hex[digest[i]>>4];rid[i*2+1]=hex[digest[i]&15];}rid[32]=0;
    int result=snprintf(out,cap,"%s&w_rid=%s",query,rid);return result>=0 && (size_t)result<cap ? 0 : -1;
}

int wili_bili_play_query(const char *bv,uint64_t cid,uint64_t epoch,const char mixin[33],char *out,size_t cap)
{
    return wili_bili_play_query_qn(bv,cid,epoch,mixin,32,out,cap);
}

struct json_object *wili_bili_json_parse(const char *body,size_t bytes){return parse(body,bytes);}
int wili_bili_sign_sorted(const char *query,const char mixin[33],char *out,size_t cap)
{
    if(!query || !hex32(mixin) || !out || strlen(query)>1024)return -1;
    char data[1057];size_t n=strlen(query);memcpy(data,query,n);memcpy(data+n,mixin,32);unsigned char digest[16];
#ifdef __lv2ppu__
    if(mbedtls_md5_ret((const unsigned char*)data,n+32,digest))return -1;
#else
    unsigned bytes=0;if(!EVP_Digest(data,n+32,digest,&bytes,EVP_md5(),NULL) || bytes!=16)return -1;
#endif
    char rid[33];static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<16;i++){rid[i*2]=hex[digest[i]>>4];rid[i*2+1]=hex[digest[i]&15];}rid[32]=0;
    int result=snprintf(out,cap,"%s&w_rid=%s",query,rid);return result>=0 && (size_t)result<cap?0:-1;
}
