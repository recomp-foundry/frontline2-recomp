"""Build the translator's function map (MAP.txt) for the RBWE01 main.dol.

Every map line becomes a function boundary for the translator, so a start
that lands inside a real function splits it. Each seed class below is
therefore filtered to positions that cannot be fallen into: after a
terminator (blr, bctr, rfi or an unlinked branch), after zero padding, or at
a stack prologue.

Seeds (ideas from NSMB-Wii's build_function_map.py and mh3-wii's
generate_symbols.py / function-discovery.md):
  - entry point and executable section starts
  - SEL exports (BW2.sel, FinalRelease) in .init/.text
  - direct-call (bl) targets
  - stack prologues (stwu r1,-N(r1), optionally preceded by mflr r0)
  - CodeWarrior save/restore register thunks (_save_gpr_N ...)
  - data words pointing into text: vtables and function-pointer tables,
    excluding jump tables (short runs whose targets cluster)
  - this-adjustment thunks (addi r3,r3,-N ; b target)
  - lis/addi|ori pairs that materialize a code address
Names: SEL exports, then Dolphin's signature database (totaldb.dsy,
GPL-2.0-or-later data from the Dolphin project) matched by opcode checksum
and exact size. The checksum ignores operands, so short bodies collide with
unrelated functions; a signature name is kept only when the body is at least
16 bytes, the name is a plain C identifier (SDK, MSL, Bluetooth stack,
MetroTRK: C++ names in the database come from other games) and no other
start in this DOL got the same name. Calibrated against the SEL names:
110 agree, 1 disagrees (OSReport vs JUTWarningConsole_f; SEL wins).

SDK functions whose signature was ambiguous get a second chance from
Mario Kart Wii's map (projects/mkwii/MAP.txt in WiiCompiled), as in
NSMB-Wii's sdk_align.py: between two SDK functions named in both games whose
address delta is equal, the functions in between keep their layout. A placed
name is accepted only if the address is already a start in this map and, when
Dolphin has a signature for that name, the body there matches it.

usage: python3 -I projects/bw2/tools/bw2_function_map.py <extracted disc root> <totaldb.dsy> <out dir> <mkwii MAP.txt>
writes <out dir>/MAP.txt and <out dir>/map_report.json
"""
import hashlib
import json
import re
import struct
import sys
from bisect import bisect_right
from collections import Counter
from pathlib import Path

DOL_SHA256 = "9e557a6c9d316de3a3db8e9790b8d8c54ffc9cd1656dee89a90a2d7f964df87f"
SEL_SHA256 = "f4676e5dd04fee4b9ec94b54eecc3a4539067e155aaa6dc76737058c94b57afe"
BASE = 0x80000000
SIZE = 0x01800000

BLR, BCTR, RFI, MFLR_R0 = 0x4E800020, 0x4E800420, 0x4C000064, 0x7C0802A6

disc, sigdb_path, out_dir, mkw_map_path = (Path(a) for a in sys.argv[1:5])
dol = (disc / "sys" / "main.dol").read_bytes()
if hashlib.sha256(dol).hexdigest() != DOL_SHA256:
    sys.exit("main.dol does not match RBWE01 rev 0")
sel = (disc / "files" / "DLL" / "FinalRelease" / "BW2.sel").read_bytes()
if hashlib.sha256(sel).hexdigest() != SEL_SHA256:
    sys.exit("BW2.sel does not match RBWE01 rev 0")

# ---------------------------------------------------------------- flat image
flat = bytearray(SIZE)
offs = struct.unpack_from(">18I", dol, 0x00)
addrs = struct.unpack_from(">18I", dol, 0x48)
sizes = struct.unpack_from(">18I", dol, 0x90)
entry = struct.unpack_from(">I", dol, 0xE0)[0]
text, data = [], []
for i in range(18):
    if sizes[i]:
        flat[addrs[i] - BASE:addrs[i] - BASE + sizes[i]] = dol[offs[i]:offs[i] + sizes[i]]
        (text if i < 7 else data).append((addrs[i], addrs[i] + sizes[i]))
text.sort()
data.sort()


def in_text(a):
    return (a & 3) == 0 and any(s <= a < e for s, e in text)


def word(a):
    return struct.unpack_from(">I", flat, a - BASE)[0]


def is_uncond_branch(w):
    return (w >> 26) == 18 and (w & 3) == 0  # b (relative, no link)


def is_terminator(w, allow_branch):
    return w in (BLR, BCTR, RFI) or (allow_branch and is_uncond_branch(w))


def after_terminator(a, allow_branch=True):
    """Nothing can fall through into a: the previous non-padding word ends a path."""
    p = a - 4
    padded = False
    while word(p) == 0 and a - p < 0x40:
        p -= 4
        padded = True
    return is_terminator(word(p), allow_branch or padded)


def is_stwu_r1(w):
    return (w >> 16) == 0x9421 and (w & 0x8000)  # stwu r1,-N(r1)


def prologue_at(a):
    w = word(a)
    if is_stwu_r1(w):
        return True
    return w == MFLR_R0 and any(is_stwu_r1(word(a + 4 * k)) for k in range(1, 4))


starts, why = {}, Counter()


def looks_like_data(t):
    """.init also holds _rom_copy_info/_bss_init_info tables and MetroTRK's strings: a first
    word that is printable ASCII or a pointer into the image is data, not code."""
    w = word(t)
    if all(0x20 <= c <= 0x7E for c in w.to_bytes(4, "big")):
        return True
    # A pointer such as 0x80004000 decodes as a load/store with rA = 0 ("lwz r0, 0x4000(0)"),
    # which compiled code does not emit: it addresses through r13/r2 or lis.
    return 32 <= (w >> 26) <= 55 and ((w >> 16) & 31) == 0


TRUSTED_SEEDS = {"entry point", "section start", "SEL export", "save/restore thunk", "bl target"}


def add(a, reason):
    if not in_text(a) or word(a) == 0:
        return False
    if reason not in TRUSTED_SEEDS and a not in starts and looks_like_data(a):
        why["dropped: data in text"] += 1
        return False
    if a not in starts:
        starts[a] = reason
        why[reason] += 1
    return True


# ---------------------------------------------------------------- SEL exports
def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def cstr(b, o):
    return b[o:b.index(b"\0", o)].decode("ascii")


# ELF section index -> address. Sections 1/2 are .init/.text (memcpy at the
# first .init word, _SDA_BASE_/_SDA2_BASE_ match __init_registers); only code
# sections matter here.
SEL_CODE_BASES = {1: text[0][0], 2: text[1][0]}
names = {}
sel_names = {}
for p in range(u32(sel, 0x40), u32(sel, 0x40) + u32(sel, 0x44), 16):
    name_off, off, sec, _hash = struct.unpack_from(">4I", sel, p)
    nm = cstr(sel, u32(sel, 0x48) + name_off)
    if sec == 0xFFF1:
        assert {"_SDA_BASE_": 0x80601160, "_SDA2_BASE_": 0x80608EC0}[nm] == off
    elif sec in SEL_CODE_BASES:
        sel_names[SEL_CODE_BASES[sec] + off] = nm

# ---------------------------------------------------------------- save/restore thunks
# CodeWarrior's runtime.c: entry N stores/loads rN (or fN) at a fixed offset from r11,
# entries 14..31 back to back, ending in blr.
THUNK_FORMS = {36: ("save", "gpr", 4), 32: ("rest", "gpr", 4), 54: ("save", "fpr", 8), 50: ("rest", "fpr", 8)}
thunks = {}
for s, e in text:
    for a in range(s, e - 18 * 4, 4):
        w = word(a)
        op = w >> 26
        if op not in THUNK_FORMS or ((w >> 21) & 31) != 14 or ((w >> 16) & 31) != 11:
            continue
        kind, bank, step = THUNK_FORMS[op]
        ok = True
        for k, reg in enumerate(range(14, 32)):
            x = word(a + 4 * k)
            disp = x & 0xFFFF
            if (x >> 26) != op or ((x >> 21) & 31) != reg or ((x >> 16) & 31) != 11 or \
                    (disp - 0x10000) != -step * (32 - reg):
                ok = False
                break
        if not ok:
            continue
        # gpr thunks end in blr after r31; fpr thunks may restore/save r12/lr first in some runtimes
        if word(a + 18 * 4) != BLR:
            continue
        for k, reg in enumerate(range(14, 32)):
            thunks[a + 4 * k] = f"_{kind}_{bank}_{reg}"

# ---------------------------------------------------------------- seeds
add(entry, "entry point")
for s, _e in text:
    add(s, "section start")
for a in sel_names:
    add(a, "SEL export")
for a in thunks:
    add(a, "save/restore thunk")

bl_targets = set()
for s, e in text:
    for a in range(s, e, 4):
        w = word(a)
        if (w >> 26) == 18 and (w & 3) == 1:
            d = w & 0x03FFFFFC
            if d & 0x02000000:
                d -= 0x04000000
            t = (a + d) & 0xFFFFFFFF
            if t != a + 4 and in_text(t):  # "bl next" materializes the PC
                bl_targets.add(t)
thunk_set = set(thunks)
for t in bl_targets:
    # a bl into the middle of a thunk run is a call to that thunk entry; anything else is a start
    add(t, "bl target")

for s, e in text:
    for a in range(s, e, 4):
        if prologue_at(a) and after_terminator(a) and not (word(a) != MFLR_R0 and word(a - 4) == MFLR_R0):
            add(a, "stack prologue")


def boundary_ok(t, allow_branch):
    if t not in starts and looks_like_data(t):
        return False
    return t in starts or prologue_at(t) or after_terminator(t, allow_branch)


# Data words pointing into text, grouped into runs of consecutive pointers.
runs = []
for s, e in data:
    cur = []
    for a in range((s + 3) & ~3, e - 3, 4):
        v = word(a)
        if in_text(v):
            cur.append(v)
        else:
            if cur:
                runs.append(cur)
            cur = []
    if cur:
        runs.append(cur)

jump_tables = 0
for run in runs:
    span = max(run) - min(run)
    if len(run) >= 3 and span < 0x1000 and not all(t in starts for t in run):
        jump_tables += 1  # switch table: its targets are case labels, not functions
        continue
    table = len(run) >= 2  # vtable or function-pointer table
    for t in run:
        w = word(t)
        if (w >> 16) == 0x3863 and (w & 0x8000) and is_uncond_branch(word(t + 4)):
            add(t, "this-adjust thunk")
        elif boundary_ok(t, allow_branch=table):
            add(t, "vtable/function table" if table else "data pointer")

for s, e in text:
    for a in range(s, e, 4):
        w = word(a)
        if (w >> 26) != 15 or ((w >> 16) & 31) != 0:  # lis rD,hi
            continue
        rd, hi = (w >> 21) & 31, w & 0xFFFF
        for b in range(a + 4, min(a + 36, e), 4):
            x = word(b)
            op = x >> 26
            if op == 14 and ((x >> 16) & 31) == rd:  # addi rX,rD,lo
                lo = x & 0xFFFF
                t = ((hi << 16) + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF
            elif op == 24 and ((x >> 21) & 31) == rd:  # ori rX,rD,lo
                t = (hi << 16) | (x & 0xFFFF)
            else:
                continue
            if in_text(t) and boundary_ok(t, allow_branch=False):
                add(t, "lis/addi pointer")
            break

# ---------------------------------------------------------------- Dolphin signatures
def dolphin_checksum_step(total, opcode):
    """One step of Dolphin's HashSignatureDB::ComputeCodeChecksum (opcode fields only)."""
    op = opcode & 0xFC000000
    op2 = op3 = 0
    aux = op >> 26
    if aux == 4:
        op2 = opcode & 0x3F
        if op2 in (0, 8, 16, 21, 22):
            op3 = opcode & 0x7C0
    elif aux in (7, 8, 10, 11, 12, 13, 14, 15):
        op2 = opcode & 0x03FF0000
    elif aux in (19, 31, 63):
        op2 = opcode & 0x7FF
    elif aux == 59:
        op2 = opcode & 0x3F
        if op2 < 16:
            op3 = opcode & 0x7C0
    elif 32 <= aux < 56:
        op2 = opcode & 0x03FF0000
    total = ((total << 17) & 0xFFFE0000) | ((total >> 15) & 0x0001FFFF)
    return total ^ (op | op2 | op3)


db, db_by_name = {}, {}
raw = sigdb_path.read_bytes()
for i in range(struct.unpack_from("<I", raw, 0)[0]):
    chk, size = struct.unpack_from("<II", raw, 4 + 136 * i)
    nm = raw[12 + 136 * i:4 + 136 * (i + 1)].split(b"\0")[0].decode("latin1")
    nm = nm.split("\t")[0].split(" (entry of")[0].strip()
    if size:
        db[(chk, size)] = nm
        db_by_name.setdefault(nm, set()).add((chk, size))

MIN_SIGNATURE_BYTES = 16
C_IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
ordered = sorted(starts)
candidates = {}
for i, a in enumerate(ordered):
    section_end = next(e for s, e in text if s <= a < e)
    limit = min(ordered[i + 1] if i + 1 < len(ordered) else section_end, section_end)
    total, hits = 0, []
    for p in range(a, limit, 4):
        total = dolphin_checksum_step(total, word(p))
        size = p + 4 - a
        hit = db.get((total, size))
        if hit and size >= MIN_SIGNATURE_BYTES:
            hits.append((hit, size))
    if hits:
        candidates[a] = hits[-1]  # longest body that matches
name_count = Counter(n for n, _ in candidates.values())
sig_names, sig_rejected = {}, Counter()
for a, (nm, size) in candidates.items():
    if name_count[nm] > 1:
        sig_rejected["name matched more than one start"] += 1
    elif not C_IDENTIFIER.match(nm):
        sig_rejected["C++ name (other game)"] += 1
    else:
        sig_names[a] = nm
        # the signature's body ends before the next known start: a function begins there
        end = a + size
        if in_text(end) and word(end) != 0 and end not in starts and after_terminator(end):
            add(end, "after signature body")

# ---------------------------------------------------------------- names
for a, nm in sig_names.items():
    names[a] = nm
for a, nm in sel_names.items():
    m = nm.replace("_savegpr_", "_save_gpr_").replace("_restgpr_", "_rest_gpr_") \
          .replace("_savefpr_", "_save_fpr_").replace("_restfpr_", "_rest_fpr_")
    names[a] = m
for a, nm in thunks.items():
    names[a] = nm  # translator convention wins over SEL/Dolphin spellings


# ---------------------------------------------------------------- MKW layout alignment
SDK_NS = r"OS|DVD|GX|VI|AI|AX|DSP|SC|NAND|PAD|SI|EXI|IPC|WPAD|KPAD|PPC|DC|IC|LC|THP|WUD|ARC|MEM|NWC24"


def sdk_spelling(n):
    """MKW's map mixes 'DVDReadPrio', 'DVD::ReadPrio' and 'RVL::OSInit'; return the plain C name."""
    n = n.strip()
    m = re.fullmatch(rf"(?:RVL::)?({SDK_NS})::(__)?(\w+)", n)
    if m:
        return f"{'__' if m.group(2) else ''}{m.group(1)}{m.group(3)}"
    m = re.fullmatch(rf"__({SDK_NS})::(\w+)", n)
    if m:
        return f"__{m.group(1)}{m.group(2)}"
    m = re.fullmatch(r"(?:RVL::)?(IOS|ISFS|ES)::(\w+)", n)
    if m:
        return f"{m.group(1)}_{m.group(2)}"
    m = re.fullmatch(r"RVL::(\w+)", n)
    return m.group(1) if m else n


def body_matches_signature(a, nm):
    sigs = db_by_name.get(nm)
    if not sigs:
        return None  # Dolphin has no signature for this name: cannot check
    sizes = {size for _, size in sigs}
    total = 0
    for p in range(a, a + max(sizes), 4):
        if not in_text(p):
            break
        total = dolphin_checksum_step(total, word(p))
        if (total, p + 4 - a) in sigs:
            return True
    return False


mkw_by_name = {}
for line in mkw_map_path.read_text().splitlines():
    parts = line.split(None, 1)
    if len(parts) == 2 and not parts[1].startswith("0x"):
        nm = sdk_spelling(parts[1])
        if C_IDENTIFIER.match(nm):
            mkw_by_name.setdefault(nm, []).append(int(parts[0], 16))
bw2_by_name = Counter(names.values())
anchors = sorted((mkw_by_name[nm][0], a, nm) for a, nm in names.items()
                 if bw2_by_name[nm] == 1 and len(mkw_by_name.get(nm, ())) == 1)
# Keep the longest chain increasing in both games (drops anchors that disagree on layout).
best = [1] * len(anchors)
prev = [-1] * len(anchors)
tails = []  # patience sort on BW2 address
from bisect import bisect_left
tail_idx = []
for i, (_m, b, _n) in enumerate(anchors):
    k = bisect_left(tails, b)
    if k == len(tails):
        tails.append(b)
        tail_idx.append(i)
    else:
        tails[k] = b
        tail_idx[k] = i
    prev[i] = tail_idx[k - 1] if k else -1
chain, i = [], tail_idx[-1] if tail_idx else -1
while i >= 0:
    chain.append(anchors[i])
    i = prev[i]
anchors = chain[::-1]
anchor_mkw = [m for m, _b, _n in anchors]

aligned, align_rejected = {}, Counter()
taken = set(names.values())
for nm, mkw_addrs in mkw_by_name.items():
    if nm in taken or len(mkw_addrs) != 1 or not nm.startswith(tuple(f"{p}" for p in SDK_NS.split("|")) + ("__",)):
        continue
    ma = mkw_addrs[0]
    i = bisect_right(anchor_mkw, ma) - 1
    if i < 0 or i + 1 >= len(anchors):
        continue
    lo, hi = anchors[i], anchors[i + 1]
    deltas = {lo[1] - lo[0], hi[1] - hi[0]}
    placed = None
    for d in sorted(deltas):
        cand = (ma + d) & 0xFFFFFFFF
        if cand not in starts or cand in names:
            continue
        verdict = body_matches_signature(cand, nm)
        if verdict is False:
            continue
        if len(deltas) > 1 and verdict is None:
            continue  # layout changed between the anchors and nothing confirms the guess
        placed = (cand, "aligned+signature" if verdict else "aligned (equal-delta bracket)")
        break
    if placed:
        aligned[placed[0]] = (nm, placed[1])
    else:
        align_rejected["not placed"] += 1
for a, (nm, how) in aligned.items():
    names[a] = nm
align_how = Counter(how for _n, how in aligned.values())

# ---------------------------------------------------------------- output

disagree = sorted((f"0x{a:08X}", sel_names[a], sig_names[a]) for a in sel_names
                  if a in sig_names and sig_names[a] != sel_names[a])
dup_names = Counter(names.values())

out_dir.mkdir(parents=True, exist_ok=True)
with (out_dir / "MAP.txt").open("w") as fh:
    fh.write(f"# RBWE01 rev 0 main.dol {DOL_SHA256}\n# generated by scripts/bw2_function_map.py; do not edit\n")
    for a in sorted(starts):
        fh.write(f"{a:08x} {names.get(a, f'0x{a:08x}')}\n")

sdk_prefixes = ("OS", "GX", "AX", "AI", "DSP", "DVD", "VI", "PAD", "SI", "EXI", "NAND", "ISFS", "SC", "WPAD", "KPAD",
                "WUD", "IPC", "IOS", "THP", "NWC24", "NCD", "SO", "DWC", "__OS", "__GX", "__DVD", "__VI", "__AX",
                "__AI", "__PAD", "__SI", "__EXI", "__NAND", "__IPC", "__WPAD", "__KPAD", "PPC", "DC", "IC", "LC",
                "ARC", "MEM", "TPL", "CARD", "ENC", "WENC", "MTX", "PSMTX", "PSVEC", "C_MTX", "VEC")
report = {
    "function_starts": len(starts),
    "named": len(names),
    "named_from_sel": len(sel_names),
    "named_from_dolphin_signatures": len(sig_names),
    "signature_names_rejected": dict(sig_rejected),
    "sel_vs_signature_disagreements": disagree,
    "mkw_alignment_anchors": len(anchors),
    "named_from_mkw_alignment": dict(align_how),
    "save_restore_thunks": len(thunks),
    "seeds_by_class": dict(why),
    "data_pointer_runs": len(runs),
    "jump_tables_skipped": jump_tables,
    "sdk_like_names": sum(1 for n in names.values() if n.startswith(sdk_prefixes)),
    "duplicate_names": {n: c for n, c in dup_names.items() if c > 1},
}
(out_dir / "map_report.json").write_text(json.dumps(report, indent=1) + "\n")
print(json.dumps({k: v for k, v in report.items() if k not in ("duplicate_names", "sel_vs_signature_disagreements")}, indent=1))
print(f"SEL/signature disagreements: {len(disagree)}; duplicate names: {len(report['duplicate_names'])}")
