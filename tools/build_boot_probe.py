#!/usr/bin/env python3
"""Build the explicit PS3 startup probe, not the wiliwili client."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from check_boot_exit import trace_entry

PORT = Path(__file__).resolve().parents[1]
ID = 'WILIPRB01'
CONTENT_ID = 'UP0001-WILIPRB01_00-0000000000000000'
VERSION = '0.2'


def run(args, env):
    print('+', ' '.join(map(str, args)), flush=True)
    return subprocess.run(list(map(str,args)), env=env, cwd=PORT, check=True)


def elf_segments(b):
    off = struct.unpack_from('>Q',b,32)[0]
    size,count = struct.unpack_from('>HH',b,54)
    if size != 56 or count > 32: raise RuntimeError('Invalid ELF program headers')
    return [struct.unpack_from('>IIQQQQQQ',b,off+i*size) for i in range(count)]


def main():
    sdk = Path(os.environ.get('PS3DEV','/opt/wiliwili-ps3/ps3dev'))
    env = dict(os.environ, PS3DEV=str(sdk), PSL1GHT=str(sdk))
    env['PATH']=str(sdk/'bin')+':'+str(sdk/'ppu/bin')+':'+env.get('PATH','')
    gcc = sdk/'ppu/bin/ppu-gcc'
    build = PORT/'build/boot'
    stage = build/'pkg'
    dist = PORT/'dist'
    for p in [build,stage/'USRDIR',dist]:p.mkdir(parents=True,exist_ok=True)
    flags=['-O2','-Wall','-Wextra','-Werror','-mcpu=cell','-mhard-float','-ffreestanding',
           '-fno-builtin','-fno-stack-protector','-fno-tree-loop-distribute-patterns',
           '-I'+str(sdk/'ppu/include')]
    for src in ['entry.S','boot_main.c']:
        run([gcc,*flags,'-c',PORT/'bootstrap'/src,'-o',build/(src+'.o')],env)
    elf = build/'boot.elf'
    run([gcc,'-nostdlib','-Wl,-Map,'+str(build/'boot.map'),build/'entry.S.o',build/'boot_main.c.o','-o',elf],env)
    unresolved=subprocess.check_output([str(sdk/'ppu/bin/ppu-nm'),'-u',str(elf)],env=env,text=True).strip()
    if unresolved:raise RuntimeError('Unexpected runtime dependency: '+unresolved)
    b=bytearray(elf.read_bytes())
    if b[:7]!=b'\x7fELF\x02\x02\x01' or struct.unpack_from('>HH',b,16)!=(2,21):raise RuntimeError('Wrong ELF target')
    b[7]=0x66 # Cell LV2 OSABI; no PRX imports or descriptor post-processing.
    elf.write_bytes(b)
    segments=elf_segments(b)
    entry=struct.unpack_from('>Q',b,24)[0]
    entry_file=None;proc=None
    for ph in segments:
        ptype,pflags,offset,vaddr,_,filesz,memsz,_=ph
        if offset+filesz>len(b) or filesz>memsz:raise RuntimeError('Invalid segment bounds')
        if ptype==1 and vaddr<=entry and entry+8<=vaddr+filesz:entry_file=offset+entry-vaddr
        if ptype==0x60000001 and filesz>=32:proc=bytes(b[offset:offset+filesz])
    if entry_file is None or proc is None:raise RuntimeError('Missing compact entry or process parameters')
    code,toc=struct.unpack_from('>II',b,entry_file)
    if not any(ph[0]==1 and ph[1]&1 and ph[3]<=code<ph[3]+ph[5] for ph in segments):raise RuntimeError('Entry code outside executable segment')
    if struct.unpack_from('>I',proc,4)[0]!=0x13bcc5f6:raise RuntimeError('Bad process parameter magic')
    primary_exit = trace_entry(elf)
    fallback_exit = trace_entry(elf, refuse_process_exit=True)
    if [c['number'] for c in primary_exit['calls']] != [3] or [c['number'] for c in fallback_exit['calls']] != [3,22]:
        raise RuntimeError('Compiled entry does not reach the documented process exits')
    run([sdk/'bin/make_self_npdrm',elf,stage/'USRDIR/EBOOT.BIN',CONTENT_ID],env)
    # Use the SDK's SFO writer with explicit build identity and target version.
    xml=ET.parse(sdk/'bin/sfo.xml')
    values={'TITLE':'bibilabu PS3 Boot Probe','TITLE_ID':ID,'APP_VER':'00.20',
            'PS3_SYSTEM_VER':'04.9300','PARENTAL_LEVEL':'1'}
    for value in xml.getroot().findall('value'):
        if value.get('name') in values:value.text=values[value.get('name')]
    sfo_xml=build/'PARAM.SFO.xml';xml.write(sfo_xml,encoding='utf-8',xml_declaration=True)
    run([sdk/'bin/sfo','-f',sfo_xml,stage/'PARAM.SFO'],env)
    icon=PORT/'assets/ICON0.PNG'
    png=icon.read_bytes();width,height=struct.unpack_from('>II',png,16)
    if width>320 or height>176:raise RuntimeError('Icon dimensions exceed PS3 limits')
    shutil.copyfile(icon,stage/'ICON0.PNG')
    debug_pkg=build/'probe.debug.pkg'
    package=dist/('bibilabu-PS3-boot-probe-'+VERSION+'.pkg')
    run([sdk/'bin/pkg','--contentid='+CONTENT_ID,stage,debug_pkg],env)
    shutil.copyfile(debug_pkg,package)
    run([sdk/'bin/package_finalize',package],env)
    output=package.read_bytes()
    if output[:8]!=b'\x7fPKG\x80\0\0\x01' or struct.unpack_from('>Q',output,24)[0]!=len(output):raise RuntimeError('Invalid final package')
    manifest={'artifact':'startup probe only','version':VERSION,'ps3_hardware_tested':False,'graphics':False,'media_playback':False,
              'crt_or_import_dependencies':unresolved,'entry_descriptor':{'address':entry,'code':code,'toc':toc},
              'exit_checks':{'primary':primary_exit,'fallback':fallback_exit},
              'process_param_magic':'13bcc5f6','package_bytes':len(output),
              'sha256':hashlib.sha256(output).hexdigest(),'upstream':json.loads((PORT/'UPSTREAM.json').read_text())}
    (dist/'boot-probe-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (dist/'SHA256SUMS.txt').write_text(manifest['sha256']+'  '+package.name+'\n')
    shutil.copyfile(PORT/'BOOT_TEST.md',dist/'BOOT_TEST.md')
    bundle=dist/('bibilabu-PS3-boot-probe-'+VERSION+'-USB.zip')
    with zipfile.ZipFile(bundle,'w',zipfile.ZIP_DEFLATED) as z:
        for p in [package,dist/'BOOT_TEST.md',dist/'SHA256SUMS.txt',dist/'boot-probe-manifest.json']:
            z.write(p,p.name)
    with zipfile.ZipFile(bundle) as z:
        if z.testzip() is not None:raise RuntimeError('Bundle verification failed')
    print(json.dumps(manifest,indent=2))


if __name__=='__main__':main()
