#!/usr/bin/env python3
from pathlib import Path
import shlex,subprocess
P=Path(__file__).resolve().parents[1];B=P/'build/native-checks';B.mkdir(exist_ok=True)
def run(a):subprocess.run([str(x) for x in a],cwd=P,check=True)
flags=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Iinclude']
run(flags+['src/danmaku.c','src/search_text.c','tests/danmaku_tests.c','-o',B/'danmaku_tests'])
run([B/'danmaku_tests'])
json=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','json-c'],text=True))
run(flags+['src/bili_detail.c','src/bili_api.c','src/search_text.c','tests/bili_detail_tests.c',*json,'-lcrypto','-pthread','-o',B/'bili_detail_tests'])
run([B/'bili_detail_tests'])
