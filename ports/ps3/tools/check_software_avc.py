#!/usr/bin/env python3
from pathlib import Path
import subprocess,hashlib,os
P=Path(__file__).resolve().parents[1];B=P/'build/native-checks';B.mkdir(exist_ok=True)
SDK=Path(os.environ.get('PS3DEV','/opt/wiliwili-ps3/ps3dev'));F=SDK.parent/'ffmpeg-host-7.1.1'
if not (F/'lib/libavcodec.a').exists():raise RuntimeError('Run bootstrap_software_avc.py --host first')
def run(a):subprocess.run(list(map(str,a)),cwd=P,check=True)
run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Iinclude',f'-I{F}/include','src/software_avc.c','src/mp4_dash.c','src/avc_sps.c','tests/software_avc_tests.c',F/'lib/libavcodec.a',F/'lib/libavutil.a','-lm','-o',B/'software_avc_tests'])
for h in [1280,1278]:
    src=B/f'portrait-software-{h}.mp4';decoded=B/f'portrait-software-{h}.yuv';reference=B/f'portrait-reference-{h}.yuv'
    run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','lavfi','-i',f'testsrc2=size=720x{h}:rate=30:duration=1','-an','-c:v','libx264','-threads','2','-profile:v','high','-level:v','4.0','-preset','medium','-x264-params','ref=7:bframes=3:b-pyramid=normal:keyint=30:scenecut=0','-pix_fmt','yuv420p','-movflags','+frag_keyframe+empty_moov+default_base_moof+global_sidx+skip_trailer',src])
    run([B/'software_avc_tests',src,decoded,720,h,30])
    run(['ffmpeg','-hide_banner','-loglevel','error','-xerror','-y','-i',src,'-pix_fmt','yuv420p','-f','rawvideo',reference])
    assert hashlib.sha256(decoded.read_bytes()).digest()==hashlib.sha256(reference.read_bytes()).digest()
    print('PASS exact decoded pixel identity against independent ffmpeg for',h)
