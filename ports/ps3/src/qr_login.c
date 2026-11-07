#define _POSIX_C_SOURCE 200809L
// SPDX-License-Identifier: GPL-3.0-or-later
#include "qr_login.h"
#include "https_request.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#if !defined(__PPU__) && !defined(__CELLOS_LV2__)
#include <unistd.h>
#endif

#if defined(__PPU__) || defined(__CELLOS_LV2__)
#include <sys/file.h>
#endif

static const char *CONFIG_PATHS[] = {
#ifndef WILI_CONFIG_TEST_ONLY
    "/dev_hdd0/tmp/bibilabu_config.json",
    "/dev_hdd0/game/WILIPRB01/USRDIR/wiliwili_config.json",
 #endif
#if !defined(__PPU__) && !defined(__CELLOS_LV2__)
    "wiliwili_config.json",
#endif
    NULL
};

static int fs_read_all(const char *path, char **out_buf, size_t *out_sz)
{
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    int fd = -1;
    if(sysLv2FsOpen(path, SYS_O_RDONLY, &fd, 0, 0, 0) != 0) return -1;
    sysFSStat st;
    if(sysLv2FsFStat(fd, &st) != 0 || st.st_size == 0 || st.st_size > 65536) {
        sysLv2FsClose(fd);
        return -1;
    }
    char *buf = (char*)malloc((size_t)st.st_size + 1);
    if(!buf) { sysLv2FsClose(fd); return -1; }
    u64 got = 0;
    if(sysLv2FsRead(fd, buf, st.st_size, &got) != 0 || got != st.st_size) {
        free(buf); sysLv2FsClose(fd); return -1;
    }
    sysLv2FsClose(fd);
    buf[got] = 0;
    *out_buf = buf;
    *out_sz = (size_t)got;
    return 0;
#else
    FILE *f = fopen(path, "rb");
    if(!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(sz <= 0 || sz > 65536) { fclose(f); return -1; }
    char *buf = (char*)malloc(sz + 1);
    if(!buf) { fclose(f); return -1; }
    size_t n = fread(buf, 1, sz, f);
    fclose(f);
    buf[n] = 0;
    *out_buf = buf;
    *out_sz = n;
    return 0;
#endif
}

static int fs_write_all(const char *path,const char *data,size_t sz)
{
    char temp[320];snprintf(temp,sizeof(temp),"%s.tmp",path);
#if defined(__PPU__) || defined(__CELLOS_LV2__)
    int fd=-1;if(sysLv2FsOpen(temp,SYS_O_WRONLY|SYS_O_CREAT|SYS_O_TRUNC,&fd,0600,0,0))return -1;
    u64 done=0;int rc=0;
    while(done<sz){u64 amount=0;rc=sysLv2FsWrite(fd,data+done,sz-done,&amount);if(rc || !amount){rc=-1;break;}done+=amount;}
    if(!rc)rc=sysLv2FsFsync(fd);
    sysLv2FsClose(fd);
    if(rc){sysLv2FsUnlink(temp);return -1;}
    rc=sysLv2FsRename(temp,path);
    if(rc){
        char old[320];sysFSStat st;snprintf(old,sizeof(old),"%s.old",path);
        if(sysLv2FsStat(path,&st))return -1;
        sysLv2FsUnlink(old);if(sysLv2FsRename(path,old))return -1;
        rc=sysLv2FsRename(temp,path);
        if(rc)sysLv2FsRename(old,path);else sysLv2FsUnlink(old);
    }
    return rc? -1:0;
#else
    FILE *f=fopen(temp,"wb");if(!f)return -1;
    int rc=fwrite(data,1,sz,f)!=sz;
    if(fflush(f) || fsync(fileno(f)))rc=1;
    if(fclose(f))rc=1;
    if(!rc)rc=rename(temp,path);
    if(rc)unlink(temp);
    return rc?-1:0;
#endif
}

int wili_qr_login_init(wili_qr_login *qr, const char *ca, void (*log)(const char*))
{
    if(!qr) return -1;
    memset(qr, 0, sizeof(*qr));
    qr->ca = ca;
    qr->log = log;
    qr->state = WILI_QR_IDLE;
    snprintf(qr->message, sizeof(qr->message), "Not logged in");
    if(!wili_qr_login_load_config(qr))wili_qr_login_save_config(qr);
    return 0;
}

int wili_qr_login_generate(wili_qr_login *qr)
{
    if(!qr) return -1;
    qr->state = WILI_QR_GENERATING;
    snprintf(qr->message, sizeof(qr->message), "Generating QR code...");
    if(qr->log) qr->log("QR_LOGIN_GENERATE_BEGIN");

    wili_https_response r = {0};
    const char *url = "https://passport.bilibili.com/x/passport-login/web/qrcode/generate?source=main_electron_pc";
    int rc = wili_https_get(url, qr->ca, NULL, 65536, qr->cancel, qr->cancel_arg, &r, qr->log);
    if(rc || r.status != 200 || !r.body) {
        qr->state = WILI_QR_ERROR;
        snprintf(qr->message, sizeof(qr->message), "Failed to generate QR (Network error)");
        wili_https_response_free(&r);
        return -1;
    }

    struct json_object *root = json_tokener_parse(r.body);
    wili_https_response_free(&r);
    if(!root) {
        qr->state = WILI_QR_ERROR;
        snprintf(qr->message, sizeof(qr->message), "Failed to parse QR response");
        return -1;
    }

    struct json_object *data = NULL;
    if(!json_object_object_get_ex(root, "data", &data) || !data) {
        json_object_put(root);
        qr->state = WILI_QR_ERROR;
        return -1;
    }

    struct json_object *j_url = NULL, *j_key = NULL;
    json_object_object_get_ex(data, "url", &j_url);
    json_object_object_get_ex(data, "qrcode_key", &j_key);

    if(!j_url || !j_key) {
        json_object_put(root);
        qr->state = WILI_QR_ERROR;
        return -1;
    }

    const char *qr_url = json_object_get_string(j_url);
    const char *qr_key = json_object_get_string(j_key);
    snprintf(qr->url, sizeof(qr->url), "%s", qr_url ? qr_url : "");
    snprintf(qr->oauth_key, sizeof(qr->oauth_key), "%s", qr_key ? qr_key : "");
    json_object_put(root);

    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(qrcodegen_VERSION_MAX)];
    bool ok = qrcodegen_encodeText(qr->url, temp, qr->qrcode, qrcodegen_Ecc_LOW,
                                   qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                   qrcodegen_Mask_AUTO, true);
    if(!ok) {
        qr->state = WILI_QR_ERROR;
        snprintf(qr->message, sizeof(qr->message), "Failed to encode QR matrix");
        return -1;
    }

    qr->size = qrcodegen_getSize(qr->qrcode);
    qr->state = WILI_QR_NEED_SCAN;
    snprintf(qr->message, sizeof(qr->message), "Scan with Bilibili mobile app");
    if(qr->log) {
        char line[256];
        snprintf(line, sizeof(line), "QR_LOGIN_GENERATED size=%d", qr->size);
        qr->log(line);
    }
    return 0;
}

int wili_qr_login_poll(wili_qr_login *qr, uint64_t now_us)
{
    if(!qr || (qr->state != WILI_QR_NEED_SCAN && qr->state != WILI_QR_NEED_CONFIRM)) return 0;

    uint64_t interval = (qr->state == WILI_QR_NEED_CONFIRM) ? 1000000ULL : 2500000ULL;
    if(qr->last_poll_us && now_us - qr->last_poll_us < interval) return 0;
    qr->last_poll_us = now_us;

    char url[512];
    snprintf(url, sizeof(url), "https://passport.bilibili.com/x/passport-login/web/qrcode/poll?qrcode_key=%s&source=main_electron_pc", qr->oauth_key);

    wili_https_response r = {0};
    int rc = wili_https_get(url, qr->ca, NULL, 65536, qr->cancel, qr->cancel_arg, &r, qr->log);
    if(rc || r.status != 200 || !r.body) {
        wili_https_response_free(&r);
        return 0;
    }

    struct json_object *root = json_tokener_parse(r.body);
    if(!root) {
        wili_https_response_free(&r);
        return 0;
    }

    struct json_object *data = NULL;
    int poll_code = -1;
    if(json_object_object_get_ex(root, "data", &data) && data) {
        struct json_object *j_code = NULL;
        if(json_object_object_get_ex(data, "code", &j_code)) {
            poll_code = json_object_get_int(j_code);
        }
        struct json_object *j_token = NULL;
        if(json_object_object_get_ex(data, "refresh_token", &j_token)) {
            const char *rt = json_object_get_string(j_token);
            if(rt) snprintf(qr->refresh_token, sizeof(qr->refresh_token), "%s", rt);
        }
    }
    json_object_put(root);

    if(poll_code == 86101) {
        qr->state = WILI_QR_NEED_SCAN;
        snprintf(qr->message, sizeof(qr->message), "Scan with Bilibili mobile app");
    } else if(poll_code == 86090) {
        qr->state = WILI_QR_NEED_CONFIRM;
        snprintf(qr->message, sizeof(qr->message), "Scanned! Confirm login on phone");
    } else if(poll_code == 86038) {
        qr->state = WILI_QR_TIMEOUT;
        snprintf(qr->message, sizeof(qr->message), "QR expired (Press CROSS to refresh)");
    } else if(poll_code == 0) {
        qr->state = WILI_QR_SUCCESS;
        qr->is_logged_in = 1;
        snprintf(qr->message, sizeof(qr->message), "Login successful; available quality depends on video");
        if(r.set_cookie[0]) {
            snprintf(qr->cookie, sizeof(qr->cookie), "%s", r.set_cookie);
        }
        const char *p = strstr(qr->cookie, "DedeUserID=");
        if(p) {
            sscanf(p, "DedeUserID=%31[^;]", qr->dede_user_id);
        }
        if(qr->log) {
            char line[256];
            snprintf(line, sizeof(line), "QR_LOGIN_SUCCESS uid=%s cookie_len=%u", qr->dede_user_id, (unsigned)strlen(qr->cookie));
            qr->log(line);
        }
        wili_qr_login_save_config(qr);
    }
    wili_https_response_free(&r);
    return (qr->state == WILI_QR_SUCCESS) ? 1 : 0;
}

int wili_qr_login_save_config(const wili_qr_login *qr)
{
    if(!qr || !qr->is_logged_in || !qr->cookie[0]) return -1;
    struct json_object *root=json_object_new_object();if(!root)return -1;
    json_object_object_add(root,"cookie",json_object_new_string(qr->cookie));
    json_object_object_add(root,"dede_user_id",json_object_new_string(qr->dede_user_id));
    json_object_object_add(root,"refresh_token",json_object_new_string(qr->refresh_token));
    const char *content=json_object_to_json_string(root);size_t len=strlen(content);

    int saved = 0;
    for(int i=0;i<2 && CONFIG_PATHS[i];i++){
        char backup[320];snprintf(backup,sizeof(backup),"%s.bak",CONFIG_PATHS[i]);
        fs_write_all(backup,content,len);
        if(fs_write_all(CONFIG_PATHS[i], content, len) == 0) {
            saved++;
            if(qr->log) {
                char line[256];
                snprintf(line, sizeof(line), "CONFIG_SAVED path=%s", CONFIG_PATHS[i]);
                qr->log(line);
            }
        }
    }
    json_object_put(root);return saved > 0 ? 0 : -1;
}

int wili_qr_login_load_config(wili_qr_login *qr)
{
    if(!qr) return -1;
    for(int index=0;CONFIG_PATHS[index/2];index++){
        int i=index/2;char path[320];snprintf(path,sizeof(path),"%s%s",CONFIG_PATHS[i],index%2?".bak":"");
        char *buf = NULL;
        size_t sz = 0;
        if(fs_read_all(path, &buf, &sz) != 0 || !buf) continue;

        struct json_object *root = json_tokener_parse(buf);
        free(buf);
        if(!root || !json_object_is_type(root,json_type_object)){if(root)json_object_put(root);if(qr->log)qr->log("CONFIG_INVALID_JSON trying_backup");continue;}
        struct json_object *logout=NULL;
        if(json_object_object_get_ex(root,"logged_out",&logout) && json_object_get_boolean(logout)){json_object_put(root);if(qr->log)qr->log("CONFIG_LOGOUT_MARKER");return -1;}
        struct json_object *j_cookie = NULL, *j_uid = NULL, *j_token = NULL;
        json_object_object_get_ex(root, "cookie", &j_cookie);
        json_object_object_get_ex(root, "dede_user_id", &j_uid);
        json_object_object_get_ex(root, "refresh_token", &j_token);

        if(j_cookie) {
            const char *c = json_object_get_string(j_cookie);
            if(c && strstr(c,"SESSDATA=") && strlen(c)<sizeof(qr->cookie)) {
                snprintf(qr->cookie, sizeof(qr->cookie), "%s", c);
                if(j_uid) {
                    const char *u = json_object_get_string(j_uid);
                    if(u) snprintf(qr->dede_user_id, sizeof(qr->dede_user_id), "%s", u);
                }
                if(j_token) {
                    const char *t = json_object_get_string(j_token);
                    if(t) snprintf(qr->refresh_token, sizeof(qr->refresh_token), "%s", t);
                }
                qr->is_logged_in = 1;
                qr->state = WILI_QR_SUCCESS;
                snprintf(qr->message, sizeof(qr->message), "Logged in (UID: %s)", qr->dede_user_id[0] ? qr->dede_user_id : "verified");
                if(qr->log) {
                    char line[256];
                    snprintf(line, sizeof(line), "CONFIG_LOADED from=%.200s account_saved=1", path);
                    qr->log(line);
                }
                json_object_put(root);
                return 0;
            }
        }
        json_object_put(root);
    }
    return -1;
}

int wili_qr_login_logout(wili_qr_login *qr)
{
    if(!qr)return -1;
    const char *ca=qr->ca;void (*log)(const char*)=qr->log;
    int saved=0;const char marker[]="{\"logged_out\":true}";
    for(int i=0;i<2 && CONFIG_PATHS[i];i++){
        char backup[320];snprintf(backup,sizeof(backup),"%s.bak",CONFIG_PATHS[i]);fs_write_all(backup,marker,sizeof(marker)-1);
        if(!fs_write_all(CONFIG_PATHS[i],marker,sizeof(marker)-1))saved++;
    }
    memset(qr,0,sizeof(*qr));qr->ca=ca;qr->log=log;
    if(log)log(saved?"LOGOUT_CONFIG_CLEARED":"LOGOUT_CONFIG_CLEAR_FAILED");
    return saved?0:-1;
}
