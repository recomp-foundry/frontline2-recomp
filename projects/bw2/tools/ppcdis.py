"""Disassemble the RBWE01 main.dol with map names and data-address annotations.

Annotates small-data accesses (r13/r2), lis+addi/ori/load/store pairs and branch
targets with the nearest MAP.txt symbol. Adapted from NSMB-Wii's ppcdis.py.

usage: python3 -P scripts/ppcdis.py <main.dol> <MAP.txt> <addr|symbol> [count]
"""
import bisect
import re
import struct
import sys

import capstone

R13, R2 = 0x80601160, 0x80608EC0
dol_path, map_path, target = sys.argv[1], sys.argv[2], sys.argv[3]
count = int(sys.argv[4]) if len(sys.argv) > 4 else 64

dol = open(dol_path, "rb").read()
mem = {}
offs = struct.unpack_from(">18I", dol, 0x00)
addrs = struct.unpack_from(">18I", dol, 0x48)
sizes = struct.unpack_from(">18I", dol, 0x90)
sections = [(addrs[i], offs[i], sizes[i]) for i in range(18) if sizes[i]]


def read(a, n):
    for base, off, size in sections:
        if base <= a and a + n <= base + size:
            return dol[off + a - base:off + a - base + n]
    return b"\0" * n


syms = {}
for line in open(map_path):
    if line.startswith("#"):
        continue
    a, n = line.split(None, 1)
    syms[int(a, 16)] = n.strip()
by_name = {n: a for a, n in syms.items() if not n.startswith("0x")}
order = sorted(syms)


def label(a):
    i = bisect.bisect_right(order, a) - 1
    if i < 0:
        return ""
    base = order[i]
    return syms[base] if a == base else f"{syms[base]}+0x{a - base:X}"


start = int(target, 16) if re.fullmatch(r"(0x)?[0-9A-Fa-f]{8}", target) else by_name[target]
md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
md.skipdata = True
known = {}  # register -> value it holds (from lis, then addi/ori)
STORE_OR_COMPARE = ("st", "cmp", "mt", "tw", "dcb", "icb", "b")
print(f"; {label(start)} @ 0x{start:08X}")
for ins in md.disasm(read(start, count * 4), start):
    ops, note = ins.op_str, ""
    m = re.search(r"(-?0x[0-9a-f]+|-?\d+)\((r13|r2)\)", ops)
    if m:
        ea = ((R13 if m.group(2) == "r13" else R2) + int(m.group(1), 0)) & 0xFFFFFFFF
        note = f"  ; [0x{ea:08X}]"
    m = re.fullmatch(r"([rf]\d+), (-?0x[0-9a-f]+|-?\d+)\((r\d+)\)", ops)
    if m and m.group(3) in known:
        ea = (known[m.group(3)] + int(m.group(2), 0)) & 0xFFFFFFFF
        note = f"  ; [0x{ea:08X}]"
    dest = ops.split(",")[0].strip() if ops else ""
    value = None
    m = re.fullmatch(r"(r\d+), (-?0x[0-9a-f]+|-?\d+)", ops)
    if ins.mnemonic == "lis" and m:
        value = (int(m.group(2), 0) & 0xFFFF) << 16
    m = re.fullmatch(r"(r\d+), (r\d+), (-?0x[0-9a-f]+|-?\d+)", ops)
    if ins.mnemonic in ("addi", "ori") and m and m.group(2) in known:
        lo = int(m.group(3), 0)
        value = (known[m.group(2)] | (lo & 0xFFFF)) if ins.mnemonic == "ori" else (known[m.group(2)] + lo) & 0xFFFFFFFF
        note = f"  ; =0x{value:08X} {label(value) if value in syms else ''}"
    if re.fullmatch(r"r\d+", dest) and not ins.mnemonic.startswith(STORE_OR_COMPARE):
        if value is None:
            known.pop(dest, None)
        else:
            known[dest] = value
    if ins.mnemonic == "bl":
        for r in ("r0", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12"):
            known.pop(r, None)
    if ins.mnemonic.startswith("b") and re.fullmatch(r"(cr\d, )?0x[0-9a-f]+", ops):
        note = f"  ; {label(int(ops.split()[-1], 16))}"
    if ins.address != start and ins.address in syms:
        print(f"; ---- {syms[ins.address]}")
    print(f"{ins.address:08X}: {ins.mnemonic:8} {ops}{note}")
