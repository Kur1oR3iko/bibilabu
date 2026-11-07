#!/usr/bin/env python3
"""Run the actual C Range client and MP4 demuxer against a local HTTP server."""
from pathlib import Path
import shlex
import subprocess
import threading
from media_range_server import create_server
PORT=Path(__file__).resolve().parents[1]
def main():
    build=PORT/'build/network';build.mkdir(parents=True,exist_ok=True)
    flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','libcurl'],text=True))
    test=build/'http_range_test'
    subprocess.run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations',
                    '-fsanitize=address,undefined','-I'+str(PORT/'include'),
                    str(PORT/'src/http_range.c'),str(PORT/'src/fmp4_stream.c'),str(PORT/'src/avc_test_stream.c'),
                    str(PORT/'tests/http_range_tests.c'),*flags,'-o',str(test)],check=True)
    media=PORT/'build/media';server=create_server(media,'127.0.0.1',0,test_mode=True)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:subprocess.run([str(test),f'http://127.0.0.1:{server.server_port}',str(media/'WILI_VIDEO.MP4'),str(media/'WILI_AUDIO.MP4')],check=True)
    finally:server.shutdown();server.server_close();thread.join()
if __name__=='__main__':main()
