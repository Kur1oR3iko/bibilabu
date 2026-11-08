// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../third_party/qrcodegen/qrcodegen.h"

typedef enum {
    WILI_QR_IDLE = 0,
    WILI_QR_GENERATING = 1,
    WILI_QR_NEED_SCAN = 2,
    WILI_QR_NEED_CONFIRM = 3,
    WILI_QR_SUCCESS = 4,
    WILI_QR_TIMEOUT = 5,
    WILI_QR_ERROR = 6
} wili_qr_state;

typedef struct {
    wili_qr_state state;
    char oauth_key[128];
    char url[512];
    char message[128];
    uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(qrcodegen_VERSION_MAX)];
    int size;
    char cookie[1024];
    char refresh_token[128];
    char dede_user_id[32];
    int is_logged_in;
    uint64_t last_poll_us;
    int (*cancel)(void *);
    void *cancel_arg;
    const char *ca;
    void (*log)(const char*);
} wili_qr_login;

int wili_qr_login_init(wili_qr_login *qr, const char *ca, void (*log)(const char*));
int wili_qr_login_generate(wili_qr_login *qr);
int wili_qr_login_poll(wili_qr_login *qr, uint64_t now_us);
int wili_qr_login_save_config(const wili_qr_login *qr);
int wili_qr_login_load_config(wili_qr_login *qr);

int wili_qr_login_logout(wili_qr_login *qr);
