// SPDX-License-Identifier: GPL-3.0-or-later
#include "bili_probe.h"
#include <sys/time.h>
#include <curl/curl.h>
#include <stdio.h>
static void log_line(const char *line){puts(line);}
int main(int argc,char **argv)
{
    if(curl_global_init(CURL_GLOBAL_DEFAULT))return 1;
    const char *bv=argc>1 ? argv[1] : WILI_BILI_DEFAULT_BVID;
    int rc=wili_bili_probe_prepare(bv,log_line);
    if(!rc)rc=wili_bili_probe_run(NULL,NULL,NULL);
    unsigned i;char line[160];for(i=0;i<6;i++){wili_bili_probe_line(i,line,sizeof(line));puts(line);}
    curl_global_cleanup();return rc ? 1 : 0;
}
