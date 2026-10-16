"""Rewrite the runtime's Mario Kart Wii guest addresses for RBWE01.

Every file is read as imported from upstream (UPSTREAM_COMMIT), so the result
depends only on the inputs and the tool can be re-run; commit hand edits to
these files only after the last run, or fold them into the inputs.

  hook macro (PPC_NATIVE_OVERRIDE, REGISTER_*_FUNCTION, MKW_KNOWN_*) ->
      address rewritten when projects/bw2/remap/addresses.csv or a matching
      SDK name in projects/bw2/MAP.txt gives the BW2 address; otherwise the
      statement is wrapped in "#if 0 // BW2: <reason>" (the translator then
      ignores it, see NativeSourceParsing.StripDisabledBlocks)
  func_8xxxxxxx symbol (a direct call into translated code) -> renamed like a hook
  0x80xxxxxx literal ->
      kept below 0x80004000 (OS low memory, fixed by the hardware);
      rewritten from addresses.csv or, for an MKW function start, by name;
      otherwise left as is and listed as unresolved
  runtime/src/hle/gx/gx_fatal_stubs.cpp ->
      regenerated: upstream's stubs whose function exists in BW2, plus every
      named BW2 GX function that no native hook covers, so an unported GX
      call stops loudly instead of running translated FIFO code

Name matching and macro list follow NSMB-Wii's apply_nsmbw_remap.py /
hook_inventory.py (GPL-3.0).

usage: python3 -I projects/bw2/tools/remap_runtime.py [--check]
writes build/bw2-remap/report.json and remap.csv; --check only reports
"""
import csv
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

UPSTREAM_COMMIT = "05e82a3"
ROOT = Path(__file__).resolve().parents[3]
RUNTIME_DIRS = ("runtime/src", "runtime/include")
FATAL_STUBS = "runtime/src/hle/gx/gx_fatal_stubs.cpp"
OUT = ROOT / "build" / "bw2-remap"
CHECK = "--check" in sys.argv[1:]

HOOK = re.compile(
    r"\b(?P<macro>PPC_NATIVE_OVERRIDE(?:_VOID)?|REGISTER_NATIVE_FUNCTION(?:_AS)?|REGISTER_TRANSLATED_FUNCTION|"
    r"MKW_KNOWN_TYPED_NATIVE|MKW_KNOWN_NATIVE_CPU_CALL)\s*\(\s*(?P<prefix>(?:0x)?)(?P<addr>[0-9A-Fa-f]{8})")
LITERAL = re.compile(r"0x(?P<addr>80[0-9A-Fa-f]{6})")
FUNC_SYMBOL = re.compile(r"\bfunc_(?P<addr>8[0-9A-Fa-f]{7})\b")  # direct calls into translated code
SDK_NS = r"OS|DVD|GX|VI|AI|AX|DSP|SC|NAND|PAD|SI|EXI|IPC|WPAD|KPAD|PPC|DC|IC|LC|THP|WUD|ARC|MEM|NWC24"
LABEL = re.compile(r"_(caseD|case|switch|default|loop|label)_?|_[0-9A-Fa-f]{8}$|\+0x")


def git_show(path):
    return subprocess.run(["git", "-C", str(ROOT), "show", f"{UPSTREAM_COMMIT}:{path}"],
                          check=True, capture_output=True).stdout.decode("utf-8")


def git_ls(dirs):
    out = subprocess.run(["git", "-C", str(ROOT), "ls-tree", "-r", "--name-only", UPSTREAM_COMMIT, *dirs],
                         check=True, capture_output=True).stdout.decode()
    return [p for p in out.splitlines() if p.endswith((".cpp", ".h", ".hpp", ".inc")) and "third_party" not in p]


def load_map(path):
    by_addr = {}
    for line in path.read_text().splitlines():
        p = line.split(None, 1)
        if len(p) == 2 and not line.startswith("#"):
            by_addr[int(p[0], 16)] = "" if p[1].startswith("0x") else p[1].strip()
    return by_addr


def variants(n):
    out = [n, re.sub(r"\(.*$", "", n)]
    m = re.fullmatch(rf"(?:RVL::)?({SDK_NS})::(__)?(\w+)", n)
    if m:
        out.append(f"{'__' if m.group(2) else ''}{m.group(1)}{m.group(3)}")
    m = re.fullmatch(r"NAND::Private::(\w+)", n)
    if m:
        out.append(f"NANDPrivate{m.group(1)}")
    m = re.fullmatch(r"(?:RVL::)?(IOS|ISFS|ES)::(\w+)", n)
    if m:
        out.append(f"{m.group(1)}_{m.group(2)}")
    m = re.fullmatch(r"RVL::(\w+)", n)
    if m:
        out.append(m.group(1))
    m = re.fullmatch(rf"__({SDK_NS})::(\w+)", n)
    if m:
        out.append(f"__{m.group(1)}{m.group(2)}")
    return out


mkw = load_map(ROOT / "projects/mkwii/MAP.txt")
mkw_name_count = Counter(n for n in mkw.values() if n)
bw2 = load_map(ROOT / "projects/bw2/MAP.txt")
bw2_by_name = {n: a for a, n in bw2.items() if n}

manual = {}
manual_path = ROOT / "projects/bw2/remap/addresses.csv"
if manual_path.exists():
    for row in csv.DictReader(line for line in manual_path.open() if not line.startswith("#")):
        manual[int(row["mkw_addr"], 16)] = row


def resolve(addr):
    """(bw2_addr or None, how, reason)."""
    row = manual.get(addr)
    if row:
        if row["action"] == "bw2":
            return int(row["bw2_addr"], 16), "manual", row["evidence"]
        if row["action"] == "keep":
            return addr, "manual keep", row["evidence"]
        return None, "manual disable", row["evidence"]
    name = mkw.get(addr)
    if name is None:
        return None, "unknown", "not an MKW function start"
    if not name:
        return None, "unnamed", "unnamed in the MKW map"
    if LABEL.search(name):
        return None, "label", f"MKW label {name} (inside a function)"
    if mkw_name_count[name] > 1:
        return None, "ambiguous", f"{name} repeats in the MKW map"
    for v in variants(name):
        if v in bw2_by_name:
            return bw2_by_name[v], "name", name
    return None, "no-equivalent", f"{name} has no named BW2 equivalent"


def statement_span(text, start):
    """End offset (exclusive) of the macro call starting at `start`, including a trailing ';'."""
    depth, i = 0, text.index("(", start)
    while i < len(text):
        c = text[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                i += 1
                rest = re.match(r"[ \t]*;", text[i:])
                return i + (rest.end() if rest else 0)
        i += 1
    raise ValueError("unbalanced macro call")


def comment_spans(text):
    """Spans of // and /* */ comments (string and char literals are skipped)."""
    spans, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            spans.append((i, j))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            spans.append((i, j))
            i = j
        else:
            i += 1
    return spans


def same_style(old, value):
    s = f"{value:08X}"
    return s.lower() if old == old.lower() and any(ch.isalpha() for ch in old) else s


# ---------------------------------------------------------------- source edits
# projects/bw2/remap/source_edits.csv: exact replacements applied to the upstream text before the
# address rewrite, for what is not a 0x80xxxxxx literal (r13/r2 offsets, MKW-only behaviour).
# Each must match exactly once; the replaced text is not rewritten again.
source_edits = {}
edits_path = ROOT / "projects/bw2/remap/source_edits.csv"
if edits_path.exists():
    for row in csv.DictReader(line for line in edits_path.open() if not line.startswith("#")):
        source_edits.setdefault(row["file"], []).append(row)
edit_count = 0


def apply_source_edits(path, text):
    """Returns the edited text and the spans of replaced text (left alone by the rewrite)."""
    global edit_count
    spans = []
    for row in source_edits.get(path, []):
        old = row["old"].encode().decode("unicode_escape")
        new = row["new"].encode().decode("unicode_escape")
        if text.count(old) != 1:
            sys.exit(f"source edit for {path} matches {text.count(old)} times: {row['old']!r}")
        at = text.index(old)
        text = text[:at] + new + text[at + len(old):]
        delta = len(new) - len(old)
        spans = [(s + delta, e + delta) if s > at else (s, e) for s, e in spans] + [(at, at + len(new))]
        edit_count += 1
    return text, spans


rows, unresolved, covered_bw2 = [], [], set()
changed_files = {}
for path in git_ls(RUNTIME_DIRS):
    if path == FATAL_STUBS:
        continue
    text, protected = apply_source_edits(path, git_show(path))
    edits = []  # (start, end, replacement)
    disabled = []
    for m in HOOK.finditer(text):
        if any(a <= m.start() < b for a, b in protected):
            continue
        addr = int(m.group("addr"), 16)
        new, how, reason = resolve(addr)
        line = text.count("\n", 0, m.start()) + 1
        rows.append({"file": path, "line": line, "kind": "hook", "macro": m.group("macro"),
                     "mkw_addr": f"0x{addr:08X}", "bw2_addr": f"0x{new:08X}" if new else "",
                     "how": how, "reason": reason})
        if new is not None:
            covered_bw2.add(new)
            if new != addr:
                edits.append((m.start("addr"), m.end("addr"), same_style(m.group("addr"), new)))
        else:
            end = statement_span(text, m.start())
            line_start = text.rfind("\n", 0, m.start()) + 1
            indent = re.match(r"[ \t]*", text[line_start:]).group(0)
            line_end = text.find("\n", end)
            line_end = len(text) if line_end < 0 else line_end
            tail = text[end:line_end].strip()
            tail_is_blank = tail == "" or tail.startswith("//")
            if text[line_start:m.start()].strip() == "" and tail_is_blank:
                block = (f"#if 0  // BW2: {reason}\n" + text[line_start:line_end] + "\n#endif")
                disabled.append((line_start, line_end))
                edits.append((line_start, line_end, block))
            else:
                # Shares its line with other code: comment the call out in place.
                edits.append((m.start(), end, f"/* BW2: {reason} */"))
                disabled.append((m.start(), end))
    hook_spans = [(m.start(), statement_span(text, m.start())) for m in HOOK.finditer(text)]
    comments = comment_spans(text)
    for m in LITERAL.finditer(text):
        addr = int(m.group("addr"), 16)
        if addr < 0x80004000 or any(s <= m.start() < e for s, e in hook_spans + protected):
            continue
        new, how, reason = resolve(addr)
        line = text.count("\n", 0, m.start()) + 1
        rows.append({"file": path, "line": line, "kind": "literal", "macro": "", "mkw_addr": f"0x{addr:08X}",
                     "bw2_addr": f"0x{new:08X}" if new else "", "how": how, "reason": reason})
        if new is not None and new != addr:
            edits.append((m.start("addr"), m.end("addr"), same_style(m.group("addr"), new)))
        elif new is None and how != "manual disable" and not any(a <= m.start() < b for a, b in comments):
            unresolved.append(rows[-1])
    for m in FUNC_SYMBOL.finditer(text):
        if any(a <= m.start() < b for a, b in protected + comments):
            continue
        addr = int(m.group("addr"), 16)
        new, how, reason = resolve(addr)
        line = text.count("\n", 0, m.start()) + 1
        rows.append({"file": path, "line": line, "kind": "symbol", "macro": "", "mkw_addr": f"0x{addr:08X}",
                     "bw2_addr": f"0x{new:08X}" if new else "", "how": how, "reason": reason})
        if new is not None and new != addr:
            edits.append((m.start("addr"), m.end("addr"), f"{new:08X}"))
        elif new is None and how != "manual disable":
            unresolved.append(rows[-1])
    if edits or protected:
        out, last = [], 0
        for s, e, rep in sorted(edits, key=lambda x: (x[0], -x[1])):
            if s < last:
                continue  # an address inside a statement that was disabled as a whole
            out.append(text[last:s])
            out.append(rep)
            last = e
        out.append(text[last:])
        changed_files[path] = "".join(out)

# ---------------------------------------------------------------- GX fatal stubs
stub_src = git_show(FATAL_STUBS)
STUB = re.compile(r'^GX_FATAL_STUB\((?P<addr>[0-9a-fA-F]{8}), "(?P<name>[^"]*)"\).*$', re.M)
header = stub_src[:STUB.search(stub_src).start()]
stubs, stub_kept, stub_dropped = {}, 0, 0
for m in STUB.finditer(stub_src):
    new, how, _reason = resolve(int(m.group("addr"), 16))
    if new is not None and new not in covered_bw2:
        stubs[new] = re.sub(r"_[0-9a-fA-F]{8}$", "", m.group("name"))
        stub_kept += 1
    else:
        stub_dropped += 1
for a, n in bw2.items():
    if n and re.match(r"_*GX[A-Z]", n) and a not in covered_bw2 and a not in stubs:
        stubs[a] = n
stub_body = "".join(
    f'GX_FATAL_STUB({a:08x}, "{n}") PPC_NATIVE_OVERRIDE_VOID({a:08x}, gx_stub_{a:08x}, '
    f'(CpuContext* ctx), (ctx));\n' for a, n in sorted(stubs.items()))
changed_files[FATAL_STUBS] = header.replace("// Auto-generated GX fatal stubs",
                                            "// Auto-generated GX fatal stubs (projects/bw2/tools/remap_runtime.py)") + stub_body

# ---------------------------------------------------------------- write
if not CHECK:
    for path, content in changed_files.items():
        (ROOT / path).write_text(content)
OUT.mkdir(parents=True, exist_ok=True)
with (OUT / "remap.csv").open("w", newline="") as fh:
    w = csv.DictWriter(fh, fieldnames=list(rows[0]))
    w.writeheader()
    w.writerows(rows)
summary = {
    "hooks": dict(Counter(r["how"] for r in rows if r["kind"] == "hook")),
    "literals": dict(Counter(r["how"] for r in rows if r["kind"] == "literal")),
    "gx_fatal_stubs": {"kept_from_upstream": stub_kept, "dropped": stub_dropped, "total": len(stubs)},
    "source_edits": edit_count,
    "files_changed": len(changed_files),
    "unresolved_literals": len(unresolved),
}
(OUT / "report.json").write_text(json.dumps({**summary, "unresolved": unresolved}, indent=1) + "\n")
print(json.dumps(summary, indent=1))
for r in unresolved:
    print(f"  unresolved {r['mkw_addr']} {r['file']}:{r['line']} ({r['reason']})")
