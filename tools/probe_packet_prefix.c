// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit local diagnostic: credentials are read only into memory, never logged.
#include "bili_session.h"
#include <json-c/json.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void log_line(const char *s){if(strstr(s,"FAILED")||strstr(s,"HTTP_STOP")||strstr(s,"VIDEO_CONFIG"))puts(s);}
int main(int argc,char **argv)
{
    if(argc!=5)return 2;
    FILE *config=!strcmp(argv[1],"-")?stdin:fopen(argv[1],"rb");if(!config)return 3;
    char json[65537];size_t n=fread(json,1,sizeof(json)-1,config);fclose(config);json[n]=0;
    struct json_object *root=json_tokener_parse(json),*value=NULL;
    if(!root || !json_object_object_get_ex(root,"cookie",&value))return 4;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    wili_bili_session *s=calloc(1,sizeof(*s));unsigned char *packet=malloc(WILI_DASH_PACKET_CAP);
    int rc=wili_bili_session_open_ex(s,argv[2],(unsigned)strtoul(argv[3],NULL,10),json_object_get_string(value),NULL,NULL,NULL,log_line);
    json_object_put(root);memset(json,0,sizeof(json));
    if(rc){wili_bili_session_close(s,log_line);free(packet);free(s);return 5;}
    unsigned level;if(wili_mp4_dash_prepare_ps3(&s->video,&level))return 6;
    FILE *out=fopen(argv[4],"wb");if(!out)return 7;
    for(unsigned i=0;i<72;i++){
        size_t bytes=0;wili_dash_sample sample;
        rc=wili_mp4_dash_packet(&s->video,packet,WILI_DASH_PACKET_CAP,&bytes,&sample);if(rc<=0)break;
        unsigned sps=0,pps=0,idr=0;
        for(size_t j=0;j+5<bytes;j++)if(!packet[j]&&!packet[j+1]&&!packet[j+2]&&packet[j+3]==1){unsigned t=packet[j+4]&31;sps+=t==7;pps+=t==8;idr+=t==5;}
        if(i<12)printf("AU %u flags=%08x dts=%llu pts=%llu bytes=%zu sps=%u pps=%u idr=%u\n",i,sample.flags,(unsigned long long)sample.dts,(unsigned long long)sample.pts,bytes,sps,pps,idr);
        if(fwrite(packet,1,bytes,out)!=bytes)return 8;
    }
    fclose(out);wili_bili_session_close(s,log_line);free(packet);free(s);curl_global_cleanup();return rc<0?9:0;
}
