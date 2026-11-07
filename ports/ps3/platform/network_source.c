// SPDX-License-Identifier: GPL-3.0-or-later
#include "network_source.h"
#include <sys/time.h>
#include <curl/curl.h>
#include <net/net.h>
#include <net/netctl.h>
#include <sysmodule/sysmodule.h>
#include <sys/file.h>
#include <sys/systime.h>
#include <stdio.h>
#include <string.h>
extern int wili_probe_network_cancel(void);
static struct {int attempted,ready,net,ctl,ctl_owned,module_owned,curl,config_attempted,config_ready;unsigned sources;char video[2048],audio[2048];} n;
static void rc_log(void (*log)(const char*),const char *what,int rc)
{
    char line[128];snprintf(line,sizeof(line),"%s rc=0x%08x",what,(unsigned)rc);log(line);
}
static int cancel(void *arg){(void)arg;return wili_probe_network_cancel();}
static int config(void (*log)(const char*))
{
    char path[]="/dev_usb000/WILI_NET.INI",data[4096];int fd=-1;unsigned port;
    for(port=0;port<8;port++){path[10]=(char)('0'+port);if(!sysLv2FsOpen(path,SYS_O_RDONLY,&fd,0,0,0))break;}
    if(fd<0){log("NETWORK_CONFIG_MISSING WILI_NET.INI");return -1;}
    sysFSStat stat;int bad=sysLv2FsFStat(fd,&stat) || !stat.st_size || stat.st_size>=sizeof(data);
    uint64_t got=0;
    if(!bad)bad=sysLv2FsRead(fd,data,stat.st_size,&got) || got!=stat.st_size;
    sysLv2FsClose(fd);if(bad){log("NETWORK_CONFIG_READ_FAILED");return -1;}data[got]=0;
    char *line=data;
    while(*line){
        char *end=strchr(line,'\n');if(end)*end=0;
        size_t length=strlen(line);while(length && (line[length-1]=='\r' || line[length-1]==' ' || line[length-1]=='\t'))line[--length]=0;
        char *value=NULL,*target=NULL;
        if(!strncmp(line,"video=",6)){value=line+6;target=n.video;}
        else if(!strncmp(line,"audio=",6)){value=line+6;target=n.audio;}
        else if(*line && *line!='#'){log("NETWORK_CONFIG_UNKNOWN_KEY");return -1;}
        if(value){if(!*value || strlen(value)>=2048 || *target){log("NETWORK_CONFIG_INVALID_URL_OR_DUPLICATE");return -1;}strcpy(target,value);}
        if(!end)break;
        line=end+1;
    }
    if(!n.video[0] || !n.audio[0]){log("NETWORK_CONFIG_URLS_MISSING");return -1;}
    return 0;
}
static int initialize(void (*log)(const char*))
{
    int rc;
    if(!n.net){log("NET_INITIALIZE_BEGIN");rc=netInitialize();rc_log(log,"NET_INITIALIZE",rc);if(rc)return -1;n.net=1;}
    if(!n.ctl){
        rc=sysModuleLoad(SYSMODULE_NETCTL);rc_log(log,"NETCTL_MODULE_LOAD",rc);
        if(rc && (unsigned)rc!=SYSMODULE_ERR_DUPLICATE)return -1;
        n.module_owned=!rc;
        rc=netCtlInit();rc_log(log,"NETCTL_INIT",rc);
        if(rc && (unsigned)rc!=NET_CTL_ERROR_NOT_TERMINATED)return -1;
        n.ctl_owned=!rc;n.ctl=1;
    }
    int state=0;rc=netCtlGetState(&state);
    char line[192];snprintf(line,sizeof(line),"NETCTL_STATE state=%d rc=%08x",state,(unsigned)rc);log(line);
    if(rc || state!=NET_CTL_STATE_IPObtained){n.ready=0;log("NETWORK_OFFLINE retry_allowed credentials_retained");return -1;}
    if(n.curl){n.ready=1;return 0;}
    union net_ctl_info info;memset(&info,0,sizeof(info));
    if(!netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS,&info)){snprintf(line,sizeof(line),"NETWORK_IP address=%s",info.ip_address);log(line);}
    rc=(int)curl_global_init(CURL_GLOBAL_DEFAULT);rc_log(log,"CURL_GLOBAL_INIT",rc);if(rc)return -1;n.curl=1;
    curl_version_info_data *version=curl_version_info(CURLVERSION_NOW);
    snprintf(line,sizeof(line),"NETWORK_CLIENT curl=%s tls=%s cache_per_track=%u",version->version,version->ssl_version ? version->ssl_version : "none",WILI_HTTP_CACHE);log(line);
    n.ready=1;return 0;
}
int wili_network_prepare(void (*log)(const char*)){return initialize(log);}
int wili_network_open(wili_http_range *range,const char *name,uint64_t size,void (*log)(const char*))
{
    if(initialize(log))return -1;
    if(!n.config_attempted){n.config_attempted=1;n.config_ready=!config(log);}
    if(!n.config_ready)return -1;
    const char *url=!strcmp(name,"WILI_VIDEO.MP4") ? n.video : !strcmp(name,"WILI_AUDIO.MP4") ? n.audio : NULL;
    if(!url){log("NETWORK_UNEXPECTED_MEDIA_NAME");return -1;}
    if(wili_http_range_open(range,url,size,"/dev_hdd0/game/WILIPRB01/USRDIR/cacert.pem",cancel,NULL,log)){log("HTTP_SOURCE_OPEN_FAILED");return -1;}
    n.sources++;log("HTTP_SOURCE_OPEN cache=262144 timeout_ms=6000 strict_206=1 TLS_verify=1");return 0;
}
void wili_network_close(wili_http_range *range)
{
    if(range->easy){
        char line[160];snprintf(line,sizeof(line),"HTTP_SOURCE_SUMMARY size=%llu requests=%llu downloaded=%llu cache=%u",(unsigned long long)range->size,(unsigned long long)range->requests,(unsigned long long)range->downloaded,WILI_HTTP_CACHE);range->log(line);
        wili_http_range_close(range);if(n.sources)n.sources--;
    }
}
void wili_network_shutdown(void (*log)(const char*))
{
    if(n.sources){log("NETWORK_SOURCES_RETAINED until_process_teardown");return;}
    if(n.curl){curl_global_cleanup();n.curl=0;}
    if(n.ctl && n.ctl_owned){netCtlTerm();log("NETCTL_TERM_CALLED");}
    n.ctl=0;
    if(n.module_owned)rc_log(log,"NETCTL_MODULE_UNLOAD",sysModuleUnload(SYSMODULE_NETCTL));
    if(n.net){rc_log(log,"NET_DEINITIALIZE",netDeinitialize());n.net=0;}
    memset(&n,0,sizeof(n));log("NETWORK_SHUTDOWN_DONE");
}

int wili_network_link_ready(void){int state=0;return n.ctl && !netCtlGetState(&state) && state==NET_CTL_STATE_IPObtained;}
