// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void dummy_log(const char *msg) { (void)msg; }

int main(void)
{
    printf("Starting ui_image tests...\n");

    int rc = ui_image_init(NULL, dummy_log);
    assert(rc == 0);

    uint32_t fb[100 * 100];
    memset(fb, 0, sizeof(fb));

    // Request non-existent / new url
    int drawn = ui_image_draw(fb, 100, 100, 10, 10, 50, 50, "https://i0.hdslb.com/bfs/archive/test.jpg");
    assert(drawn == 0); // Not loaded yet

    ui_image_shutdown();
    printf("PASS ui_image tests\n");
    return 0;
}
