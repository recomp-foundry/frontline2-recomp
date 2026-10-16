"""Append rows to projects/bw2/remap/addresses.csv from stdin: mkw|action|bw2|name|evidence per line."""
import csv
import sys
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "remap" / "addresses.csv"
existing = {row["mkw_addr"].upper() for row in csv.DictReader(l for l in path.open() if not l.startswith("#"))}
with path.open("a", newline="") as fh:
    w = csv.writer(fh, lineterminator="\n")
    for line in sys.stdin:
        if not line.strip():
            continue
        mkw, action, bw2, name, evidence = (p.strip() for p in line.split("|", 4))
        mkw = f"0x{int(mkw, 16):08X}"
        if mkw.upper() in existing:
            print(f"skip duplicate {mkw} {name}")
            continue
        existing.add(mkw.upper())
        w.writerow([mkw, action, f"0x{int(bw2, 16):08X}" if bw2 else "", name, evidence])
