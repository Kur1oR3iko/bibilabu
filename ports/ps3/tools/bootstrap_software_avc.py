#!/usr/bin/env python3
"""Build only FFmpeg's software H.264 decoder and avutil for the PS3 PPU."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tarfile,urllib.request
P=Path(__file__).resolve().parents[1]
VERSION='7.1.1'
SHA256='733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1'
parser=argparse.ArgumentParser();parser.add_argument('--host',action='store_true');args=parser.parse_args()
sdk=Path(os.environ.get('PS3DEV','/opt/wiliwili-ps3/ps3dev'));base=sdk.parent
archive=P/'.reference'/f'ffmpeg-{VERSION}.tar.xz';archive.parent.mkdir(exist_ok=True)
if not archive.exists():
    with urllib.request.urlopen(f'https://ffmpeg.org/releases/ffmpeg-{VERSION}.tar.xz',timeout=120) as response:archive.write_bytes(response.read())
if hashlib.sha256(archive.read_bytes()).hexdigest()!=SHA256:raise RuntimeError('FFmpeg source checksum mismatch')
source=base/'ffmpeg-src'/f'ffmpeg-{VERSION}'
if not source.exists():
    source.parent.mkdir(parents=True,exist_ok=True)
    with tarfile.open(archive) as tar:tar.extractall(source.parent,filter='data')
# FFmpeg 7.1.1 H.264-only builds still reference the shared film-grain cleanup.
makefile=source/'libavcodec/Makefile'
text=makefile.read_text()
old='OBJS-$(CONFIG_H264_SEI)                += h264_sei.o h2645_sei.o'
if old+' aom_film_grain.o' not in text:
    if old not in text:raise RuntimeError('Unexpected FFmpeg dependency rule')
    makefile.write_text(text.replace(old,old+' aom_film_grain.o'))
mode='host' if args.host else 'ppu';build=base/f'ffmpeg-build-{mode}';prefix=base/f'ffmpeg-{mode}-{VERSION}';build.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,PS3DEV=str(sdk),PSL1GHT=str(sdk));env['PATH']=str(sdk/'ppu/bin')+':'+str(sdk/'bin')+':'+env['PATH']
flags=['--disable-everything','--disable-autodetect','--enable-decoder=h264','--enable-parser=h264','--disable-programs','--disable-doc','--disable-network','--disable-debug','--disable-shared','--enable-static','--disable-avformat','--disable-avdevice','--disable-avfilter','--disable-swscale','--disable-swresample','--disable-postproc','--disable-pthreads','--disable-w32threads','--disable-os2threads']
if args.host:flags+=['--disable-x86asm']
else:flags+=['--arch=ppc64','--cpu=cell','--target-os=none','--enable-cross-compile',f'--cross-prefix={sdk}/ppu/bin/ppu-','--disable-vsx','--disable-power8',f'--extra-cflags=-I{sdk}/ppu/include -mcpu=cell -maltivec -mabi=altivec -Dstatic_assert=_Static_assert -ffunction-sections -fdata-sections',f'--extra-ldflags=-L{sdk}/ppu/lib']
subprocess.run([str(source/'configure'),f'--prefix={prefix}',*flags],cwd=build,env=env,check=True)
subprocess.run(['make','-j4'],cwd=build,env=env,check=True)
subprocess.run(['make','install'],cwd=build,env=env,check=True)
(prefix/'bibilabu-build.json').write_text(json.dumps({'version':VERSION,'sha256':SHA256,'flags':flags},indent=2))
print('Software AVC dependency built:',prefix)
