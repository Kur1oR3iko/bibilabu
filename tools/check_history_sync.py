#!/usr/bin/env python3
from pathlib import Path
import subprocess,shlex
P=Path(__file__).resolve().parents[1];B=P/'build/native-checks';B.mkdir(exist_ok=True)
libs=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','json-c'],text=True))
cmd=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-Iinclude','src/history_sync.c','src/bili_detail.c','src/bili_api.c','src/search_text.c','tests/history_sync_tests.c',*libs,'-lcrypto','-pthread','-Wl,--gc-sections','-o',str(B/'history_sync_tests')]
subprocess.run(cmd,cwd=P,check=True)
subprocess.run([B/'history_sync_tests'],cwd=P,check=True)
