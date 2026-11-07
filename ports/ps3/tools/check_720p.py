#!/usr/bin/env python3
"""Generate a local 720p fixture, run the actual demuxer, decode its AUs on host.
This verifies media handling, not PS3 decoding performance.
"""
from pathlib import Path
import subprocess
PORT=Path(__file__).resolve().parents[1]
B=PORT/'build/native-checks'
B.mkdir(exist_ok=True)
def run(args):subprocess.run([str(x) for x in args],cwd=PORT,check=True)
run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','lavfi','-i',
     'testsrc2=size=1280x720:rate=30:duration=3','-an','-c:v','libx264','-threads','2',
     '-preset','fast','-profile:v','high','-level:v','3.1','-pix_fmt','yuv420p','-g','30','-bf','3',
     '-color_primaries','bt709','-color_trc','bt709','-colorspace','bt709',
     '-movflags','+frag_keyframe+empty_moov+default_base_moof+global_sidx+skip_trailer',B/'synthetic-720.mp4'])
run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
     '-Iinclude','src/avc_sps.c','src/mp4_dash.c','tests/hd_stream_tests.c','-o',B/'hd_stream_tests'])
run([B/'hd_stream_tests',B/'synthetic-720.mp4',B/'normalized-720.h264'])
run(['ffmpeg','-hide_banner','-loglevel','error','-xerror','-threads','2','-i',B/'normalized-720.h264','-f','null','-'])
print('PASS normalized 720p AVC decodes on host; PS3 performance still requires hardware')

# Bilibili's observed 720p streams declare Level 5.1, rejected by CellVdec query.
# Change the declaration only, then exercise the actual gated SPS normalization.
data=bytearray((B/'synthetic-720.mp4').read_bytes())
i=data.index(b'avcC')+4
assert data[i]==1 and data[i+8]&31==7
data[i+3]=51
data[i+11]=51
(B/'overdeclared-720.mp4').write_bytes(data)
run([B/'hd_stream_tests',B/'overdeclared-720.mp4',B/'normalized-51.h264','51'])
run(['ffmpeg','-hide_banner','-loglevel','error','-xerror','-threads','2','-i',B/'normalized-51.h264','-f','null','-'])
print('PASS over-declared 720p Level 5.1 -> gated 4.1 normalization and host decode')

run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','lavfi','-i','sine=frequency=440:sample_rate=44100:duration=3',
     '-ac','2','-c:a','aac','-b:a','128k','-frag_duration','1000000',
     '-movflags','+empty_moov+default_base_moof+global_sidx+skip_trailer',B/'seek-audio.mp4'])
run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
     '-Iinclude','src/avc_sps.c','src/mp4_dash.c','tests/seek_tests.c','-o',B/'seek_tests'])
run([B/'seek_tests',B/'synthetic-720.mp4',B/'seek-audio.mp4',B/'seek-video.h264',B/'seek-audio.aac'])
for path in [B/'seek-video.h264',B/'seek-audio.aac']:
    run(['ffmpeg','-hide_banner','-loglevel','error','-xerror','-i',path,'-f','null','-'])
print('PASS indexed seek extraction and decode of AVC/AAC preroll streams')
