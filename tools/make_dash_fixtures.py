#!/usr/bin/env python3
"""Build synthetic single-fragment DASH MP4 regression fixtures.

Only the real initialization / sidx / moof metadata boxes are reused. The
mdat payload is deterministic dummy data with the same NAL-length structure
and the same trun sample sizes as the fresh Bilibili stream, so no copyrighted
media bytes and no signed URLs are stored. sidx is rewritten to describe the
single fragment, making the fixture size self-consistent.
"""
import struct
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / 'tests' / 'fixtures'

def boxes(data, start, end):
    at = start
    while at + 8 <= end:
        size, typ = struct.unpack_from('>I4s', data, at)
        if size == 0: size = end - at
        if size < 8 or at + size > end: break
        yield typ.decode('latin1'), at, size
        at += size

def trun_samples(moof):
    seq = None; dts = None; samples = []
    def walk(start, end):
        nonlocal seq, dts
        for typ, at, size in boxes(moof, start, end):
            p = at + 8
            if typ == 'mfhd': seq = struct.unpack_from('>I', moof, p + 4)[0]
            elif typ == 'tfhd':
                flags = struct.unpack_from('>I', moof, p)[0] & 0xffffff
                q = p + 8
                if flags & 1: q += 8
                if flags & 2: q += 4
                default_duration = struct.unpack_from('>I', moof, q)[0] if flags & 8 else 0
                q += 4 if flags & 8 else 0
                default_size = struct.unpack_from('>I', moof, q)[0] if flags & 16 else 0
                q += 4 if flags & 16 else 0
                default_flags = struct.unpack_from('>I', moof, q)[0] if flags & 32 else 0
                yield ('tfhd', default_duration, default_size, default_flags)
            elif typ == 'tfdt':
                v = moof[p]
                dts = struct.unpack_from('>Q' if v else '>I', moof, p + 4)[0]
            elif typ == 'trun':
                version = moof[p]; flags = struct.unpack_from('>I', moof, p)[0] & 0xffffff
                count = struct.unpack_from('>I', moof, p + 4)[0]
                q = p + 8
                data_offset = struct.unpack_from('>i', moof, q)[0] if flags & 1 else 0
                q += 4 if flags & 1 else 0
                first_flags = struct.unpack_from('>I', moof, q)[0] if flags & 4 else 0
                q += 4 if flags & 4 else 0
                stride = 4 * sum(bool(flags & m) for m in (0x100, 0x200, 0x400, 0x800))
                samples.append((version, flags, count, q, stride, data_offset, first_flags, at, size))
            elif typ in ('moof', 'traf'):
                yield from walk(at + 8, at + size)
    defaults = [x for x in walk(0, len(moof)) if isinstance(x, tuple) and x[0] == 'tfhd']
    td, ts, tf = defaults[0][1:] if defaults else (1024, 0, 0)
    out = []
    for (version, flags, count, q, stride, data_offset, first_flags, at, size) in samples:
        assert q + count * stride <= at + size
        for i in range(count):
            off = q + i * stride; dur, sz, fl = td, ts, tf; cto = 0
            if flags & 0x100: dur = struct.unpack_from('>I', moof, off)[0]; off += 4
            if flags & 0x200: sz = struct.unpack_from('>I', moof, off)[0]; off += 4
            if flags & 0x400: fl = struct.unpack_from('>I', moof, off)[0]; off += 4
            if flags & 0x800:
                raw = struct.unpack_from('>I', moof, off)[0]; off += 4
                cto = raw - (1 << 32) if version and raw >= (1 << 31) else raw
            if i == 0 and flags & 4: fl = first_flags
            out.append({'duration': dur, 'size': sz, 'flags': fl, 'cto': cto})
    return seq, dts, samples[0][5] if samples else 0, out

def patch_sidx(sidx, mdat_size, duration):
    b = bytearray(sidx)
    assert struct.unpack_from('>I', b, 4)[0] == 0x73696478
    struct.pack_into('>H', b, 30, 1)                 # reference_count = 1
    struct.pack_into('>I', b, 32, mdat_size)         # referenced_size
    struct.pack_into('>I', b, 36, duration)          # subsegment_duration
    struct.pack_into('>I', b, 40, 0x80000000)        # starts_with_SAP, SAP_type=1
    return bytes(b)

def video_mdat(samples):
    payload = bytearray()
    for i, s in enumerate(samples):
        n = s['size']
        assert n >= 8, n
        if i == 0 and n >= 16:
            sei = min(48, n - 12)
            vcl = n - 8 - sei
            payload += struct.pack('>I', sei) + bytes([0x06]) + bytes(((j * 7 + i) & 0xff) for j in range(sei - 1))
            payload += struct.pack('>I', vcl) + bytes([0x65]) + bytes(((j * 13 + i * 3) & 0xff) for j in range(vcl - 1))
        else:
            vcl = n - 4
            payload += struct.pack('>I', vcl) + bytes([0x65 if i == 0 else 0x41]) + bytes(((j * 13 + i * 3) & 0xff) for j in range(vcl - 1))
    return struct.pack('>I', 8 + len(payload)) + b'mdat' + bytes(payload)

def audio_mdat(samples):
    payload = bytearray()
    for i, s in enumerate(samples):
        payload += bytes(((j * 11 + i) & 0xff) for j in range(s['size']))
    return struct.pack('>I', 8 + len(payload)) + b'mdat' + bytes(payload)

def build(kind, audio):
    init = (SRC / f'bili_{kind}_init.bin').read_bytes()
    sidx = (SRC / f'bili_{kind}_sidx.bin').read_bytes()
    moof = (SRC / f'bili_{kind}_moof1.bin').read_bytes()
    seq, dts, data_offset, samples = trun_samples(moof)
    print(kind, 'sequence', seq, 'tfdt', dts, 'data_offset', data_offset, 'samples', len(samples),
          'size range', min(s['size'] for s in samples), max(s['size'] for s in samples))
    mdat = audio_mdat(samples) if audio else video_mdat(samples)
    duration = sum(s['duration'] for s in samples)
    # trun data_offset is relative to moof start; moof follows sidx immediately.
    assert data_offset == len(moof) + 8, (data_offset, len(moof))
    data = init + patch_sidx(sidx, len(moof) + len(mdat), duration) + moof + mdat
    target = SRC / f'bili_{kind}_single.bin'
    target.write_bytes(data)
    print('wrote', target.name, len(data), 'bytes; mdat', len(mdat), 'duration_ticks', duration)

if __name__ == '__main__':
    build('video', False)
    build('audio', True)
