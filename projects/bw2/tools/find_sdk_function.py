"""List BW2 candidates for SDK function names the map left unnamed.

For each name: every function start whose body matches one of Dolphin's
signatures for that name (including starts the map rejected as ambiguous),
and the address MKW's layout predicts between the nearest shared anchors.
A candidate both sources agree on is reported as "agree".

usage: python3 -I projects/bw2/tools/find_sdk_function.py <extracted disc root> NAME [NAME...]
"""
import contextlib
import io
import runpy
import sys
from bisect import bisect_right
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
disc, wanted = sys.argv[1], sys.argv[2:]
sys.argv = ["bw2_function_map.py", disc, str(ROOT / "third_party/dolphin-sigdb/totaldb.dsy"),
            str(ROOT / "build/bw2-map-scratch"), str(ROOT / "projects/mkwii/MAP.txt")]
with contextlib.redirect_stdout(io.StringIO()):
    g = runpy.run_path(str(ROOT / "projects/bw2/tools/bw2_function_map.py"))

starts, names, anchors = g["starts"], g["names"], g["anchors"]
mkw_by_name, sdk_spelling = g["mkw_by_name"], g["sdk_spelling"]
db_by_name, word, step, in_text = g["db_by_name"], g["word"], g["dolphin_checksum_step"], g["in_text"]
anchor_mkw = [m for m, _b, _n in anchors]


def signature_hits(nm):
    sigs = db_by_name.get(nm, set())
    if not sigs:
        return None
    longest = max(s for _, s in sigs)
    hits = []
    for a in sorted(starts):
        total = 0
        for p in range(a, a + longest, 4):
            if not in_text(p):
                break
            total = step(total, word(p))
            if (total, p + 4 - a) in sigs:
                hits.append(a)
                break
    return hits


for nm in wanted:
    nm = sdk_spelling(nm)
    hits = signature_hits(nm)
    predicted = []
    for ma in mkw_by_name.get(nm, []):
        i = bisect_right(anchor_mkw, ma) - 1
        if 0 <= i < len(anchors) - 1:
            lo, hi = anchors[i], anchors[i + 1]
            for d in {lo[1] - lo[0], hi[1] - hi[0]}:
                predicted.append(((ma + d) & 0xFFFFFFFF, f"{lo[2]}..{hi[2]}"))
    agree = sorted({p for p, _ in predicted} & set(hits or []))
    print(f"{nm}: named={hex(next((a for a, n in names.items() if n == nm), 0))} "
          f"signature={'n/a' if hits is None else [hex(h) for h in hits[:8]]}"
          f"{' (+%d more)' % (len(hits) - 8) if hits and len(hits) > 8 else ''} "
          f"predicted={[(hex(p), 'start' if p in starts else 'not-start', why) for p, why in predicted]} "
          f"agree={[hex(a) for a in agree]}")
