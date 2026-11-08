#!/usr/bin/env python3
"""Exercise actual MP4 sample timing, SPS parsing and normalized host decoding."""
from pathlib import Path
import subprocess
PORT=Path(__file__).resolve().parents[1]
B=PORT/'build/native-checks'
B.mkdir(exist_ok=True)
def run(args): subprocess.run([str(x) for x in args],cwd=PORT,check=True)
run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
     '-Iinclude','src/avc_sps.c','src/mp4_dash.c','tests/hd_1080_tests.c','-o',B/'hd_1080_tests'])
for label,rate,refs,level,mode in [('24','24000/1001',4,'4.1','accept'),
    ('30','30',4,'4.1','accept'),('60','60',4,'4.2','reject-rate'),('refs5','30',5,'5.1','reject-dpb')]:
    path=B/f'1080-{label}.mp4'
    run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','lavfi','-i',
         f'testsrc2=size=1920x1080:rate={rate}:duration=1','-an','-c:v','libx264','-threads','2',
         '-preset','veryfast','-profile:v','high','-level:v',level,'-pix_fmt','yuv420p',
         '-x264-params',f'ref={refs}:bframes=2:b-pyramid=none:keyint=30:scenecut=0',
         '-movflags','+frag_keyframe+empty_moov+default_base_moof+global_sidx+skip_trailer',path])
    out=B/f'1080-{label}.h264'
    run([B/'hd_1080_tests',path,out,mode])
    if mode=='accept':run(['ffmpeg','-hide_banner','-loglevel','error','-xerror','-threads','2','-i',out,'-f','null','-'])
# Same real 30fps data declaring Level 5.1: normalize only after timing/DPB proof.
data=bytearray((B/'1080-30.mp4').read_bytes());i=data.index(b'avcC')+4
assert data[i]==1 and data[i+8]&31==7
data[i+3]=data[i+11]=51
(B/'1080-overdeclared.mp4').write_bytes(data)
run([B/'hd_1080_tests',B/'1080-overdeclared.mp4',B/'1080-normalized.h264','accept'])
run(['ffmpeg','-hide_banner','-loglevel','error','-xerror','-i',B/'1080-normalized.h264','-f','null','-'])
run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
     '-Iinclude','src/ui_chime.c','tests/ui_chime_tests.c','-lm','-o',B/'ui_chime_tests'])
run([B/'ui_chime_tests'])
print('PASS 1080p24/30 MP4 timing, Level 5.1 normalization, 60fps/DPB rejection and chime continuity; PS3 throughput not measured')
