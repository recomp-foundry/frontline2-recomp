#!/usr/bin/env bash
# Data references of BW2 functions: xrefs.sh <disc root> NAME|ADDR [count]...
# Prints each instruction that touches an absolute or small-data address.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
disc="$1"; shift
for f in "$@"; do
    python3 -P "$here/ppcdis.py" "$disc/sys/main.dol" "$here/../MAP.txt" "$f" 400 |
        awk 'NR==1 {print; next} /^; ----/ {exit} /\[0x|=0x|bl / {print}'
done
