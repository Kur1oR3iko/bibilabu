#!/usr/bin/env python3
from pathlib import Path
import subprocess
P=Path(__file__).resolve().parents[1];B=P/'build/native-checks';B.mkdir(exist_ok=True)
def run(cmd):subprocess.run(list(map(str,cmd)),cwd=P,check=True)
run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Iinclude','src/mp4_dash.c','src/avc_sps.c','src/yuv_frame.c','tests/portrait_rate_tests.c','-lm','-o',B/'portrait_rate_tests'])
for w,h,fps in [(480,864,30),(720,1280,30),(1280,720,60),(720,1280,60),(720,1280,120),(1280,720,240)]:
    label=f'{w}x{h}-{fps}';src=B/f'{label}.mp4';dst=B/f'{label}.h264'
    run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','lavfi','-i',f'testsrc2=size={w}x{h}:rate={fps}:duration=1','-an','-c:v','libx264','-threads','2','-preset','veryfast','-profile:v','high','-level:v','5.1','-pix_fmt','yuv420p','-x264-params',f'ref=4:bframes=2:b-pyramid=none:keyint={fps}:scenecut=0','-movflags','+frag_keyframe+empty_moov+default_base_moof+global_sidx+skip_trailer',src])
    level=41 if fps<=60 else 42 if fps<=144 else 51
    run([B/'portrait_rate_tests',src,dst,w,h,fps,level])
    run(['ffmpeg','-hide_banner','-loglevel','error','-xerror','-threads','2','-r',str(fps),'-i',dst,'-f','null','-'])
print('PASS real portrait/high-frame-rate stream checks; host decode does not validate PS3 firmware/performance')
