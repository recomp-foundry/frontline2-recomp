# Building by hand

`./build.sh` does all of this for you (see the [README](../README.md)). This page
lists the individual steps for when you want to run one of them on its own or
understand what the script does. Paths are relative to the repository root.

## 1. Prerequisites

- Apple Silicon Mac, macOS 14 or later, Xcode Command Line Tools (`clang`).
- .NET 8 SDK, CMake 3.25+, Ninja, Python 3.10+.
- Your RBWE01 disc (NTSC-U, revision 0), extracted: a folder with `sys/main.dol`
  and `files/`. `nodtool extract --validate image.rvz out/` or Dolphin's
  "Extract entire disc" both produce it.

## 2. Check and place the DOL

```sh
shasum -a 256 /path/to/game/sys/main.dol
# 9e557a6c9d316de3a3db8e9790b8d8c54ffc9cd1656dee89a90a2d7f964df87f
mkdir -p Assets && cp /path/to/game/sys/main.dol Assets/main.dol
```

`Assets/` is ignored by git. Any other hash is rejected by the project file
(`projects/bw2/recomp.yml`).

## 3. Function map

The translator follows direct calls from the entry point; functions reached only
through vtables, callbacks or function tables need a map of function starts.

```sh
python3 -I projects/bw2/tools/bw2_function_map.py /path/to/game \
    third_party/dolphin-sigdb/totaldb.dsy projects/bw2 projects/mkwii/MAP.txt
```

Writes `projects/bw2/MAP.txt` and `map_report.json` (both ignored by git):
23 499 function starts, 1 793 of them named (SEL exports, Dolphin SDK
signatures). Deterministic: the same disc gives the same file.

## 4. Translator

```sh
dotnet build translator/src/Translator.Cli -c Release
T=translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll
```

## 5. Translation

```sh
dotnet "$T" translate-recursive 0x80006124 --project projects/bw2/recomp.yml \
    --outdir generated/functions \
    --output-metadata generated/base_translation_output.json \
    --production-source-bundle generated/base_translation_sources.bin \
    --no-function-files --prune-stale --threads "$(getconf _NPROCESSORS_ONLN)"
dotnet "$T" generate-data-init --project projects/bw2/recomp.yml
dotnet "$T" emit-build-shards --project projects/bw2/recomp.yml \
    --base-metadata generated/base_translation_output.json \
    --base-functions-dir generated/functions \
    --native-source-dir runtime/src --out generated/build_shards
```

23 897 functions in about 20 seconds on an M4; `generated/` is ignored by git.

## 6. Native build

```sh
cmake -S runtime -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DAURORA_SDL3_PROVIDER=vendor -DMKW_BUILD_PRODUCTS=ON
cmake --build build-macos --target WiiCompiled --parallel "$(getconf _NPROCESSORS_ONLN)"
```

The target keeps upstream's name `WiiCompiled`; the output is
`build-macos/FrontlineRecomp`. The first build compiles Aurora, SDL3, the
runtime and the translated functions (grouped in shards): about 3 minutes on an
M4 with 10 cores. Later builds are incremental.

## 7. Configuration

`~/Library/Application Support/FrontlineRecomp/Config.toml` is created on first
run. The only required key is the game folder:

```toml
[paths]
dvd_root = "/path/to/game"   # the folder with sys/ and files/
```

`build.sh` writes this line for you. Saves go to `NAND/` and logs to `Logs/` in
the same folder.

## Development loop

After editing the address tables (`projects/bw2/remap/*.csv`), the patches
(`projects/bw2/remap/patches/`) or the translator:

```sh
projects/bw2/tools/iterate.sh
```

It resets the upstream runtime files, re-applies the re-keying
(`remap_runtime.py`), re-translates and rebuilds. After editing only
`runtime/src/hle/bw2/` or `aurora-main/`:

```sh
cmake --build build-macos --target WiiCompiled --parallel 10
```
