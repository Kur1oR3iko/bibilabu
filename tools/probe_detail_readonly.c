#define _DEFAULT_SOURCE
#include "bili_detail.h"
#include <curl/curl.h>
#include <stdio.h>
#include <unistd.h>
static void log_line(const char *s){puts(s);}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    curl_global_init(CURL_GLOBAL_DEFAULT);if(bili_detail_init(NULL,log_line))return 1;
    bili_detail_state view={0};bili_detail_open(argv[1],NULL);bili_detail_allow_network(1);
    for(unsigned i=0;i<6000;i++){bili_detail_pump(&view);if(view.ready && !view.busy)break;usleep(10000);}
    printf("READONLY_DETAIL ready=%d related=%d comments=%d mid=%llu\n",view.ready,view.related_count,view.comment_count,(unsigned long long)view.mid);
    if(view.ready && !bili_detail_action(BILI_DETAIL_CREATOR,NULL,0)){
        for(unsigned i=0;i<6000;i++){bili_detail_pump(&view);if(!view.busy)break;usleep(10000);}
        printf("READONLY_CREATOR ready=%d count=%d\n",view.creator_ready,view.creator_count);
    }
    bili_detail_cancel();bili_detail_shutdown();curl_global_cleanup();return view.ready?0:1;
}
