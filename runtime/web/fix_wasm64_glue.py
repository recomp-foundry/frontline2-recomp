#!/usr/bin/env python3
"""Post-link fixes for wasm64 gaps in third-party JavaScript glue.

emdawnwebgpu's WebGPU.makeStringFromStringView/makeStringFromOptionalStringView
divide their pointer argument as a Number, but functions such as
wgpuRenderPassEncoderPushDebugGroup pass it through unconverted (a BigInt on
wasm64). Coercing the argument at the top of both helpers fixes every caller.

    fix_wasm64_glue.py FrontlineRecomp.js
"""
import sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text()
for name in ('makeStringFromStringView', 'makeStringFromOptionalStringView'):
    old = f'{name}:stringViewPtr=>{{'
    new = old + 'stringViewPtr=Number(stringViewPtr);'
    if new in text:
        continue
    if text.count(old) != 1:
        sys.exit(f'{path}: expected one {old!r}')
    text = text.replace(old, new)
path.write_text(text)
