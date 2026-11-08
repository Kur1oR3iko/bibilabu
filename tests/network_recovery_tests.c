// SPDX-License-Identifier: GPL-3.0-or-later
#include "../platform/network_source.h"
#include <curl/curl.h>
#include <net/netctl.h>
#include <sysmodule/sysmodule.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int state,borrowed,curl_fail,net_inits,net_closes,ctl_terms,module_unloads,curl_closes;
int netInitialize(void){net_inits++;return 0;}
int netDeinitialize(void){net_closes++;return 0;}
int netCtlInit(void){return borrowed?(int)NET_CTL_ERROR_NOT_TERMINATED:0;}
void netCtlTerm(void){ctl_terms++;}
int netCtlGetState(int *out){*out=state;return 0;}
int netCtlGetInfo(int code,union net_ctl_info *out){(void)code;strcpy(out->ip_address,"127.0.0.1");return 0;}
int sysModuleLoad(int module){(void)module;return borrowed?(int)SYSMODULE_ERR_DUPLICATE:0;}
int sysModuleUnload(int module){(void)module;module_unloads++;return 0;}
int wili_probe_network_cancel(void){return 0;}
CURLcode curl_global_init(long flags){(void)flags;return curl_fail?CURLE_FAILED_INIT:CURLE_OK;}
void curl_global_cleanup(void){curl_closes++;}
curl_version_info_data *curl_version_info(CURLversion age){(void)age;static curl_version_info_data info;info.version="stub";info.ssl_version="stub";return &info;}
static void log_line(const char *s){(void)s;}
int main(void)
{
    assert(wili_network_prepare(log_line)<0);assert(net_inits==1);
    state=3;assert(!wili_network_prepare(log_line));assert(net_inits==1 && wili_network_link_ready());
    state=0;assert(!wili_network_link_ready());assert(wili_network_prepare(log_line)<0);
    state=3;assert(!wili_network_prepare(log_line));wili_network_shutdown(log_line);
    assert(net_closes==1 && ctl_terms==1 && module_unloads==1 && curl_closes==1);
    curl_fail=1;assert(wili_network_prepare(log_line)<0);curl_fail=0;assert(!wili_network_prepare(log_line));wili_network_shutdown(log_line);
    borrowed=1;int terms=ctl_terms,unloads=module_unloads;assert(!wili_network_prepare(log_line));wili_network_shutdown(log_line);
    assert(terms==ctl_terms && unloads==module_unloads);
    puts("PASS offline -> online -> disconnect -> recover, retry after CURL failure, borrowed service ownership");
}
