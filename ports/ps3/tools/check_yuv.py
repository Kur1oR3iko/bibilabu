#!/usr/bin/env python3
"""Independent FFmpeg color reference for the generated RSX shader matrices."""
from pathlib import Path
import json,subprocess,sys,struct
PORT=Path(__file__).resolve().parents[1]
B=PORT/'build/native-checks';B.mkdir(exist_ok=True)
S=PORT/'build/bili-play/shaders'
subprocess.run([sys.executable,str(PORT/'tools/build_rsx_shaders.py'),'--out',str(S)],check=True)
report=json.loads((S/'shader-report.json').read_text())
colors=[(16,128,128),(235,128,128),(81,90,240),(145,54,34),(41,240,110),(0,128,128),(255,128,128),(127,100,160)]
raw=b''.join(bytes([y])*256+bytes([u])*64+bytes([v])*64 for y,u,v in colors)
(B/'color-reference.yuv').write_bytes(raw)
def f32(x):return struct.unpack('f',struct.pack('f',x))[0]
for name,constants in report['matrices'].items():
    bt='bt709' if '709' in name else 'bt601';full=name.endswith('_full')
    target=B/(name+'.rgb')
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','rawvideo','-pixel_format','yuv420p',
        '-video_size','16x16','-framerate','1','-i',str(B/'color-reference.yuv'),
        '-vf',f'scale=in_color_matrix={bt}:out_color_matrix={bt}:in_range={"pc" if full else "tv"}:out_range=pc',
        '-pix_fmt','rgb24','-f','rawvideo',str(target)],check=True)
    expected=target.read_bytes();assert len(expected)==len(colors)*16*16*3
    for index,sample in enumerate(colors):
        values=[f32(f32(sample[i]/255)+f32(constants[0][i])) for i in range(3)]
        actual=[]
        for row in constants[1:4]:
            c=sum(f32(values[i]*f32(row[i])) for i in range(3))
            actual.append(round(max(0,min(1,c))*255))
        reference=list(expected[index*768:index*768+3])
        assert max(abs(a-b) for a,b in zip(actual,reference))<=3,(name,sample,actual,reference)
    print('PASS shader matrix vs independent FFmpeg:',name)
