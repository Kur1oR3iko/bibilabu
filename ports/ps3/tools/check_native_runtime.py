#!/usr/bin/env python3
"""Deterministic regressions for the native UI's worker ownership boundaries."""
from pathlib import Path
import subprocess
import shlex
PORT=Path(__file__).resolve().parents[1]
BUILD=PORT/'build/native-checks'
BUILD.mkdir(exist_ok=True)
def run(args):
    subprocess.run([str(x) for x in args],cwd=PORT,check=True)
flags=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
       '-fsanitize=address,undefined','-Iinclude']
jsonflags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','json-c'],text=True))
for name,sources,libs in [
    ('user_settings',['src/user_settings.c','src/vdec_ex_spec.c','tests/user_settings_tests.c'],[]),
    ('media_memory',['platform/media_memory.c','tests/media_memory_tests.c'],['-Iplatform','-Itests/memory_stub']),
    ('yuv_frame',['src/yuv_frame.c','tests/yuv_frame_tests.c'],['-lm']),
    ('network_recovery',['platform/network_source.c','tests/network_recovery_tests.c'],['-Itests/network_stub','-ffunction-sections','-Wl,--gc-sections']),
    ('ui_motion',['src/ui_motion.c','tests/ui_motion_tests.c'],['-lm']),
    ('image_async',['src/ui_image.c','tests/ui_image_async_tests.c'],['-pthread','-lm']),
    ('feed_mailbox',['src/bili_feed.c','src/search_text.c','tests/feed_mailbox_tests.c'],jsonflags+['-pthread']),
    ('detail_layout',['src/detail_layout.c','tests/detail_layout_tests.c'],['-lm']),
    ('native_ui_preview',['src/native_ui.c','src/detail_layout.c','src/ui_motion.c','src/ui_core.c','src/ui_font.c',
                          'third_party/qrcodegen/qrcodegen.c','tests/native_ui_preview.c'],['-lm'])]:
    run(flags+sources+libs+['-o',BUILD/name]);run([BUILD/name])
