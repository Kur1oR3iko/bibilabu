#!/usr/bin/env python3
"""Check the real PPU ELF repair without running any PS3 kernel functions.

Build the platform probe first. Uses the unmodified link output as its fixture;
checks the actual binary diff and dependency order, not a mock constructor list.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT / 'tools'))
from build_platform_probe import instrument_constructors
from build_boot_probe import elf_segments


class ConstructorRepairTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = Path(os.environ.get('WILI_PROBE_BUILD_DIR', PORT / 'build/platform'))
        cls.original = (cls.build / 'platform.elf').read_bytes()
        sdk = Path(os.environ.get('PS3DEV', '/opt/wiliwili-ps3/ps3dev'))
        cls.nm = subprocess.check_output(
            [str(sdk / 'ppu/bin/ppu-nm'), '-n', str(cls.build / 'platform.elf')], text=True)
        cls.symbols = {l.split()[-1]: int(l.split()[0], 16)
                       for l in cls.nm.splitlines() if len(l.split()) == 3}
        b = cls.original
        start = struct.unpack_from('>Q', b, 40)[0]
        size, count, names_index = struct.unpack_from('>HHH', b, 58)
        sections = [struct.unpack_from('>IIQQQQIIQQ', b, start+i*size) for i in range(count)]
        names_header = sections[names_index]
        names = b[names_header[4]:names_header[4]+names_header[5]]
        cls.ctors = next(sh for sh in sections if names[sh[0]:].split(b'\0', 1)[0] == b'.ctors')

    def offset(self, address, size):
        for ph in elf_segments(self.original):
            if ph[0] == 1 and ph[3] <= address and address+size <= ph[3]+ph[5]:
                return ph[2]+address-ph[3]
        self.fail('Address outside file-backed memory')

    def repair(self, data):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / 'test.elf'
            p.write_bytes(data)
            report = instrument_constructors(p, self.nm)
            return p.read_bytes(), report

    def test_dependency_order_and_no_skipped_or_duplicated_initializers(self):
        b, report = self.repair(self.original)
        sh = self.ctors
        table = struct.unpack_from('>'+'Q'*(sh[5]//8), b, sh[4])
        self.assertEqual(table[0], 0xffffffffffffffff)
        self.assertEqual(table[-1], 0)
        # crtbegin executes .ctors backwards; check the real resulting order.
        execution = list(reversed(table[1:-1]))
        expected = ['trace_constructor', 'wili_ctor_0', 'wili_ctor_1', 'wili_ctor_3', 'wili_ctor_2']
        self.assertEqual(execution, [self.symbols[n] for n in expected])
        self.assertEqual(len(set(execution)), 5)
        originals = struct.unpack_from('>4Q', b, self.offset(self.symbols['wili_original_ctors'], 32))
        self.assertEqual(list(originals), [self.symbols[n] for n in
                         ['sbrk_init', '__syscalls_init', '__glob_file_init', 'init_metalock']])
        self.assertEqual(len(report), 4)

    def test_only_constructor_table_and_original_pointer_array_change(self):
        b, _ = self.repair(self.original)
        array = self.offset(self.symbols['wili_original_ctors'], 32)
        sh = self.ctors
        self.assertEqual(len(b), len(self.original))
        changed = [i for i, (before, after) in enumerate(zip(self.original, b)) if before != after]
        self.assertTrue(changed)
        self.assertTrue(all(sh[4] <= i < sh[4]+sh[5] or array <= i < array+32 for i in changed))

    def test_packaged_elf_retains_the_verified_constructor_repair(self):
        repaired, _ = self.repair(self.original)
        final = (self.build / 'platform.patched.elf').read_bytes()
        sh = self.ctors
        array = self.offset(self.symbols['wili_original_ctors'], 32)
        self.assertEqual(final[sh[4]:sh[4]+sh[5]], repaired[sh[4]:sh[4]+sh[5]])
        self.assertEqual(final[array:array+32], repaired[array:array+32])

    def test_changed_sdk_order_and_repeated_repair_are_rejected_without_writing(self):
        repaired, _ = self.repair(self.original)
        changed = bytearray(self.original)
        sh = self.ctors
        # Swap two original SDK ctors, simulating a different SDK/link layout.
        a = bytes(changed[sh[4]+8:sh[4]+16])
        changed[sh[4]+8:sh[4]+16] = changed[sh[4]+16:sh[4]+24]
        changed[sh[4]+16:sh[4]+24] = a
        for data in [repaired, bytes(changed)]:
            with tempfile.TemporaryDirectory() as directory:
                p = Path(directory) / 'rejected.elf'
                p.write_bytes(data)
                with self.assertRaisesRegex(RuntimeError, 'constructor order changed'):
                    instrument_constructors(p, self.nm)
                self.assertEqual(p.read_bytes(), data)


if __name__ == '__main__':
    unittest.main()
