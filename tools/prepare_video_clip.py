#!/usr/bin/env python3
"""Generate and validate the fixed 30-second split-fMP4 streaming fixture."""
import array
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import zlib
PORT=Path(__file__).resolve().parents[1]

def run(args):subprocess.run(list(map(str,args)),check=True)
def probe(path):
    return json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-show_entries','stream=codec_name,profile,width,height,level,nb_read_frames,sample_rate,channels','-of','json',str(path)],text=True))['streams'][0]
def main():
    build=PORT/'build/media';build.mkdir(parents=True,exist_ok=True)
    video=build/'WILI_VIDEO.MP4';audio=build/'WILI_AUDIO.MP4'
    run(['ffmpeg','-v','error','-f','lavfi','-i','testsrc2=size=848x480:rate=30','-vf',r'drawbox=x=0:y=0:w=iw:h=ih:color=white:t=fill:enable=lt(mod(t\,1)\,0.12)',
         '-t','30','-c:v','libx264','-profile:v','baseline','-level:v','3.1','-pix_fmt','yuv420p','-refs','1','-bf','0','-g','30','-x264-params','aud=1:threads=1','-an',
         '-movflags','+empty_moov+default_base_moof+frag_keyframe','-f','mp4','-y',video])
    run(['ffmpeg','-v','error','-f','lavfi','-i',r'aevalsrc=0.15*sin(2*PI*880*t)*lt(mod(t\,1)\,0.12):s=48000:d=30',
         '-ac','2','-ar','48000','-c:a','aac','-profile:a','aac_low','-b:a','128k','-movflags','+empty_moov+default_base_moof','-frag_duration','1000000','-f','mp4','-y',audio])
    vp,ap=probe(video),probe(audio)
    if (vp['width'],vp['height'],vp['level'],int(vp['nb_read_frames']))!=(848,480,31,900) or ap['codec_name']!='aac' or ap['sample_rate']!='48000' or ap['channels']!=2:raise RuntimeError('Unexpected generated media')
    test=build/'fmp4_test'
    run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(PORT/'include'),
         PORT/'src/fmp4_stream.c',PORT/'src/avc_test_stream.c',PORT/'src/aac_test_stream.c',PORT/'tests/fmp4_tests.c','-o',test])
    avc=build/'converted.h264';aac=build/'converted.aac'
    run([test,video,audio,avc,aac])
    run(['ffmpeg','-v','error','-i',avc,'-f','null','-'])
    if int(probe(avc)['nb_read_frames'])!=900:raise RuntimeError('Converted video frame mismatch')
    raw=subprocess.check_output(['ffmpeg','-v','error','-i',str(aac),'-f','f32le','-acodec','pcm_f32le','-'])
    pcm=array.array('f');pcm.frombytes(raw)
    if sys.byteorder!='little':pcm.byteswap()
    if len(pcm)<(1440000+1024)*2 or any(not math.isfinite(x) or abs(x)>1 for x in pcm):raise RuntimeError('Invalid reference audio')
    for second in range(30):
        at=(1024+second*48000)*2
        signal=sum(x*x for x in pcm[at+1024:at+8000]);silence=sum(x*x for x in pcm[at+24000:at+30000])
        if signal<0.1 or silence>signal*.05:raise RuntimeError('Pulse/priming reference failed')
    vb,ab=video.read_bytes(),audio.read_bytes()
    (build/'test_clip_identity.h').write_text('#define WILI_CLIP_FILENAME "WILI_VIDEO.MP4"\n#define WILI_CLIP_WIDTH 848u\n#define WILI_CLIP_HEIGHT 480u\n#define WILI_CLIP_FPS 30u\n#define WILI_CLIP_FRAMES 900u\n#define WILI_CLIP_DURATION_SECONDS 30u\n#define WILI_CLIP_LEVEL 31u\n'+f'#define WILI_CLIP_BYTES {len(vb)}u\n#define WILI_CLIP_CRC32 0x{zlib.crc32(vb):08x}u\n')
    (build/'test_audio_identity.h').write_text('#define WILI_AUDIO_FILENAME "WILI_AUDIO.MP4"\n#define WILI_AUDIO_DURATION_SECONDS 30u\n'+f'#define WILI_AUDIO_BYTES {len(ab)}u\n#define WILI_AUDIO_CRC32 0x{zlib.crc32(ab):08x}u\n#define WILI_AUDIO_AUS {ap["nb_read_frames"]}u\n')
    ar={'file':audio.name,'bytes':len(ab),'sha256':hashlib.sha256(ab).hexdigest(),'aus':int(ap['nb_read_frames']),'rate':48000,'channels':2,'target_samples':1440000,'fixture_encoder_delay_trim':1024,'host_reference_decode':'PASS: 30 pulse/silence windows after fixture trim'}
    report={'generator':'FFmpeg synthetic testsrc2 + 880Hz pulsed AAC','format':'split single-track fragmented MP4','width':848,'height':480,'fps':30,'frames':900,'duration_seconds':30,'audio':True,'file':video.name,'bytes':len(vb),'crc32':f'{zlib.crc32(vb):08x}','sha256':hashlib.sha256(vb).hexdigest(),'ffprobe':vp,'audio_fixture':ar,'host_parser_tests':'PASS actual fMP4, timestamps, 4096 structured mutations/truncation cases under ASan/UBSan; converted AVC/AAC reference decode','stream_memory_contract':'one 64KiB metadata box and at most 128 samples per reader; no whole-file indexes'}
    (build/'audio-manifest.json').write_text(json.dumps(ar,indent=2)+'\n');(build/'clip-manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
