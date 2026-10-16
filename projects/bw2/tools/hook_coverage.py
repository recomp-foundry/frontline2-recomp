"""How many of the runtime's Mario Kart Wii hooks have a named equivalent in BW2's map.

Scans the WiiCompiled runtime for hook macros with a guest address, resolves
each address to its MKW symbol through projects/mkwii/MAP.txt, normalizes the
SDK spelling (OS::Foo -> OSFoo) and looks the name up in BW2's MAP.txt.
Macro list and name normalization follow NSMB-Wii's hook_inventory.py (GPLv3).

usage: python3 -I scripts/hook_coverage.py <wiicompiled copy> <BW2 MAP.txt> <out.csv>
"""
import csv
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

wc, bw2_map, out_csv = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
HOOK = re.compile(
    r"\b(PPC_NATIVE_OVERRIDE(?:_VOID)?|REGISTER_NATIVE_FUNCTION(?:_AS)?|REGISTER_TRANSLATED_FUNCTION|"
    r"MKW_KNOWN_TYPED_NATIVE|MKW_KNOWN_NATIVE_CPU_CALL)\s*\(\s*(?:0x)?([0-9A-Fa-f]{8})u?\s*,\s*([A-Za-z_]\w*)")
SDK_NS = r"OS|DVD|GX|VI|AI|AX|DSP|SC|NAND|PAD|SI|EXI|IPC|WPAD|KPAD|PPC|DC|IC|LC|THP|WUD|ARC|MEM|NWC24"


def load_map(path):
    by_addr = {}
    for line in path.read_text().splitlines():
        p = line.split(None, 1)
        if len(p) == 2 and not line.startswith("#") and not p[1].startswith("0x"):
            by_addr[int(p[0], 16)] = p[1].strip()
    return by_addr


def variants(n):
    out = {n, re.sub(r"\(.*$", "", n)}
    m = re.fullmatch(rf"(?:RVL::)?({SDK_NS})::(__)?(\w+)", n)
    if m:
        out.add(f"{'__' if m.group(2) else ''}{m.group(1)}{m.group(3)}")
    m = re.fullmatch(r"(?:RVL::)?(IOS|ISFS|ES)::(\w+)", n)
    if m:
        out.add(f"{m.group(1)}_{m.group(2)}")
    m = re.fullmatch(r"RVL::(\w+)", n)
    if m:
        out.add(m.group(1))
    m = re.fullmatch(rf"__({SDK_NS})::(\w+)", n)
    if m:
        out.add(f"__{m.group(1)}{m.group(2)}")
    return out


def family(name):
    m = re.match(rf"_*({SDK_NS}|IOS|ISFS|ES|MTX|PSMTX|PSVEC|VEC|C_MTX)", name)
    return m.group(1) if m else "other"


mkw = load_map(wc / "projects" / "mkwii" / "MAP.txt")
mkw_names_count = Counter(mkw.values())
LABEL = re.compile(r"_(caseD|case|switch|default|loop|label)_?|_[0-9A-Fa-f]{8}$|\+0x")
bw2 = {n: a for a, n in load_map(bw2_map).items()}
hooks = {}
for src in sorted((wc / "runtime").rglob("*")):
    if src.suffix not in (".cpp", ".h", ".hpp", ".inc") or "third_party" in src.parts:
        continue
    for m in HOOK.finditer(src.read_text(errors="replace")):
        hooks.setdefault(int(m.group(2), 16), (m.group(1), m.group(3), src.relative_to(wc).as_posix()))

rows, stats = [], defaultdict(Counter)
for addr, (macro, sym, src) in sorted(hooks.items()):
    mkw_name = mkw.get(addr, "")
    found = next((bw2[v] for v in sorted(variants(mkw_name)) if v in bw2), None) if mkw_name else None
    fam = family(mkw_name or sym)
    if found:
        status = "matched"
    elif not mkw_name:
        status = "unnamed in MKW map"
    elif LABEL.search(mkw_name):
        status = "MKW label inside a function"
    elif mkw_names_count[mkw_name] > 1:
        status = "name repeated in MKW map"
    else:
        status = "no BW2 equivalent named"
    stats[fam][status] += 1
    rows.append({"mkw_addr": f"0x{addr:08X}", "macro": macro, "symbol": sym, "mkw_name": mkw_name,
                 "family": fam, "bw2_addr": f"0x{found:08X}" if found else "", "status": status, "source": src})

out_csv.parent.mkdir(parents=True, exist_ok=True)
with out_csv.open("w", newline="") as fh:
    w = csv.DictWriter(fh, fieldnames=list(rows[0]))
    w.writeheader()
    w.writerows(rows)
total = Counter()
for fam in sorted(stats, key=lambda f: -sum(stats[f].values())):
    total.update(stats[fam])
    print(f"{fam:8} " + "  ".join(f"{k}: {v}" for k, v in sorted(stats[fam].items())))
print("TOTAL    " + "  ".join(f"{k}: {v}" for k, v in sorted(total.items())) + f"  (hooks: {len(hooks)})")
