// SPDX-License-Identifier: GPL-3.0-or-later
// Regression for the 1.0 hardware failure: the CDN first-range probe returned
// a perfectly valid `206 bytes 0-4095/5644323`, but the guard used
// `!r.range_start` and rejected it. range_start==0 is the expected case.
#include "bili_session.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static wili_https_response real_video_probe(void)
{
    wili_https_response r;memset(&r,0,sizeof(r));
    r.tls_verified=1;r.status=206;r.has_range=1;r.curl_code=0;r.overflow=0;
    r.range_start=0;r.range_end=4095;r.range_total=5644323;r.bytes=4096;
    return r;
}
int main(void)
{
    wili_https_response r=real_video_probe();
    assert(wili_bili_cdn_probe_ok(&r,0)==1);      // exact 1.0 hardware response
    wili_https_response a=real_video_probe();
    a.range_end=4095;a.range_total=1265842;assert(wili_bili_cdn_probe_ok(&a,0)==1);
    assert(wili_bili_cdn_probe_ok(NULL,0)==0);
#define BAD(member,value) do { wili_https_response x=real_video_probe(); x.member=value; assert(wili_bili_cdn_probe_ok(&x,0)==0); } while(0)
    assert(wili_bili_cdn_probe_ok(&r,-1)==0);
    BAD(status,200);BAD(status,403);BAD(status,412);BAD(status,0);
    BAD(tls_verified,0);BAD(has_range,0);BAD(overflow,1);
    BAD(range_start,1);BAD(range_total,0);BAD(range_end,5644323);
    BAD(bytes,4095);BAD(bytes,0);
#undef BAD
    puts("PASS CDN first-range probe guard: 0-based 206 accepted; curl/TLS/status/range/length/overflow rejected");
    return 0;
}
