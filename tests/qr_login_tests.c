#define _DEFAULT_SOURCE
// SPDX-License-Identifier: GPL-3.0-or-later
#include "qr_login.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    wili_qr_login qr;
    memset(&qr, 0, sizeof(qr));
    
    char tmpdir[]="/tmp/bibi-qr-test-XXXXXX";
    assert(mkdtemp(tmpdir));assert(chdir(tmpdir)==0);
    // 1. Test QR generation with text
    const char *test_url = "https://passport.bilibili.com/h5-app/passport/login/scan?navhide=1&qrcode_key=testkey123";
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(qrcodegen_VERSION_MAX)];
    bool ok = qrcodegen_encodeText(test_url, temp, qr.qrcode, qrcodegen_Ecc_LOW,
                                   qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                   qrcodegen_Mask_AUTO, true);
    assert(ok);
    qr.size = qrcodegen_getSize(qr.qrcode);
    assert(qr.size >= 21 && qr.size <= 177);
    // Position detection patterns must have black corners
    assert(qrcodegen_getModule(qr.qrcode, 0, 0) == true);
    assert(qrcodegen_getModule(qr.qrcode, qr.size - 1, 0) == true);
    assert(qrcodegen_getModule(qr.qrcode, 0, qr.size - 1) == true);

    // 2. Test config saving and loading
    const char *test_cfg = "test_wiliwili_config.json";
    FILE *f = fopen(test_cfg, "wb");
    assert(f);
    const char *json_data = "{\n  \"cookie\": \"SESSDATA=abcdef123456; DedeUserID=12345678;\",\n  \"dede_user_id\": \"12345678\",\n  \"refresh_token\": \"rt_98765\"\n}\n";
    fwrite(json_data, 1, strlen(json_data), f);
    fclose(f);

    // Save current working dir config for load test
    rename(test_cfg, "wiliwili_config.json");
    int rc = wili_qr_login_load_config(&qr);
    unlink("wiliwili_config.json");

    assert(rc == 0);
    assert(qr.is_logged_in == 1);
    assert(qr.state == WILI_QR_SUCCESS);
    assert(strstr(qr.cookie, "SESSDATA=abcdef123456") != NULL);
    assert(strcmp(qr.dede_user_id, "12345678") == 0);
    assert(strcmp(qr.refresh_token, "rt_98765") == 0);

    strcpy(qr.cookie,"SESSDATA=fixture; note=\"quoted\"; path=back\\slash");
    assert(!wili_qr_login_save_config(&qr));
    wili_qr_login restored={0};assert(!wili_qr_login_load_config(&restored));assert(!strcmp(qr.cookie,restored.cookie));
    f=fopen("wiliwili_config.json","wb");assert(f);fputs("{broken",f);fclose(f);
    memset(&restored,0,sizeof(restored));assert(!wili_qr_login_load_config(&restored));assert(restored.is_logged_in);
    assert(!wili_qr_login_logout(&restored));
    memset(&restored,0,sizeof(restored));assert(wili_qr_login_load_config(&restored)<0 && !restored.is_logged_in);
    unlink("wiliwili_config.json");unlink("wiliwili_config.json.bak");
    puts("PASS escaped credential roundtrip, corrupt-primary backup recovery and persistent logout tombstone");
    assert(chdir("/")==0);assert(rmdir(tmpdir)==0);
    puts("PASS QR generation matrix and config persistence tests");
    return 0;
}
