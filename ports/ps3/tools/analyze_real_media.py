#!/usr/bin/env python3
"""Analyze the bounded Bilibili DASH MP4 snapshots captured by probe_real_media.

Reads only de-identified initialization/index/fragment-metadata bytes (no media
payload, no signed URLs) and writes a reproducible parameter record.
"""
import json
import struct
import sys
from pathlib import Path

FCC = lambda s: s.encode('latin1')

class Box:
    def __init__(self, typ, start, size, header, data):
        self.type, self.start, self.size, self.header, self.data = typ, start, size, header, data
    @property
    def end(self): return self.start + self.size
    @property
    def payload(self): return self.data[self.header:self.size]

def parse_boxes(data, start=0, end=None, depth=0, maxdepth=6):
    if end is None: end = len(data)
    out = []
    at = start
    while at + 8 <= end:
        size, typ = struct.unpack_from('>I4s', data, at)
        header = 8
        if size == 1:
            if at + 16 > end: break
            size = struct.unpack_from('>Q', data, at + 8)[0]
            header = 16
        elif size == 0:
            size = end - at
        if size < header or at + size > end: break
        b = Box(typ.decode('latin1'), at, size, header, data[at:at+size])
        out.append(b)
        if depth < maxdepth and typ in CONTAINERS:
            b.children = parse_boxes(data, at + header, at + size, depth + 1, maxdepth)
        at += size
    return out

CONTAINERS = {FCC(x) for x in ('moov','trak','mdia','minf','stbl','moof','traf','mvex','edts','dinf','udta','meta','stsd')}

def find(boxes, typ):
    return [b for b in boxes if b.type == typ]

def u8(p, at): return p[at]
def u16(p, at): return struct.unpack_from('>H', p, at)[0]
def u32(p, at): return struct.unpack_from('>I', p, at)[0]
def u64(p, at): return struct.unpack_from('>Q', p, at)[0]

class Bits:
    def __init__(self, data, offset=0, limit=None):
        self.d = data; self.pos = (offset or 0) * 8
        self.limit = (limit if limit is not None else len(data)) * 8
    def u(self, n):
        if self.pos + n > self.limit: raise ValueError('EOF in bitstream')
        v = 0
        for _ in range(n):
            byte = self.d[self.pos >> 3]
            v = (v << 1) | ((byte >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v
    def ue(self):
        z = 0
        while True:
            if self.pos >= self.limit: raise ValueError('EOF in ue')
            if self.u(1): break
            z += 1
            if z > 31: raise ValueError('ue overflow')
        return (1 << z) - 1 + self.u(z)
    def se(self):
        v = self.ue()
        return (v + 1) // 2 if v & 1 else -(v // 2)

def rbsp(nal):
    out = bytearray(); zeros = 0
    for x in nal[1:]:
        if zeros >= 2 and x == 3:
            zeros = 0
            continue
        out.append(x)
        zeros = zeros + 1 if x == 0 else 0
    return bytes(out)

def parse_sps(nal):
    p = rbsp(nal)
    b = Bits(p)
    r = {'nal_type': nal[0] & 31, 'nal_ref_idc': (nal[0] >> 5) & 3}
    r['profile_idc'] = b.u(8)
    r['constraint_flags'] = b.u(8)
    r['level_idc'] = b.u(8)
    r['sps_id'] = b.ue()
    high = {100,110,122,244,44,83,86,118,128,138,139,134,135}
    r['chroma_format_idc'] = 1; r['bit_depth_luma'] = 8; r['bit_depth_chroma'] = 8
    r['separate_colour_plane'] = 0
    if r['profile_idc'] in high:
        r['chroma_format_idc'] = b.ue()
        if r['chroma_format_idc'] == 3: r['separate_colour_plane'] = b.u(1)
        r['bit_depth_luma'] = 8 + b.ue()
        r['bit_depth_chroma'] = 8 + b.ue()
        b.u(1)
        if b.u(1):  # seq_scaling_matrix_present
            n = 8 if r['chroma_format_idc'] != 3 else 12
            for i in range(n):
                if b.u(1):
                    size = 16 if i < 6 else 64
                    last, nxt = 8, 8
                    for _ in range(size):
                        if nxt: nxt = (last + b.se() + 256) % 256
                        last = nxt if nxt else last
    r['log2_max_frame_num_minus4'] = b.ue()
    r['pic_order_cnt_type'] = b.ue()
    if r['pic_order_cnt_type'] == 0:
        r['log2_max_poc_lsb_minus4'] = b.ue()
    elif r['pic_order_cnt_type'] == 1:
        r['delta_pic_order_always_zero'] = b.u(1)
        r['offset_for_non_ref_pic'] = b.se()
        r['offset_for_top_to_bottom_field'] = b.se()
        n = b.ue()
        for _ in range(n): b.se()
    r['max_num_ref_frames'] = b.ue()
    r['gaps_in_frame_num_value_allowed'] = b.u(1)
    wm, hm = b.ue(), b.ue()
    r['frame_mbs_only'] = b.u(1)
    if not r['frame_mbs_only']: r['mb_adaptive_frame_field'] = b.u(1)
    r['direct_8x8_inference'] = b.u(1)
    r['frame_cropping'] = b.u(1)
    r['crop_left'] = r['crop_right'] = r['crop_top'] = r['crop_bottom'] = 0
    if r['frame_cropping']:
        for k in ('crop_left','crop_right','crop_top','crop_bottom'): r[k] = b.ue()
    r['vui_present'] = b.u(1)
    r['vui'] = {}
    if r['vui_present']:
        v = r['vui']
        if b.u(1):
            v['aspect_ratio_idc'] = b.u(8)
            if v['aspect_ratio_idc'] == 255:
                v['sar_width'] = b.u(16); v['sar_height'] = b.u(16)
        if b.u(1): v['overscan_appropriate'] = b.u(1)
        if b.u(1):
            v['video_format'] = b.u(3); v['video_full_range'] = b.u(1)
            if b.u(1):
                v['colour_primaries'] = b.u(8); v['transfer_characteristics'] = b.u(8); v['matrix_coefficients'] = b.u(8)
        if b.u(1):
            v['chroma_sample_loc_type_top'] = b.ue(); v['chroma_sample_loc_type_bottom'] = b.ue()
        if b.u(1):
            v['num_units_in_tick'] = b.u(32); v['time_scale'] = b.u(32); v['fixed_frame_rate'] = b.u(1)
            if v['num_units_in_tick'] and v['time_scale']:
                v['fps'] = round(v['time_scale'] / (2 * v['num_units_in_tick']), 6)
        def hrd():
            count = b.ue(); b.u(4); b.u(4)
            for _ in range(count+1):
                b.ue(); b.ue(); b.u(1)
            b.u(5); b.u(5); b.u(5); b.u(5)
        nal_hrd = b.u(1); vcl_hrd = b.u(1)
        if nal_hrd: hrd()
        if vcl_hrd: hrd()
        if nal_hrd or vcl_hrd: v['pic_struct_present'] = b.u(1)
        if b.u(1):  # bitstream restriction
            v['motion_vectors_over_pic_boundaries'] = b.u(1)
            v['max_bytes_per_pic_denom'] = b.ue(); v['max_bits_per_mb_denom'] = b.ue()
            v['log2_max_mv_length_h'] = b.ue(); v['log2_max_mv_length_v'] = b.ue()
            v['max_num_reorder_frames'] = b.ue(); v['max_dec_frame_buffering'] = b.ue()
        else:
            v['bitstream_restriction_present'] = 0
    coded_w = (wm + 1) * 16
    coded_h = (hm + 1) * 16 * (2 - r['frame_mbs_only'])
    sub_w = 2 if r['chroma_format_idc'] in (1,2) else 1
    sub_h = 2 if r['chroma_format_idc'] == 1 else 1
    crop_x = sub_w * (r['crop_left'] + r['crop_right'])
    crop_y = sub_h * (r['crop_top'] + r['crop_bottom']) * (2 - r['frame_mbs_only'])
    r['coded_width'], r['coded_height'] = coded_w, coded_h
    r['display_width'], r['display_height'] = coded_w - crop_x, coded_h - crop_y
    r['rbsp_bytes'], r['bits_consumed'] = len(p), b.pos
    return r

RATES = [96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350]

def parse_asc(p):
    b = Bits(p)
    aot = b.u(5)
    if aot == 31: aot = 32 + b.u(6)
    r = {'audio_object_type': aot}
    freq = b.u(4)
    if freq == 15: r['sample_rate'] = b.u(24)
    else: r['sample_rate'] = RATES[freq] if freq < len(RATES) else -1
    r['sampling_frequency_index'] = freq
    r['channel_configuration'] = b.u(4)
    if aot in (5,29):
        r['extension_audio_object_type'] = 5
        r['sbr'] = 1
        f2 = b.u(4)
        r['extension_sample_rate'] = b.u(24) if f2 == 15 else (RATES[f2] if f2 < len(RATES) else -1)
        aot = b.u(5)
        if aot == 31: aot = 32 + b.u(6)
        r['core_audio_object_type'] = aot
        if aot == 22: r['extension_channel_configuration'] = b.u(4)
    if aot in (1,2,3,4,6,7,17,19,20,21,22,23):
        r['frame_length_flag'] = b.u(1)
        r['depends_on_core_coder'] = b.u(1)
        if r['depends_on_core_coder']: r['core_coder_delay'] = b.u(14)
        r['extension_flag'] = b.u(1)
    r['bits_consumed'] = b.pos
    return r

def parse_avcc(p):
    r = {'configuration_version': p[0], 'avc_profile_indication': p[1], 'profile_compatibility': p[2], 'avc_level_indication': p[3]}
    r['length_size_minus_one'] = p[4] & 3
    r['num_sps'] = p[5] & 31
    at = 6; sps, pps = [], []
    for _ in range(r['num_sps']):
        n = u16(p, at); at += 2
        sps.append(p[at:at+n]); at += n
    r['num_pps'] = p[at]; at += 1
    for _ in range(r['num_pps']):
        n = u16(p, at); at += 2
        pps.append(p[at:at+n]); at += n
    r['sps_hex'] = [s.hex() for s in sps]
    r['pps_hex'] = [s.hex() for s in pps]
    r['sps_parsed'] = [parse_sps(s) for s in sps]
    return r

def descriptors(p):
    at = 0
    while at < len(p):
        tag = p[at]; at += 1
        length = 0
        for _ in range(4):
            if at >= len(p): return
            x = p[at]; at += 1
            length = (length << 7) | (x & 127)
            if not (x & 128): break
        body = p[at:at+length]; at += length
        yield tag, body

def parse_esds(p):
    r = {}
    def walk(body, depth=0):
        if depth > 6: return
        for tag, d in descriptors(body):
            if tag == 3 and len(d) >= 3: walk(d[3:], depth+1)
            elif tag == 4 and len(d) >= 13:
                r['object_type_indication'] = d[0]
                r['stream_type'] = d[1] >> 2
                r['buffer_size_db'] = ((d[1] & 3) << 16) | (d[2] << 8) | d[3]
                r['max_bitrate'] = u32(d, 4); r['avg_bitrate'] = u32(d, 8)
                walk(d[13:], depth+1)
            elif tag == 5:
                r['asc_hex'] = d[:16].hex()
                r['asc'] = parse_asc(d)
    walk(p[4:])  # esds full box: version/flags
    return r

def parse_stsd(entry, kind):
    r = {'entry_type': entry.type, 'data_reference_index': u16(entry.payload, 6)}
    p = entry.payload
    if kind == 'video':
        r['width'] = u16(p, 24); r['height'] = u16(p, 26)
        r['horiz_resolution'] = u32(p, 28); r['vert_resolution'] = u32(p, 32)
        r['frame_count'] = u16(p, 40)
        r['depth'] = u16(p, 74)
        children = parse_boxes(p, 78, len(p), 1, 2)
        for c in children:
            if c.type == 'avcC':
                r['avcc'] = parse_avcc(c.payload)
    else:
        r['channelcount'] = u16(p, 16); r['samplesize'] = u16(p, 18)
        r['sample_rate_fixed'] = u32(p, 24) / 65536.0
        children = parse_boxes(p, 28, len(p), 1, 2)
        for c in children:
            if c.type == 'esds': r['esds'] = parse_esds(c.payload)
    r['child_boxes'] = [{'type':c.type,'size':c.size} for c in children]
    return r

def parse_moov(data, boxes, kind):
    r = {}
    for moov in find(boxes, 'moov'):
        for mvhd in find(moov.children, 'mvhd'):
            v = u8(mvhd.payload, 0); off = 20 if v else 12
            r['mvhd_timescale'] = u32(mvhd.payload, off)
            r['mvhd_duration'] = u32(mvhd.payload, off+4) if not v else u64(mvhd.payload, off+4)
        for trex in [c for m in find(moov.children,'mvex') for c in find(m.children,'trex')]:
            r['trex'] = {'track_ID':u32(trex.payload,4),'default_sample_description_index':u32(trex.payload,8),
                         'default_sample_duration':u32(trex.payload,12),'default_sample_size':u32(trex.payload,16),
                         'default_sample_flags':u32(trex.payload,20)}
        for trak in find(moov.children, 'trak'):
            for tkhd in find(trak.children, 'tkhd'):
                v = u8(tkhd.payload,0); r['tkhd_track_id'] = u32(tkhd.payload, 20 if v else 12)
                r['tkhd_duration'] = u32(tkhd.payload, 28 if v else 20)
                r['tkhd_width'] = u32(tkhd.payload, 76 if v else 68)/65536.0
                r['tkhd_height'] = u32(tkhd.payload, 80 if v else 72)/65536.0
            for edts in find(trak.children,'edts'):
                for elst in find(edts.children,'elst'):
                    v = u8(elst.payload,0); count = u32(elst.payload,4); at = 8
                    items = []
                    for _ in range(min(count,8)):
                        if v: seg, media = u64(elst.payload,at), struct.unpack_from('>q',elst.payload,at+8)[0]; at += 16
                        else: seg, media = u32(elst.payload,at), struct.unpack_from('>i',elst.payload,at+4)[0]; at += 8
                        rate_i, rate_f = u16(elst.payload,at),u16(elst.payload,at+2); at += 4
                        items.append({'segment_duration':seg,'media_time':media,'media_rate':[rate_i,rate_f]})
                    r['elst'] = {'version':v,'entry_count':count,'entries':items}
            for mdia in find(trak.children,'mdia'):
                for mdhd in find(mdia.children,'mdhd'):
                    v = u8(mdhd.payload,0); off = 20 if v else 12
                    r['mdhd_timescale'] = u32(mdhd.payload,off)
                    r['mdhd_duration'] = u64(mdhd.payload,off+4) if v else u32(mdhd.payload,off+4)
                    r['mdhd_language'] = u16(mdhd.payload,off+(12 if v else 8))
                for hdlr in find(mdia.children,'hdlr'):
                    r['hdlr_handler_type'] = hdlr.payload[8:12].decode('latin1')
                for minf in find(mdia.children,'minf'):
                    for stbl in find(minf.children,'stbl'):
                        for stsd in find(stbl.children,'stsd'):
                            r['stsd_entry_count'] = u32(stsd.payload,4)
                            entries = [b for b in stsd.children if b.type not in ('',)]
                            # stsd children were parsed starting after count; rebuild manually
                            boxdata = stsd.data
                            at = stsd.header + 8
                            for i in range(min(r['stsd_entry_count'],4)):
                                if at+8 > len(boxdata): break
                                sz, typ = struct.unpack_from('>I4s',boxdata,at)
                                sub = parse_boxes(boxdata,at,at+sz,1,3)
                                if sub:
                                    r['stsd'] = parse_stsd(sub[0],kind)
                                at += sz
                        for n in ('stts','ctts','stsc','stsz','stco','co64','stss','sdtp','sgpd','sbgp'):
                            hit = find(stbl.children,n)
                            if hit: r.setdefault('stbl_boxes',[]).append(n)
    return r

def parse_sidx(b):
    p = b.payload; v = u8(p,0)
    r = {'version':v,'reference_ID':u32(p,4),'timescale':u32(p,8)}
    off = 12
    if not v: r['earliest_presentation_time'] = u32(p,off); r['first_offset'] = u32(p,off+4); off += 8
    else: r['earliest_presentation_time'] = u64(p,off); r['first_offset'] = u64(p,off+8); off += 16
    r['reserved'] = u16(p,off); off += 2
    count = u16(p,off); off += 2
    r['reference_count'] = count
    refs = []
    for _ in range(count):
        word = u32(p,off); duration = u32(p,off+4); sap = u32(p,off+8); off += 12
        refs.append({'reference_type':word>>31,'referenced_size':word&0x7fffffff,
                     'subsegment_duration':duration,'starts_with_SAP':sap>>31,
                     'SAP_type':(sap>>28)&7,'SAP_delta_time':sap&0xfffffff})
    r['references'] = refs
    r['covered_bytes'] = r['first_offset'] + sum(x['referenced_size'] for x in refs)
    r['covered_duration_ticks'] = sum(x['subsegment_duration'] for x in refs)
    return r

def parse_moof(b, meta):
    p = b.payload
    r = {'offset':b.start,'size':b.size,'sequence':None,'tfhd':{},'tfdt':None,'trun':[]}
    for child in b.children:
        if child.type == 'mfhd': r['sequence'] = u32(child.payload,4)
        elif child.type == 'traf':
            for t in child.children:
                if t.type == 'tfhd':
                    flags = u32(t.payload,0)&0xffffff; at = 4
                    f = {'version':u8(t.payload,0),'flags':hex(flags),'track_ID':u32(t.payload,at)}; at += 4
                    if flags & 1: f['base_data_offset'] = u64(t.payload,at); at += 8
                    if flags & 2: f['sample_description_index'] = u32(t.payload,at); at += 4
                    if flags & 8: f['default_sample_duration'] = u32(t.payload,at); at += 4
                    if flags & 16: f['default_sample_size'] = u32(t.payload,at); at += 4
                    if flags & 32: f['default_sample_flags'] = u32(t.payload,at); at += 4
                    r['tfhd'] = f
                elif t.type == 'tfdt':
                    v = u8(t.payload,0)
                    r['tfdt'] = u64(t.payload,4) if v else u32(t.payload,4)
                elif t.type == 'trun':
                    flags = u32(t.payload,0)&0xffffff; count = u32(t.payload,4); at = 8
                    tr = {'version':u8(t.payload,0),'flags':hex(flags),'sample_count':count}
                    if flags & 1: tr['data_offset'] = struct.unpack_from('>i',t.payload,at)[0]; at += 4
                    if flags & 4: tr['first_sample_flags'] = u32(t.payload,at); at += 4
                    fields = []
                    dts = r['tfdt'] if r['tfdt'] is not None else 0
                    for i in range(count):
                        s = {}
                        if flags & 0x100: s['duration'] = u32(t.payload,at); at += 4
                        if flags & 0x200: s['size'] = u32(t.payload,at); at += 4
                        if flags & 0x400: s['flags'] = u32(t.payload,at); at += 4
                        if flags & 0x800:
                            raw = u32(t.payload,at); at += 4
                            s['cto'] = struct.unpack_from('>i',t.payload,at-4)[0] if u8(t.payload,0) else raw
                        s.setdefault('duration',r['tfhd'].get('default_sample_duration'))
                        s.setdefault('size',r['tfhd'].get('default_sample_size'))
                        fields.append(s)
                    tr['fields_hex'] = t.payload[at:at+min(16,len(t.payload)-at)].hex()
                    tr['trailing_bytes'] = len(t.payload)-at
                    tr['samples'] = fields[:3]
                    tr['duration_total'] = sum(s.get('duration') or 0 for s in fields)
                    tr['size_total'] = sum(s.get('size') or 0 for s in fields)
                    tr['max_sample_size'] = max((s.get('size') or 0) for s in fields)
                    tr['min_duration'] = min((s.get('duration') or 0) for s in fields)
                    tr['max_duration'] = max((s.get('duration') or 0) for s in fields)
                    tr['cto_nonzero'] = sum(1 for s in fields if s.get('cto'))
                    tr['cto_values'] = sorted({s.get('cto') for s in fields if s.get('cto') is not None})[:12]
                    tr['cto_max_ticks'] = max((s.get('cto') or 0) for s in fields)
                    tr['cto_max_frames'] = round(tr['cto_max_ticks'] / (tr['min_duration'] or 1), 2)
                    r['trun'].append(tr)
    return r

def inspect_samples(data, moof_info, avcc, audio=False):
    """Inspect first mdat payload NAL/AAC layout using trun sizes/offsets."""
    r = {}
    tr = moof_info['trun'][0] if moof_info['trun'] else None
    if not tr: return r
    # tfhd default-base-is-moof: trun data_offset is relative to the moof start.
    base = moof_info['offset']
    data_offset = tr.get('data_offset',0)
    sample_start = base + data_offset
    size = tr['samples'][0].get('size') if tr['samples'] else None
    if size is None or sample_start + size > len(data): return r
    p = data[sample_start:sample_start+size]
    if audio:
        r['first_au_bytes'] = len(p)
        r['first_au_prefix_hex'] = p[:16].hex()
    else:
        n = avcc['length_size_minus_one'] + 1
        at = 0; nals = []
        while at + n <= len(p):
            length = int.from_bytes(p[at:at+n],'big'); at += n
            if length > len(p)-at: break
            nal = p[at:at+length]; at += length
            nals.append({'nal_type':nal[0]&31,'bytes':length,'prefix_hex':nal[:6].hex()})
        r['first_au_nal_length_bytes'] = n
        r['first_au_nals'] = nals[:16]
        r['first_au_nal_count'] = len(nals)
        r['first_au_total_bytes'] = len(p)
    return r

def main():
    if len(sys.argv) < 3:
        print('usage: analyze_real_media.py <probe-dir> <output-json>'); return 2
    d = Path(sys.argv[1]); out = Path(sys.argv[2])
    probe = json.loads((d/'probe.json').read_text())
    report = {'source':'probe_real_media.c fresh HTTPS/WBI + bounded Range', 'probe':probe, 'tracks':{}}
    for kind in ('video','audio'):
        data = (d/f'{kind}_head.bin').read_bytes()
        boxes = parse_boxes(data)
        t = {'top_level_boxes':[{'type':b.type,'offset':b.start,'size':b.size} for b in boxes[:20]], 'total_top_level':len(boxes)}
        t['moov'] = parse_moov(data, boxes, kind)
        for sidx in find(boxes,'sidx'): t['sidx'] = parse_sidx(sidx)
        moofs = []
        for b in boxes:
            if b.type == 'moof': moofs.append(parse_moof(b,t))
        t['moof_count_in_head'] = len(moofs)
        t['moofs'] = moofs[:3]
        t['trun_sample_counts'] = [x['sample_count'] for m in moofs for x in m['trun']]
        t['max_samples_per_fragment'] = max(t['trun_sample_counts']) if t['trun_sample_counts'] else 0
        t['max_sample_size_in_head'] = max([x['max_sample_size'] for m in moofs for x in m['trun']] or [0])
        t['cto_nonzero_total'] = sum(x['cto_nonzero'] for m in moofs for x in m['trun'])
        if 'stsd' in t['moov'] and 'avcc' in t['moov']['stsd']:
            t['sample_layout'] = inspect_samples(data, moofs[0], t['moov']['stsd']['avcc']) if moofs else {}
        elif moofs:
            t['sample_layout'] = inspect_samples(data, moofs[0], None, audio=True)
        # Track end-of-file structure from the tail snapshot (scan for 'moof').
        tail = (d/f'{kind}_tail.bin').read_bytes()
        tail_boxes = []
        at = 0
        while True:
            i = tail.find(b'moof',at)
            if i < 4: break
            at = i + 4
            if i-4+8 > len(tail): break
            size = u32(tail,i-4)
            if 16 <= size <= len(tail)-(i-4):
                tail_boxes.append({'tail_offset':i-4,'size':size})
                at = i-4+size
        t['tail_moof_markers'] = tail_boxes[:8]
        t['tail_moof_count'] = len(tail_boxes)
        report['tracks'][kind] = t
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n')
    print(json.dumps({'output':str(out),'video_sps':report['tracks']['video'].get('moov',{}).get('stsd',{}).get('avcc',{}).get('sps_parsed',[]),
                      'audio_asc':report['tracks']['audio'].get('moov',{}).get('stsd',{}).get('esds',{}).get('asc',{})},indent=2,ensure_ascii=False))
    return 0

if __name__ == '__main__': sys.exit(main())
