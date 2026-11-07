#!/usr/bin/env python3
"""Build and validate the native PS3 client with --bili-play."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from build_boot_probe import PORT, CONTENT_ID, ID, run, elf_segments
from check_boot_exit import trace_entry

VERSION = '0.3.3'


def instrument_constructors(path, syms):
    """Trace SDK ctors and initialize allocator locks before strdup users.

    This is a local workaround for this pinned SDK's constructor table. Fail
    closed on a changed table rather than guessing at another SDK's ordering.
    """
    symbols = {line.split()[-1]: int(line.split()[0], 16)
               for line in syms.splitlines() if len(line.split()) == 3}
    data = bytearray(path.read_bytes())
    def file_offset(address, size):
        for ph in elf_segments(data):
            if ph[0] == 1 and ph[3] <= address and address + size <= ph[3] + ph[5]:
                return ph[2] + address - ph[3]
        raise RuntimeError('Constructor address outside file-backed memory')
    shoff = struct.unpack_from('>Q', data, 40)[0]
    shsize, count, strindex = struct.unpack_from('>HHH', data, 58)
    if shsize != 64: raise RuntimeError('Unexpected ELF section header size')
    sections = [struct.unpack_from('>IIQQQQIIQQ', data, shoff + i*shsize) for i in range(count)]
    names_section = sections[strindex]
    names = data[names_section[4]:names_section[4]+names_section[5]]
    ctors = [sh for sh in sections if names[sh[0]:].split(b'\0', 1)[0] == b'.ctors']
    if len(ctors) != 1: raise RuntimeError('Missing constructor table')
    sh = ctors[0]
    entries = list(struct.unpack_from('>'+'Q'*(sh[5]//8), data, sh[4]))
    expected = ['trace_constructor', 'sbrk_init', '__syscalls_init', '__glob_file_init', 'init_metalock']
    if entries != [0xffffffffffffffff]+[symbols[name] for name in reversed(expected)]+[0]:
        raise RuntimeError('SDK constructor order changed: review before instrumenting')
    report = []
    for i, name in enumerate(expected[1:]):
        original = symbols[name]
        wrapper = symbols[f'wili_ctor_{i}']
        pos = entries.index(original)
        struct.pack_into('>Q', data, sh[4] + pos*8, wrapper)
        struct.pack_into('>Q', data, file_offset(symbols['wili_original_ctors'] + i*8, 8), original)
        # Each pointer is an ELFv1 descriptor, not a code address.
        code, toc = struct.unpack_from('>QQ', data, file_offset(wrapper, 16))
        if not code or not toc: raise RuntimeError('Invalid constructor wrapper descriptor')
        report.append({'name': name, 'original_descriptor': original, 'wrapper_descriptor': wrapper})
    # __glob_file_init calls strdup/malloc, which uses the allocator metaLock.
    # In the linked SDK it runs before init_metalock, triggering abort on PS3
    # (0.3.2 hardware log). Preserve heap/syscalls setup, then create metaLock
    # before the first allocating constructor. Each constructor still runs once.
    order = ['trace_constructor', 'wili_ctor_0', 'wili_ctor_1', 'wili_ctor_3', 'wili_ctor_2']
    ordered = [0xffffffffffffffff] + [symbols[name] for name in reversed(order)] + [0]
    struct.pack_into('>'+'Q'*len(ordered), data, sh[4], *ordered)
    actual = list(struct.unpack_from('>'+'Q'*len(ordered), data, sh[4]))
    if actual != ordered or len(set(actual[1:-1])) != len(order):
        raise RuntimeError('Constructor repair did not preserve one call per initializer')
    path.write_bytes(data)
    return report


def validate_entry(b, syms):
    symbols = {line.split()[-1]: int(line.split()[0], 16)
               for line in syms.splitlines() if len(line.split()) == 3}
    entry = struct.unpack_from('>Q', b, 24)[0]
    if entry != symbols['wili_early_entry']:
        raise RuntimeError('Not using pre-CRT diagnostic entry')
    segments = elf_segments(b)
    for ph in segments:
        if ph[0] == 1 and ph[3] <= entry and entry + 8 <= ph[3] + ph[5]:
            code, toc = struct.unpack_from('>II', b, ph[2] + entry - ph[3])
            break
    else:
        raise RuntimeError('Entry descriptor outside file-backed memory')
    if code != symbols['.wili_early_start'] or not toc:
        raise RuntimeError('Invalid compact entry descriptor')
    if not any(ph[0] == 1 and ph[1] & 1 and ph[3] <= code < ph[3] + ph[5] for ph in segments):
        raise RuntimeError('Entry code not executable')
    # ELFv1 TOC is biased by 0x8000 for signed 16-bit loads; the base itself
    # need not be mapped, but its table origin must be.
    if not any(ph[0] == 1 and ph[3] <= toc - 0x8000 < ph[3] + ph[6] for ph in segments):
        raise RuntimeError('Entry TOC table outside mapped memory')
    return {'address': entry, 'code': code, 'toc': toc}


def validate_decoder_callback(b, syms, symbol='decoder_callback'):
    symbols = {line.split()[-1]: int(line.split()[0],16)
               for line in syms.splitlines() if len(line.split())==3}
    address=symbols[symbol]
    for ph in elf_segments(b):
        if ph[0]==1 and ph[3]<=address and address+24<=ph[3]+ph[5]:
            offset=ph[2]+address-ph[3]
            code,toc=struct.unpack_from('>QQ',b,offset)
            compact=struct.unpack_from('>II',b,offset+16)
            break
    else:raise RuntimeError('Decoder callback descriptor outside mapped file')
    if not (0<code<=0xffffffff and 0<toc<=0xffffffff) or compact!=(code,toc):
        raise RuntimeError('Decoder callback is incompatible with PPU32 descriptor conversion')
    if not any(ph[0]==1 and ph[1]&1 and ph[3]<=code<ph[3]+ph[5] for ph in elf_segments(b)):
        raise RuntimeError('Decoder callback code outside executable memory')
    return {'descriptor':address,'code':code,'toc':toc,'compact_descriptor_verified':True,
            'scope':'ELF representation only; firmware callback invocation requires PS3 test'}


def validate_adec_imports(b,syms):
    symbols={line.split()[-1]:int(line.split()[0],16) for line in syms.splitlines() if len(line.split())==3}
    def offset(address,size):
        for ph in elf_segments(b):
            if ph[0]==1 and ph[3]<=address and address+size<=ph[3]+ph[5]:return ph[2]+address-ph[3]
        raise RuntimeError('Import pointer outside mapped ELF')
    shoff=struct.unpack_from('>Q',b,40)[0]
    size,count,names_index=struct.unpack_from('>HHH',b,58)
    sections=[struct.unpack_from('>IIQQQQIIQQ',b,shoff+i*size) for i in range(count)]
    ns=sections[names_index];names=b[ns[4]:ns[4]+ns[5]]
    stub=next(sh for sh in sections if names[sh[0]:].split(b'\0',1)[0]==b'.lib.stub')
    if stub[5]%44:raise RuntimeError('Malformed PRX import header stride')
    rows={}
    for at in range(stub[4],stub[4]+stub[5],44):
        row=struct.unpack_from('>IHH9I',b,at)
        if row[0]!=0x2c000001:raise RuntimeError('Invalid PRX import header')
        name_at=offset(row[5],1);name=bytes(b[name_at:].split(b'\0',1)[0])
        if name in [b'libadec',b'cellAdec']:
            if name in rows:raise RuntimeError('Duplicate AAC import library')
            rows[name]=row
    if set(rows)!={b'libadec',b'cellAdec'}:raise RuntimeError('AAC alias import libraries missing')
    expected={'QueryAttr':0x7e4a4a49,'Open':0xd00a6988,'Close':0x847d2380,'StartSeq':0x487b613e,
              'EndSeq':0xe2ea549b,'DecodeAu':0x1529e506,'GetPcm':0x97ff2af1,'GetPcmItem':0xbd75f78b}
    for library,prefix in [(b'libadec','wiliAdec'),(b'cellAdec','wiliCellAdec')]:
        row=rows[library]
        if row[2]!=8:raise RuntimeError('AAC alias must import exactly 8 functions')
        slots=[]
        for name,nid in expected.items():
            fnid=symbols[prefix+name+'_fnid'];fstub=symbols[prefix+name+'_stub']
            index=(fnid-row[6])//4
            if (fnid-row[6])%4 or not 0<=index<8 or fstub!=row[7]+index*4:
                raise RuntimeError('AAC NID/stub table index mismatch: '+prefix+name)
            if struct.unpack_from('>I',b,offset(fnid,4))[0]!=nid:raise RuntimeError('Wrong AAC NID: '+prefix+name)
            target=struct.unpack_from('>I',b,offset(fstub,4))[0]
            if target!=symbols['__'+prefix+name]:raise RuntimeError('Wrong AAC import trampoline: '+prefix+name)
            slots.append(index)
        if sorted(slots)!=list(range(8)):raise RuntimeError('Duplicate AAC import slots')
    return {'libraries':{'libadec':8,'cellAdec':8},'functions':16,'header_bytes':44,'pointer_bytes':4,'nid_stub_mapping':'PASS',
            'scope':'ELF import table integrity; runtime resolves and validates the active firmware library before calls'}



def validate_vdec_imports(b,syms):
    symbols={line.split()[-1]:int(line.split()[0],16) for line in syms.splitlines() if len(line.split())==3}
    def offset(address,size):
        for ph in elf_segments(b):
            if ph[0]==1 and ph[3]<=address and address+size<=ph[3]+ph[5]:return ph[2]+address-ph[3]
        raise RuntimeError('VDEC import outside ELF')
    shoff=struct.unpack_from('>Q',b,40)[0];size,count,nsindex=struct.unpack_from('>HHH',b,58)
    sections=[struct.unpack_from('>IIQQQQIIQQ',b,shoff+i*size) for i in range(count)]
    ns=sections[nsindex];names=b[ns[4]:ns[4]+ns[5]]
    stub=next(sh for sh in sections if names[sh[0]:].split(b'\0',1)[0]==b'.lib.stub')
    rows=[]
    for at in range(stub[4],stub[4]+stub[5],44):
        row=struct.unpack_from('>IHH9I',b,at);start=offset(row[5],1)
        if b[start:].split(b'\0',1)[0]==b'libvdec':rows.append(row)
    if len(rows)!=1 or rows[0][2]!=10:raise RuntimeError('Expected one 10-function libvdec import table')
    row=rows[0];expected={'vdecClose':0x16698e83,'vdecGetPicItem':0x17c702b9,'vdecDecodeAu':0x2bf4ddd2,'vdecGetPicture':0x807c861a,'vdecEndSequence':0x824433f0,'vdecOpen':0xb6bbcd5d,'vdecStartSequence':0xc757c2aa,'vdecQueryAttr':0xff6f6ebe,'wiliVdecQueryAttrEx':0xc982a84a,'wiliVdecOpenEx':0x0053e2d8}
    seen=[]
    for name,nid in expected.items():
        fnid=symbols[name+'_fnid'];slot=symbols[name+'_stub'];index=(fnid-row[6])//4
        if (fnid-row[6])%4 or not 0<=index<10 or slot!=row[7]+index*4:raise RuntimeError('VDEC NID/stub index mismatch '+name)
        if struct.unpack_from('>I',b,offset(fnid,4))[0]!=nid or struct.unpack_from('>I',b,offset(slot,4))[0]!=symbols['__'+name]:raise RuntimeError('VDEC import mismatch '+name)
        seen.append(index)
    if sorted(seen)!=list(range(10)):raise RuntimeError('VDEC duplicate slots')
    return {'library':'libvdec','functions':10,'extended_functions':2,'nid_stub_mapping':'PASS','hardware_validation':'pending'}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--video',action='store_true',help='Build the native AVC/AAC fixed-pair test')
    parser.add_argument('--network',action='store_true',help='Use configured LAN HTTP/HTTPS Range sources')
    parser.add_argument('--bili',action='store_true',help='Public HTTPS, Bilibili metadata/WBI/playurl/CDN probe page')
    parser.add_argument('--bili-play',action='store_true',help='Real Bilibili DASH native playback test')
    parser.add_argument('--release-only',action='store_true',help='Build and package without running test suites')
    args=parser.parse_args()
    if args.bili_play:
        # 1.0 implies network+video but must never define WILI_API_TEST.
        args.network=True
        args.video=True
        args.bili=False
    if args.bili:args.network=True
    if args.network:args.video=True
    version='1.0.0-r5' if args.bili_play else '0.9' if args.bili else '0.8' if args.network else '0.7.1' if args.video else VERSION
    kind='bili-play' if args.bili_play else 'api' if args.bili else 'network' if args.network else 'video' if args.video else 'platform'
    sdk=Path(os.environ.get('PS3DEV','/opt/wiliwili-ps3/ps3dev'))
    software_sdk=sdk.parent/'ffmpeg-ppu-7.1.1'
    if args.bili_play and not (software_sdk/'lib/libavcodec.a').exists():
        raise RuntimeError('Build software decoder first: python3 tools/bootstrap_software_avc.py')
    env=dict(os.environ,PS3DEV=str(sdk),PSL1GHT=str(sdk))
    env['PATH']=str(sdk/'bin')+':'+str(sdk/'ppu/bin')+':'+env.get('PATH','')
    build=PORT/'build'/kind;stage=build/'pkg';dist=PORT/'dist'
    env['WILI_PROBE_BUILD_DIR']=str(build)
    for p in [build,stage/'USRDIR',dist]:p.mkdir(parents=True,exist_ok=True)
    gcc=sdk/'ppu/bin/ppu-gcc'
    if args.video and not args.release_only:
        if not args.bili_play and (not args.bili or not all((PORT/'build/media'/f).exists() for f in ['test_clip_identity.h','test_audio_identity.h','clip-manifest.json'])):
            run([sys.executable,PORT/'tools/prepare_video_clip.py'],env)
        guard_test=build/'adec_binding_test'
        run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-I'+str(PORT/'include'),PORT/'platform/adec_bindings.c',PORT/'tests/adec_binding_tests.c','-o',guard_test],env)
        run([guard_test],env)
        pcm_test=build/'pcm_ring_test'
        run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(PORT/'include'),PORT/'src/pcm_ring.c',PORT/'tests/pcm_ring_tests.c','-o',pcm_test],env)
        run([pcm_test],env)
    if args.bili_play and not args.release_only:
            # 1.0 host regressions run before the PPU build; fixtures and test paths
            # are relative to ports/ps3, so cwd must be PORT.
            # check_http_range.py still exercises the shared Range client against the
            # local fixture; generate it on a clean tree without changing playback.
            if not all((PORT/'build/media'/f).exists() for f in ['WILI_VIDEO.MP4','WILI_AUDIO.MP4']):
                run([sys.executable,PORT/'tools/prepare_video_clip.py'],env)
            run([sys.executable,PORT/'tools/check_bili_api.py'],env)
            run([sys.executable,PORT/'tools/check_http_range.py'],env)
            subprocess.run([sys.executable,'tools/make_dash_fixtures.py'],cwd=PORT,check=True,env=env)
            subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                            '-I','include','src/avc_sps.c','src/mp4_dash.c','src/resampler.c','tests/mp4_dash_tests.c',
                            '-o','build/bili-play/mp4_dash_tests'],cwd=PORT,check=True,env=env)
            subprocess.run([str(build/'mp4_dash_tests')],cwd=PORT,check=True,env=env)
            # 1.0.1 hardware freeze: NaN/over-range cellAdec PCM rejected by the
            # ring writer. Pin both the sanitizer and the logged capacity margin.
            subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined','-I','include',
                            'src/pcm_ring.c','tests/pcm_sanitize_tests.c','-lm',
                            '-o',str(build/'pcm_sanitize_tests')],cwd=PORT,check=True,env=env)
            subprocess.run([str(build/'pcm_sanitize_tests')],cwd=PORT,check=True,env=env)
            # Pin the 1.0 hardware regression: a valid `206 bytes 0-4095/N` probe
            # must be accepted (the 1.0 guard wrongly rejected range_start==0).
            pkg_flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','libcurl','json-c'],text=True))
            session_test=build/'bili_session_tests'
            subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations',
                            '-fsanitize=address,undefined','-I','include',
                            'src/bili_session.c','src/bili_api.c','src/https_request.c','src/http_range.c',
                            'src/mp4_dash.c','src/avc_sps.c','tests/bili_session_tests.c',
                            *pkg_flags,'-lcrypto','-o',str(session_test)],cwd=PORT,check=True,env=env)
            subprocess.run([str(session_test)],cwd=PORT,check=True,env=env)
            qr_test=build/'qr_login_tests'
            subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations',
                            '-fsanitize=address,undefined','-DWILI_CONFIG_TEST_ONLY','-I','include','-I','third_party/qrcodegen',
                            'src/qr_login.c','third_party/qrcodegen/qrcodegen.c','src/https_request.c','tests/qr_login_tests.c',
                            *pkg_flags,'-o',str(qr_test)],cwd=PORT,check=True,env=env)
            subprocess.run([str(qr_test)],cwd=PORT,check=True,env=env)
            run([sys.executable,PORT/'tools/check_native_runtime.py'],env)
            run([sys.executable,PORT/'tools/check_720p.py'],env)
            run([sys.executable,PORT/'tools/check_portrait_rates.py'],env)
            run([sys.executable,PORT/'tools/check_software_avc.py'],env)
            run([sys.executable,PORT/'tools/check_1080p.py'],env)
            run([sys.executable,PORT/'tools/check_playback_buffers.py'],env)
            run([sys.executable,PORT/'tools/check_detail_features.py'],env)
            run([sys.executable,PORT/'tools/check_history_sync.py'],env)
            run([sys.executable,PORT/'tools/check_yuv.py'],env)
            feed_test=build/'bili_feed_tests'
            subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations',
                            '-fsanitize=address,undefined','-I','include',
                            'src/bili_feed.c','src/search_text.c','src/https_request.c','tests/bili_feed_tests.c',
                            *pkg_flags,'-lpthread','-o',str(feed_test)],cwd=PORT,check=True,env=env)
            subprocess.run([str(feed_test)],cwd=PORT,check=True,env=env)
            font_test=build/'ui_font_tests'
            subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations',
                            '-fsanitize=address,undefined','-I','include',
                            'src/ui_font.c','tests/ui_font_tests.c',
                            '-lm','-o',str(font_test)],cwd=PORT,check=True,env=env)
            subprocess.run([str(font_test)],cwd=PORT,check=True,env=env)
            img_test=build/'ui_image_tests'
            subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Wno-deprecated-declarations',
                            '-fsanitize=address,undefined','-I','include','-I','third_party/stb',
                            'src/ui_image.c','src/https_request.c','tests/ui_image_tests.c',
                            *pkg_flags,'-lpthread','-lm','-o',str(img_test)],cwd=PORT,check=True,env=env)
            subprocess.run([str(img_test)],cwd=PORT,check=True,env=env)
    if args.bili_play:run([sys.executable,PORT/'tools/build_rsx_shaders.py','--out',build/'shaders'],env)
    sources=[PORT/'platform'/name for name in ['platform_probe.c','probe_exit.S','early_entry.S','early_trace.c','ctor_trace.c']]
    extra_flags=[]
    extra_libs=[]
    if args.bili_play:
        sources += [PORT/'platform/native_bili_player.c',PORT/'platform/media_memory.c',PORT/'platform/vdec_ex_bindings.c',PORT/'src/vdec_ex_spec.c',PORT/'src/user_settings.c',PORT/'src/software_avc.c',PORT/'platform/adec_bindings.c',PORT/'platform/network_source.c',
                    PORT/'src/detail_layout.c',
                    PORT/'src/bili_detail.c',PORT/'src/history_sync.c',PORT/'src/danmaku.c',PORT/'platform/danmaku_runtime.c',
                    PORT/'src/avc_sps.c',PORT/'src/mp4_dash.c',PORT/'src/resampler.c',PORT/'src/pcm_ring.c',PORT/'src/compressed_ring.c',
                    PORT/'src/http_range.c',PORT/'src/https_request.c',PORT/'src/bili_api.c',PORT/'src/bili_session.c',PORT/'src/qr_login.c',PORT/'third_party/qrcodegen/qrcodegen.c',
                    PORT/'platform/runtime_log.c',PORT/'platform/audio_runtime.c',PORT/'platform/ui_sound.c',PORT/'src/ui_chime.c',PORT/'platform/rsx_video.c',PORT/'src/yuv_frame.c',PORT/'src/native_ui.c',PORT/'src/ui_motion.c',PORT/'src/ui_core.c',PORT/'src/ui_font.c',PORT/'src/ui_image.c',PORT/'src/bili_feed.c',PORT/'src/search_text.c',PORT/'platform/search_osk.c',PORT/'platform/wallpaper.c',
                    PORT/'third_party/psl1ght-imports/exports.S',PORT/'third_party/psl1ght-imports/libexport.c',
                    PORT/'third_party/psl1ght-imports/cell/exports_cell.S',PORT/'third_party/psl1ght-imports/cell/libexport_cell.c',
                    PORT/'third_party/psl1ght-imports/vdec/exports_vdec.S',PORT/'third_party/psl1ght-imports/vdec/libexport_vdec.c']
        extra_flags=['-DWILI_VIDEO_TEST','-DWILI_NETWORK_TEST','-DWILI_BILI_PLAY_TEST',
                     '-I'+str(PORT/'include'),'-I'+str(build/'shaders'),'-I'+str(PORT/'build/media'),'-I'+str(PORT/'third_party/qrcodegen'),'-I'+str(PORT/'third_party/stb'),
                     '-I'+str(PORT/'third_party/psl1ght-imports'),
                     '-DCURL_STATICLIB','-I'+str(software_sdk/'include'),'-I'+str(sdk/'portlibs/ppu/include')]
        extra_libs=[str(software_sdk/'lib/libavcodec.a'),str(software_sdk/'lib/libavutil.a'),'-lsysmodule','-laudio',
                    '-L'+str(sdk/'portlibs/ppu/lib'),'-lcurl','-lmbedtls','-lmbedx509','-lmbedcrypto','-lz','-lnet','-lnetctl',
                    '-ljson-c']
    else:
        if args.video:
            sources += [PORT/'platform/native_video_probe.c',PORT/'platform/native_audio_probe.c',PORT/'platform/adec_bindings.c',PORT/'platform/mp4_file.c',PORT/'src/fmp4_stream.c',PORT/'src/pcm_ring.c',PORT/'src/avc_test_stream.c',PORT/'src/aac_test_stream.c',PORT/'third_party/psl1ght-imports/exports.S',PORT/'third_party/psl1ght-imports/libexport.c',PORT/'third_party/psl1ght-imports/cell/exports_cell.S',PORT/'third_party/psl1ght-imports/cell/libexport_cell.c']
            extra_flags=['-DWILI_VIDEO_TEST','-I'+str(PORT/'include'),'-I'+str(PORT/'build/media'),'-I'+str(PORT/'third_party/psl1ght-imports')]
            extra_libs=['-lvdec','-lsysmodule','-laudio']
        if args.network:
            run([sys.executable,PORT/'tools/check_http_range.py'],env)
            sources += [PORT/'src/http_range.c',PORT/'platform/network_source.c']
            extra_flags += ['-DWILI_NETWORK_TEST','-DCURL_STATICLIB','-I'+str(sdk/'portlibs/ppu/include')]
            extra_libs += ['-L'+str(sdk/'portlibs/ppu/lib'),'-lcurl','-lmbedtls','-lmbedx509','-lmbedcrypto','-lz','-lnet','-lnetctl']
        if args.bili:
            run([sys.executable,PORT/'tools/check_bili_api.py'],env)
            sources += [PORT/'src/https_request.c',PORT/'src/bili_api.c',PORT/'src/bili_probe.c']
            extra_flags += ['-DWILI_API_TEST']
            extra_libs += ['-ljson-c']
    for source in sources:
        assembler_flags=['-Wa,-mregnames'] if source.suffix=='.S' else []
        metadata_flags=['-fno-toplevel-reorder'] if source.name in ['libexport.c','libexport_cell.c','libexport_vdec.c'] else []
        run([gcc,'-std=gnu99','-O2','-Wall','-Wextra','-Werror','-mcpu=cell','-mhard-float',
             '-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-tree-loop-distribute-patterns',
             '-ffunction-sections','-fdata-sections','-I'+str(sdk/'ppu/include'),
             *extra_flags,*assembler_flags,*metadata_flags,'-c',source,'-o',build/(source.name+'.o')],env)
    elf=build/'platform.elf';patched=build/'platform.patched.elf'
    early_undefined=subprocess.check_output([str(sdk/'ppu/bin/ppu-nm'),'-u',str(build/'early_trace.c.o')],env=env,text=True).strip()
    if early_undefined:raise RuntimeError('Pre-CRT trace has runtime dependency: '+early_undefined)
    run([gcc,*[build/(source.name+'.o') for source in sources],
         '-Wl,-e,wili_early_entry','-Wl,--wrap=abort',
         '-Wl,-Map,'+str(build/'platform.map'),'-L'+str(sdk/'ppu/lib'),
         *extra_libs,'-lrsx','-lgcm_sys','-lio','-lsysutil','-lm','-o',elf],env)
    shutil.copyfile(elf,patched)
    run([sdk/'ppu/bin/ppu-strip','--strip-debug',patched],env)
    run([sdk/'bin/sprxlinker',patched],env)
    syms=subprocess.check_output([str(sdk/'ppu/bin/ppu-nm'),'-n',str(patched)],env=env,text=True)
    constructors=instrument_constructors(patched,syms)
    b=patched.read_bytes()
    if b[:8]!=b'\x7fELF\x02\x02\x01\x66' or struct.unpack_from('>HH',b,16)!=(2,21):raise RuntimeError('Wrong ELF architecture')
    params=[]
    for ph in elf_segments(b):
        if ph[2]+ph[5]>len(b) or ph[5]>ph[6]:raise RuntimeError('Bad ELF segment bounds')
        if ph[0]==0x60000001 and ph[5]>=32:params.append(b[ph[2]:ph[2]+32])
    if len(params)!=1 or struct.unpack_from('>I',params[0],4)[0]!=0x13bcc5f6:raise RuntimeError('Missing process parameters')
    syms=subprocess.check_output([str(sdk/'ppu/bin/ppu-nm'),'-n',str(patched)],env=env,text=True)
    entry=validate_entry(b,syms)
    callback=validate_decoder_callback(b,syms,'vdec_callback' if args.bili_play else 'decoder_callback') if args.video else None
    audio_callbacks={name:validate_decoder_callback(b,syms,name) for name in ['aac_callback','audio_worker']} if args.video else None
    adec_imports=validate_adec_imports(b,syms) if args.video else None
    vdec_imports=validate_vdec_imports(b,syms) if args.bili_play else None
    addr=[int(l.split()[0],16)for l in syms.splitlines()if l.split()[-1:]==['.wili_platform_exit']]
    if len(addr)!=1:raise RuntimeError('Exit function missing')
    primary=trace_entry(patched,code_address=addr[0])
    fallback=trace_entry(patched,refuse_process_exit=True,code_address=addr[0])
    if [c['number']for c in primary['calls']]!=[3] or [c['number']for c in fallback['calls']]!=[3,22]:raise RuntimeError('Bad exit ABI')
    if not args.release_only:
        run([sys.executable,PORT/'tests/test_ctor_repair.py'],env)
        if args.video:run([sys.executable,PORT/'tests/test_audio_imports.py'],env)
    run([sdk/'bin/make_self_npdrm',patched,stage/'USRDIR/EBOOT.BIN',CONTENT_ID],env)
    xml=ET.parse(sdk/'bin/sfo.xml')
    values={'TITLE':'wiliwili PS3 Platform Test 0.3.3','TITLE_ID':ID,'APP_VER':'00.33','PS3_SYSTEM_VER':'04.9300','PARENTAL_LEVEL':'1'}
    if args.video:values.update(TITLE='wiliwili PS3 Stream Test 0.7.1',APP_VER='00.71')
    if args.network:values.update(TITLE='wiliwili PS3 Network Test 0.8',APP_VER='00.80')
    if args.bili:values.update(TITLE='wiliwili PS3 API Test 0.9',APP_VER='00.90')
    if args.bili_play:values.update(TITLE='bibilabu PS3 1.0.0-r5',APP_VER='01.00')
    for value in xml.getroot().findall('value'):
        if value.get('name')in values:value.text=values[value.get('name')]
    xml_path=build/'PARAM.SFO.xml';xml.write(xml_path,encoding='utf-8',xml_declaration=True)
    run([sdk/'bin/sfo','-f',xml_path,stage/'PARAM.SFO'],env)
    shutil.copyfile(PORT/'assets/ICON0.PNG' if args.bili_play else PORT/'assets/ICON0.PNG',stage/'ICON0.PNG')
    if args.video:
        licenses=stage/'USRDIR/licenses';licenses.mkdir(exist_ok=True)
        shutil.copyfile(PORT/'third_party/psl1ght-imports/LICENSE',licenses/'PSL1GHT-imports.txt')
    if args.bili_play:
        shutil.copyfile(PORT/'assets/switch_font.ttf', stage/'USRDIR/font.ttf')
        shutil.copyfile(sdk.parent/'ffmpeg-src/ffmpeg-7.1.1/COPYING.LGPLv2.1',stage/'USRDIR/licenses/FFmpeg-LGPL-2.1.txt')
        shutil.copyfile(PORT/'assets/switch_font-LICENSE.txt',stage/'USRDIR/licenses/SourceHanSans-OFL-1.1.txt')
    if args.network:
        shutil.copyfile('/etc/ssl/certs/ca-certificates.crt',stage/'USRDIR/cacert.pem')
        for license_file in (PORT/'third_party/network-licenses').glob('*.txt'):
            shutil.copyfile(license_file,stage/'USRDIR/licenses'/license_file.name)
        shutil.copyfile('/usr/share/doc/ca-certificates/copyright',stage/'USRDIR/licenses/ca-certificates.txt')
    debug=build/'platform.debug.pkg';package=dist/f'bibilabu-PS3-test-{version}.pkg' if args.bili_play else dist/f'wiliwili-PS3-{kind}-test-{version}.pkg'
    run([sdk/'bin/pkg','--contentid='+CONTENT_ID,stage,debug],env)
    unpack=Path(tempfile.mkdtemp(prefix='verify-',dir=build))
    p=subprocess.run([str(sdk/'bin/pkg'),'-x',str(debug)],env=env,cwd=unpack,capture_output=True,text=True,check=True)
    if 'Unsupported Type'in p.stdout:raise RuntimeError('Cannot extract validation package')
    eboots=list(unpack.rglob('EBOOT.BIN'))
    if len(eboots)!=1:raise RuntimeError('Invalid package contents')
    root=eboots[0].parent.parent
    expected=sorted(str(x.relative_to(stage))for x in stage.rglob('*')if x.is_file())
    actual=sorted(str(x.relative_to(root))for x in root.rglob('*')if x.is_file())
    if actual!=expected:raise RuntimeError('Packaged file list mismatch')
    for name in expected:
        if (stage/name).read_bytes()!=(root/name).read_bytes():raise RuntimeError('Packaged file mismatch: '+name)
    shutil.copyfile(debug,package)
    run([sdk/'bin/package_finalize',package],env)
    output=package.read_bytes()
    if output[:8]!=b'\x7fPKG\x80\0\0\x01' or struct.unpack_from('>Q',output,24)[0]!=len(output):raise RuntimeError('Invalid package header')
    report={'version':version,'artifact':'bounded split-fMP4 native H.264/AAC streaming test' if args.video else 'pre-CRT trace and display-input test only','ps3_hardware_tested':False,
            'entry_descriptor':entry,'pre_crt_trace_undefined_symbols':early_undefined,
            'constructor_tracing':constructors,'constructor_order_unchanged':False,
            'sdk_constructor_order':['sbrk_init','__syscalls_init','__glob_file_init','init_metalock'],
            'repaired_constructor_order':['sbrk_init','__syscalls_init','init_metalock','__glob_file_init'],
            'constructor_fix':'Initialize allocator metaLock before file-path strdup; all initializers retained once',
            'constructor_regression_tests':'PASS (4 offline checks; no PS3 execution)',
            'runtime_allocator_check':'ADDED_NOT_HARDWARE_TESTED',
            'display':'RSX scanout with CPU bitmap drawing','borealis_ported':False,'media_playback':False,
            'exit_static_check':{'primary':primary,'fallback':fallback},'package_files_verified':expected,
            'package_bytes':len(output),'sha256':hashlib.sha256(output).hexdigest()}
    if args.video and not args.bili_play:
        report.update(display='RSX scaled main-to-local blit; cached CPU UI',target_resolution=[848,480],target_fps=30,media_playback='NATIVE_AVC_IMPLEMENTED_NOT_HARDWARE_TESTED',audio_playback='NATIVE_AAC_IMPLEMENTED_NOT_HARDWARE_TESTED',
                      callback_abi='explicit compact PPU32 descriptor via __build_opd32',
                      callback_static_check=callback,
                      audio_callback_checks=audio_callbacks,adec_import_table=adec_imports,
                      audio_import_regressions='PASS (3 offline checks)',
                      audio_runtime_binding_guard='PASS_HOST_TEST: unresolved/partial/invalid rejected without decoder calls',
                      audio_clock='Hardware read-index consumption at 48000Hz; 256-sample resolution',
                      pcm_ring_regression='PASS_HOST_TEST 100000 source frames with wrap and pause rewinds',
                      streaming_buffers={'video_input_bytes':8*256*1024,'aac_input_bytes':8*8192,'pcm_bytes':32768*2*4,'metadata_box_limit_per_track':65536,'sample_index_limit_per_fragment':128},
                      streaming_scope='Fixed local split-fMP4 fixture; native concurrent AAC/VDEC and streaming not yet hardware tested',
                      input_scope='supplied CRC/size/config-checked split-fMP4 fixture; not arbitrary MP4/network',
                      clip=json.loads((PORT/'build/media/clip-manifest.json').read_text()))
    if args.network and not args.bili_play:
        report.update(artifact='HTTP Range native fMP4 streaming test',network='IMPLEMENTED_NOT_PS3_HARDWARE_TESTED',input_scope='configured HTTP/HTTPS fixed fMP4 fixture; no USB media fallback',streaming_scope='LAN HTTP and native HTTPS transport new to hardware; local codec path accepted by user',network_cache_bytes_per_track=262144,network_host_tests='PASS fMP4 AVC=900/AAC=1408, range/etag/redirect/error/cancel cases under ASan/UBSan',TLS='mbedTLS from bundled SDK; peer/host verification enabled; native HTTPS not yet tested')
    if args.bili:
        report.update(display='PPU bitmap API diagnostic page on RSX scanout',artifact='Public HTTPS/Bilibili API/CDN diagnostic page',media_playback='NOT_RUN_IN_API_MODE',audio_playback='NOT_RUN_IN_API_MODE',input_scope='anonymous BVID metadata/playurl/CDN initialization bytes; no account cookies',api_offline_checks='PASS schema/Unicode/WBI golden signature and strict TLS1.2 CA/hostname/body-cap/redirect/cancel tests',api_live_host_probe='Separate captured host run passed NAV, VIEW, WBI PLAYURL and both CDN 206 requests; native TLS/API not yet tested')
    if args.bili:
        for field in ['target_resolution','target_fps','clip','streaming_buffers','audio_clock','network_cache_bytes_per_track','streaming_scope']:
            report.pop(field,None)
        report.update(default_bvid='BV17x411w7KC',requested_qn=32,network='LAN_HTTP_PREVIOUSLY_PASSED_PUBLIC_HTTPS_PENDING')
    if args.bili_play:
        report.update(artifact='bibilabu PS3 native client 1.0.0-r5',
                      ps3_hardware_tested=False,borealis_ported=False,
                      display='Native RSX YUV420 textured quads + premultiplied Aero UI; cached XMB browsing surface',
                      quality='360p/480p/720p AVC; 1080p hidden from UI and excluded from playurl requests and selection',
                      video_output='YUV420P for native AVC; aligned planar layout; no CPU RGB conversion',
                      shader_build='cgcomp -a assembly; no Cg runtime; verified instructions/samplers/matrix reference',
                      aero='Clear untinted frames without gray panel fill; shared XMB wallpaper, white text and focus marks',
                      navigation_sound='Short synthesized two-note PS3 XMB-style tick on tab, selection and page changes',
                      target_resolution=[1280,720],portrait_resolution=[720,1280],target_fps=None,
                      system_overlay='Continuous cached flips, input suppression, system OSK asynchronous lifetime',
                      history='Account cursor history, progress metadata, next page, authenticated background progress reports',
                      search='Explicit Chinese/English OSK and bounded UTF16/UTF8; application keyboard if OSK fails; official buvid + search/type API',
                      history_sync='Authenticated /x/v2/history/report on first presented frame, every 15 seconds of progressing playback, pause/seek/end; independent bounded worker, retry and 2-second exit drain',
                      favorites='Authenticated folder list and paginated media list; folder-aware navigation',
                      ui_motion='180ms interruptible cubic ease-out, 136px selected thumbnail; no playback-view animation',
                      picture_policy='Known PIC warnings and skipped frames handled; coded/visible/padded height variants bounded by allocation; full failure detail',
                      recovery='Retryable network initialization, borrowed-service ownership, joined-worker reinitialization; account retained',
                      seek='L1/R1 +/-10s through sidx, video preroll discard and PCM sample trimming; quality switch preserves time',
                      quality_menu='AVC 720p in landscape or portrait; qn74 requested for high frame rates, UI tier64; no 720p FPS ceiling; firmware throughput still requires hardware validation',
                      account_storage='Escaped JSON, fsync/temp replacement, backup, durable HDD path and logout marker',
                      metadata_limit_bytes=2*1024*1024,
                      wallpaper='Read-only matched user wallpaper.png, 5-second change checks, bounded async decode; not dynamic VSH themes',
                      host_tests='PASS existing ABI/API/MP4/PCM/QR tests plus stale mailbox, image dedup/pause and actual compositor ASan/UBSan',
                      media_playback='IMPLEMENTED_REQUIRES_HARDWARE_VALIDATION',
                      input_scope='User-selected feed video; optional stored login cookie; no hardcoded playback',
                      scheduling='Independent audio/video download workers, bounded 16MiB variable-size packet arena, 2.5s video prefill and AV-coordinated buffering; cached player UI',
                      gpu_depth_buffer_bytes_at_1080p=1920*1080*4,
                      buffers={'video_packets':16*1024*1024,'video_packet_descriptors':128,'video_range_cache':1024*1024,'video_packet_scratch':512*1024,'player_static_ui_cache':1280*720*4,'audio_packets':64*8192,'decoder_video_input':8*512*1024,
                               'video_yuv_720p_pool_example':14680064,'video_yuv_portrait_720p_pool_example':15728640,'danmaku_atlas':10*1024*1024,'thumbnail_compressed_cache_max':8*1024*1024,'feed_session_pages_max':256,
                               'ui_mapped':4*1024*1024,'ui_background_mapped':4*1024*1024,'detail_scroll_canvas':1280*720*4},
                      teardown='Concurrent producer cancellation; cleanup worker with 3s main-thread deadline and cached system flips; logger drain bounded to 750ms',
                      logging='Timestamped 16KiB batch writes to HDD before USB; periodic fsync even under traffic; 4MiB rotation',
                      rendering='RSX wallpaper then pinned YUV video, danmaku and clear UI; cached comment strip clipped independently; cached VRAM flips',
                      logo='Original silver B/play monogram; assets/logo-mark.svg and 320x176 ICON0.PNG',
                      detail_page='Pinned 800x450 video; comments and UP browsing keep playback state; R2/L2 paginate right column; signed 8-item creator pages and cached related slices; XMB wallpaper',
                      danmaku='Standard scrolling/top/bottom/reverse types; no parser time sampling; 0 off, 1..6; full-mode burst queue, 64 visible slots and eight new glyphs per tick; overload counts logged',
                      session_cache='Successful category/folder/page/search responses retained until manual refresh, account change or process exit; no TTL or navigation refresh',
                      chinese_input='Explicit simplified/traditional Chinese and English OSK panels; UTF16/UTF8 conversion; search and comment editing',
                      TLS='CA/hostname verification enabled',callback_static_check=callback,
                      audio_callback_checks=audio_callbacks,adec_import_table=adec_imports,vdec_import_table=vdec_imports)
    if args.release_only:report['host_tests']='SKIPPED_IN_THIS_BUILD_MODE; separately executed checks are recorded in build/evidence'
    manifest=dist/f'{kind}-test-manifest.json'
    manifest.write_text(json.dumps(report,indent=2)+'\n')
    doc='NATIVE_100R5_TEST.md' if args.bili_play else 'API_TEST.md' if args.bili else 'NETWORK_TEST.md' if args.network else 'VIDEO_TEST.md' if args.video else 'PLATFORM_TEST.md'
    doc_present=(PORT/doc).exists()
    if doc_present or not args.bili_play:
        shutil.copyfile(PORT/doc,dist/doc)  # existing modes fail closed on a missing doc
    else:
        print('WARNING: '+str(PORT/doc)+' not found; skipping doc copy',flush=True)
    shutil.copyfile(PORT/'STARTUP_FIX.md',dist/'STARTUP_FIX.md')
    sums=dist/('VIDEO_SHA256SUMS.txt' if args.video else 'PLATFORM_SHA256SUMS.txt')
    sums.write_text(report['sha256']+'  '+package.name+'\n')
    bundle=dist/f'bibilabu-PS3-test-{version}-USB.zip' if args.bili_play else dist/f'wiliwili-PS3-{kind}-test-{version}-USB.zip'
    files=[package,manifest]
    if args.bili_play and (PORT/'DANMAKU_1080_RESEARCH.md').exists():
        shutil.copyfile(PORT/'DANMAKU_1080_RESEARCH.md',dist/'DANMAKU_1080_RESEARCH.md');files.append(dist/'DANMAKU_1080_RESEARCH.md')
    if args.bili_play and (PORT/'YUV_AERO_DESIGN.md').exists():
        shutil.copyfile(PORT/'YUV_AERO_DESIGN.md',dist/'YUV_AERO_DESIGN.md');files.append(dist/'YUV_AERO_DESIGN.md')
    if doc_present:files.append(dist/doc)
    files += [dist/'STARTUP_FIX.md',sums]
    if args.video and not args.bili and not args.bili_play:
        shutil.copyfile(PORT/'build/media/WILI_VIDEO.MP4',dist/'WILI_VIDEO.MP4')
        shutil.copyfile(PORT/'build/media/WILI_AUDIO.MP4',dist/'WILI_AUDIO.MP4')
        shutil.copyfile(PORT/'third_party/psl1ght-imports/LICENSE',dist/'PSL1GHT-IMPORT-LICENSE.txt')
        files.extend([dist/'WILI_VIDEO.MP4',dist/'WILI_AUDIO.MP4',dist/'PSL1GHT-IMPORT-LICENSE.txt'])
    if args.bili:
        (dist/'WILI_BILI.INI').write_text('bvid=BV1uv411q7Mv\n')
        files.append(dist/'WILI_BILI.INI')
    with zipfile.ZipFile(bundle,'w',zipfile.ZIP_DEFLATED)as z:
        for path in files:z.write(path,path.name)
    with zipfile.ZipFile(bundle)as z:
        if z.testzip():raise RuntimeError('Bad archive')
    (PORT/f'evidence/{kind}-{version}-static.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
