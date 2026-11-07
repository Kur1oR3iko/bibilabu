#!/usr/bin/env python3
"""Assemble our RSX shaders without the proprietary Cg frontend/runtime."""
from pathlib import Path
import argparse,os,struct,subprocess,json,hashlib
PORT=Path(__file__).resolve().parents[1]
def yuv_shader(bt709,full):
    kr,kb=(0.2126,0.0722) if bt709 else (0.299,0.114)
    kg=1-kr-kb;ys=1 if full else 255/219;cs=1 if full else 255/224
    constants=[(0 if full else -16/255,-128/255,-128/255,1),
        (ys,0,2*(1-kr)*cs,0),(ys,-2*kb*(1-kb)/kg*cs,-2*kr*(1-kr)/kg*cs,0),
        (ys,2*(1-kb)*cs,0,0),(1,1,1,1)]
    lines=['!!FP2.0']+[f'#const c[{i}] = '+ ' '.join(f'{x:.10f}' for x in c) for i,c in enumerate(constants)]
    lines += ['TEX R0.x, f[TEX0], texture[0], 2D;','TEX R0.y, f[TEX0], texture[1], 2D;',
              'TEX R0.z, f[TEX0], texture[2], 2D;','ADDR R0.xyz, R0, c[0];',
              'DP3R_SAT R1.x, R0, c[1];','DP3R_SAT R1.y, R0, c[2];','DP3R_SAT R1.z, R0, c[3];',
              'MOVR R1.w, c[4].x;','MOVR o[COLR], R1;','END']
    return '\n'.join(lines)+'\n',constants
def validate(data,kind,expected_count):
    assert data[:2]==kind.encode() and len(data)>=36
    count=struct.unpack_from('>H',data,10)[0];offset=struct.unpack_from('>I',data,20)[0]
    assert count==expected_count and offset%16==0 and offset+count*16==len(data)
    if kind=='VP':assert struct.unpack_from('>I',data,24)[0]==0x109
    else:
        # RSX FP instructions store each 32-bit word with its 16-bit halves exchanged.
        words=[struct.unpack_from('>I',data,i)[0] for i in range(offset,len(data),16)]
        hardware=[((w&65535)<<16)|(w>>16) for w in words]
        samplers=[(w>>17)&15 for w in hardware if ((w>>24)&63)==0x17]
        assert samplers==([0] if expected_count==2 else [0,1,2]),samplers
    return {'instructions_with_constants':count,'ucode_offset':offset,'sha256':hashlib.sha256(data).hexdigest()}
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',required=True);args=ap.parse_args()
    out=Path(args.out);out.mkdir(parents=True,exist_ok=True)
    compiler=Path(os.environ.get('PS3DEV','/opt/wiliwili-ps3/ps3dev'))/'bin/cgcomp'
    jobs=[('quad','VP',PORT/'shaders/quad.vp',3),('overlay','FP',PORT/'shaders/overlay.fp',2)]
    report={};matrices={}
    for bt in (False,True):
        for full in (False,True):
            name=f'yuv_{709 if bt else 601}_{"full" if full else "limited"}'
            source,constants=yuv_shader(bt,full);path=out/(name+'.fp');path.write_text(source)
            jobs.append((name,'FP',path,14));matrices[name]=constants
    header=['// Generated from GPL-3.0-or-later shader sources and standard YCbCr matrices.','#pragma once']
    for name,kind,source,count in jobs:
        dest=out/(name+'.bin');subprocess.run([str(compiler),'-a','-v' if kind=='VP' else '-f',str(source),str(dest)],check=True)
        data=dest.read_bytes();report[name]=validate(data,kind,count)
        header.append(f'static const unsigned char shader_{name}[] __attribute__((aligned(16))) = {{'+','.join(str(x) for x in data)+'};')
    (out/'rsx_shaders.h').write_text('\n'.join(header)+'\n')
    (out/'shader-report.json').write_text(json.dumps({'programs':report,'matrices':matrices},indent=2)+'\n')
    print('PASS RSX assembly: vertex inputs, bytecode bounds, TEX sampler instructions and four color matrices')
if __name__=='__main__':main()
