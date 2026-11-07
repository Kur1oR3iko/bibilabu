#!/usr/bin/env python3
"""Trace the small compiled entry routine's control flow and syscall arguments.

This is a constrained static instruction trace, not a PS3 emulator. The call to
boot_main is modeled as returning normally; its C body and kernel calls are not
executed. Real console shutdown/exit behavior still needs hardware confirmation.
"""
import argparse
import json
from pathlib import Path
import struct


def sext(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def trace_entry(path, refuse_process_exit=False, code_address=None):
    data = Path(path).read_bytes()
    if data[:7] != b'\x7fELF\x02\x02\x01':
        raise ValueError('Expected big-endian ELF64')
    entry, phoff = struct.unpack_from('>QQ', data, 24)
    phsize, count = struct.unpack_from('>HH', data, 54)
    if phsize != 56 or count > 32:
        raise ValueError('Bad program header table')
    segments = [struct.unpack_from('>IIQQQQQQ', data, phoff + i * phsize) for i in range(count)]

    def read(address, size):
        for ph in segments:
            if ph[0] == 1 and ph[3] <= address and address + size <= ph[3] + ph[5]:
                offset = ph[2] + address - ph[3]
                return data[offset:offset + size]
        raise ValueError('Trace read outside file-backed segment')

    code, toc = struct.unpack('>II', read(entry, 8))
    if code_address is not None:
        code = code_address
    registers = [0] * 32
    registers[1], registers[2] = 0xd0000000, toc
    pc = code
    calls = []
    for _ in range(64):
        insn = struct.unpack('>I', read(pc, 4))[0]
        op, rt, ra = insn >> 26, (insn >> 21) & 31, (insn >> 16) & 31
        next_pc = pc + 4
        if op in (14, 15):  # addi / addis (li / lis when RA=0)
            value = sext(insn & 0xffff, 16) << (16 if op == 15 else 0)
            registers[rt] = ((registers[ra] if ra else 0) + value) & 0xffffffffffffffff
        elif op == 24:  # ori / nop
            registers[ra] = registers[rt] | (insn & 0xffff)
        elif op == 62 and insn & 3 == 1:  # stdu; model only the stack update
            if ra != 1 or rt != 1:
                raise ValueError('Unexpected store-update in entry')
            registers[ra] += sext(insn & 0xfffc, 16)
        elif op == 18:  # b / bl
            target = sext(insn & 0x03fffffc, 26)
            if not insn & 2:
                target += pc
            if insn & 1:
                # Entry's only C call. ABI-preserved registers are modeled.
                if calls:
                    raise ValueError('Unexpected call after syscall')
                registers[3] = 0
            else:
                next_pc = target
        elif op == 17:  # sc
            number = registers[11]
            calls.append({'number': number, 'r3': registers[3], 'r4': registers[4], 'r5': registers[5]})
            if number in (3, 22) and not (refuse_process_exit and number == 3):
                if registers[3] != 0:
                    raise ValueError('Unexpected process exit status')
                if number == 22 and (registers[4] or registers[5]):
                    raise ValueError('Low-level exit arguments are not zero')
                return {'result': 'TERMINATING_CALL_REACHED', 'calls': calls,
                        'scope': 'static entry trace; kernel behavior not executed'}
            # Simulate getpid returning a positive PID or an exit call failing.
            registers[3] = 123 if number == 1 else 0x80010009
        else:
            raise ValueError(f'Unexpected entry opcode {op} at {pc:#x}')
        pc = next_pc
    raise ValueError('No process termination reached within bounded trace: ' + str(calls))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('elf', type=Path)
    args = ap.parse_args()
    primary = trace_entry(args.elf)
    fallback = trace_entry(args.elf, refuse_process_exit=True)
    if [c['number'] for c in primary['calls']] != [3]:
        raise SystemExit('Primary exit is not syscall 3')
    if [c['number'] for c in fallback['calls']] != [3, 22]:
        raise SystemExit('Fallback exit is not syscall 22')
    print(json.dumps({'primary': primary, 'fallback': fallback}, indent=2))


if __name__ == '__main__':
    main()
