#!/usr/bin/env python3
from pathlib import Path
import subprocess
P=Path(__file__).resolve().parents[1]
out=P/'build/native-checks/compressed_ring_tests'
out.parent.mkdir(exist_ok=True)
subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
    '-Iinclude','src/compressed_ring.c','tests/compressed_ring_tests.c','-pthread','-o',str(out)],cwd=P,check=True)
subprocess.run([str(out)],cwd=P,check=True)
