#!/usr/bin/env bash
# Development loop after editing projects/bw2/remap/*.csv or the remap tool:
# re-apply the remap, re-translate (natives decide what is translated), rebuild.
# Logs go to build-macos/iterate-*.log. Requires an earlier ./build.sh run.
set -euo pipefail
cd "$(dirname "$0")/../../.."
T=translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll
L=build-macos
# Only files the remap tool rewrites (those imported from upstream) are reset;
# the port's own runtime files (runtime/src/hle/bw2/, bw2_hle.h) keep their edits.
UPSTREAM=05e82a3
git diff --name-only -- runtime | while read -r f; do
    if git cat-file -e "$UPSTREAM:$f" 2>/dev/null; then git checkout -q -- "$f"; fi
done
python3 -I projects/bw2/tools/remap_runtime.py > "$L/iterate-remap.log"
grep -E '"unresolved_literals"' "$L/iterate-remap.log"
dotnet "$T" translate-recursive 0x80006124 --project projects/bw2/recomp.yml --outdir generated/functions \
    --output-metadata generated/base_translation_output.json --production-source-bundle generated/base_translation_sources.bin \
    --no-function-files --prune-stale --threads "$(getconf _NPROCESSORS_ONLN)" > "$L/iterate-translate.log"
dotnet "$T" emit-build-shards --project projects/bw2/recomp.yml --base-metadata generated/base_translation_output.json \
    --base-functions-dir generated/functions --native-source-dir runtime/src --out generated/build_shards > "$L/iterate-shards.log"
cmake --build "$L" --target WiiCompiled --parallel "$(getconf _NPROCESSORS_ONLN)" > "$L/iterate-build.log" 2>&1 || {
    grep -E "error:|Undefined|\"_" "$L/iterate-build.log" | head -20; exit 1; }
echo "built $L/FrontlineRecomp"
