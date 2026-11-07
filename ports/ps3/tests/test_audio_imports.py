#!/usr/bin/env python3
"""Regression checks for compiler-sensitive PPU32 import/callback metadata."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import unittest
PORT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(PORT/'tools'))
from build_platform_probe import validate_adec_imports,validate_decoder_callback
from build_boot_probe import elf_segments

class AudioImports(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build=Path(os.environ.get('WILI_PROBE_BUILD_DIR',PORT/'build/video'))
        sdk=Path(os.environ.get('PS3DEV','/opt/wiliwili-ps3/ps3dev'))
        elf=build/'platform.patched.elf';cls.data=elf.read_bytes()
        cls.nm=subprocess.check_output([str(sdk/'ppu/bin/ppu-nm'),'-n',str(elf)],text=True)
        cls.symbols={l.split()[-1]:int(l.split()[0],16) for l in cls.nm.splitlines() if len(l.split())==3}
    def off(self,address):
        for ph in elf_segments(self.data):
            if ph[0]==1 and ph[3]<=address<ph[3]+ph[5]:return ph[2]+address-ph[3]
        raise AssertionError('unmapped fixture address')
    def test_live_binary_tables_and_both_callbacks(self):
        self.assertEqual(validate_adec_imports(self.data,self.nm)['functions'],16)
        for name in ['aac_callback','audio_worker']:
            self.assertTrue(validate_decoder_callback(self.data,self.nm,name)['compact_descriptor_verified'])
    def test_reject_wrong_nid_and_misdirected_stub(self):
        for symbol in ['wiliAdecQueryAttr_fnid','wiliAdecQueryAttr_stub','wiliCellAdecQueryAttr_fnid','wiliCellAdecQueryAttr_stub']:
            bad=bytearray(self.data);offset=self.off(self.symbols[symbol]);bad[offset+3]^=1
            with self.assertRaises(RuntimeError):validate_adec_imports(bad,self.nm)
    def test_reject_callback_compact_descriptor_mismatch(self):
        for name in ['aac_callback','audio_worker']:
            bad=bytearray(self.data);bad[self.off(self.symbols[name])+23]^=1
            with self.assertRaises(RuntimeError):validate_decoder_callback(bad,self.nm,name)

if __name__=='__main__':unittest.main()
