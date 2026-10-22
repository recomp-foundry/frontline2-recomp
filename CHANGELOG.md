# Changelog

All notable changes to this port are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/) with `0.y.z` while the port is
pre-release (see "Versioning" at the end).

## [0.1.0] — 2026-10-19

First named version. Build at commit `7b36e44` (2026-10-17); the milestone
it marks is mission 1 of the campaign played to victory on macOS on
2026-10-18 (`logs/08-manual3.log`, 171 captures, 0 decode desyncs).

### Platform

- Native arm64 binary for macOS 14+ on Apple Silicon (`FrontlineRecomp`),
  graphics through Aurora on Dawn/Metal. No emulator, interpreter or JIT.
- One supported disc: **RBWE01** revision 0 (NTSC-U). The `main.dol` hash is
  checked; any other DOL is rejected.
- The game data is read in place from the user's extracted disc
  (`dvd_root` in `~/Library/Application Support/FrontlineRecomp/Config.toml`).
  No game code or data lives in this repository.

### Translation (PowerPC → C++)

- 23 897 functions translated from `main.dol` with a reproducible function
  map (`projects/bw2/tools/bw2_function_map.py`): 23 499 function starts
  from `bl` targets, prologues, vtables, jump and function tables, SEL
  exports and Dolphin SDK signatures (1 793 named); 0 unsupported
  instructions, 0 invalid SSA. The map is byte-identical when regenerated
  from a clean clone.
- Translator fixes on top of upstream WiiCompiled: stricter jump-table
  detection (Strikers), `#if 0` blocks hidden from native-marker scanning and
  warnings for branches outside the image (NSMB-Wii), function tables called
  through `bctrl`, 3-instruction virtual methods whose first word looks like
  ASCII, `stfd` of a paired-single register stores `ps0` as a double (D23),
  paired/scalar dataflow aware of resolved loads (D26).
- Translator hook `AfterRuntimeStaticCall` for return-side tracing of static
  calls.

### Runtime and HLE

- Runtime re-keyed from Mario Kart Wii to RBWE01 addresses with a
  deterministic tool (`projects/bw2/tools/remap_runtime.py`): ~140 address
  rows and 29 source edits, each with its evidence; 285 hooks resolved by
  name, 35 by table, the rest explicitly disabled.
- HLE of the Wii SDK used by BW2: OS (threads, alarms, STM/IPC), DVD with a
  disc index, VI, GX (`GXInit`, `__GXData` mirrors, display copies), AI/AX/DSP
  audio, PAD, WPAD/KPAD (Wii Remote + Nunchuk), NAND/ISFS.
- SEL/RSO modules (`HomeButtonMenu.rso`, `wiizLib.rso`) served natively.
- Saves in an emulated NAND under
  `~/Library/Application Support/FrontlineRecomp/NAND` (`BWii.dat`,
  `BWiiMP.dat`, `banner.bin`); automated test runs use an isolated NAND.
- Loader busy-waits yield to the scheduler instead of spinning.

### Graphics

- Display lists that set their VCD/VAT inside and draw afterwards: each draw
  publishes the exact array ranges it reads (`DlArrayRange`, patches
  0011–0015), and cached lists take base/stride from the caller's state for
  arrays they do not set (0017).
- Skinned meshes (`PNMTXIDX`), title-screen globe, terrain with textures
  across the whole map (`GXSetVtxAttrFmtv` fraction fix, 0016), sky, water,
  HUD, minimap, cinematics, 2D text boxes.
- `GXSetZScaleOffset` per the SDK; CP array bases written inside lists
  resolved for Aurora.
- Zero decode desyncs, truncated draws, unknown opcodes or interpreter
  fallbacks over a full mission.

### Input

- Keyboard and mouse emulate a Wii Remote + Nunchuk: mouse = IR pointer,
  left/right click = A/B, WASD = stick, arrows = d-pad, Enter/Backspace =
  +/−, Space/Left Shift = Z/C, 1/2, H = HOME.
- Lock-on with Z works (it depended on the D26 translator fix).
- Scripted input for automated runs (`BW2_AUTOSCRIPT`: `press=`, `hold=`,
  `point=`, `stick=`, `wait=` on voice-line markers).

### What plays

- Boot, strap and ESRB notices, intro video, title screen, save creation and
  loading, name keyboard, main menu (CAMPAIGN, NINTENDO WFC, OPTIONS, EXTRAS).
- Campaign prologue and mission 1 (`SP_0.1`) from "Steps to victory" to
  victory: tutorial, heavy tank, gunboats, battlestation, gate chains,
  magazines, hill, tower, victory music, autosave, campaign card and
  cinematic.
- Music, voice lines and sound effects.

### Diagnostics

- Environment switches: `BW2_TRACE_DVD`, `BW2_CAPTURE_FRAMES`, `BW2_WATCH`,
  `BW2_TRACE_LOCK`, `BW2_TRACE_INDIRECT` family (`_FIRST`, `_EVERY`, `_DEREF`,
  `_R3`, `_FROM_FRAME`), `BW2_TRACE_VAT`, `BW2_DL_ENTRY_LAYOUT`,
  `AURORA_TRACE_VAT`, `AURORA_SKIP_DRAW_RANGE`, `AURORA_NO_PRESENT`,
  `AURORA_TRACK_BILLBOARD`/`_HUD`.
- CPU reference decoder for display lists with per-draw trace; frame
  captures from Aurora.

### Known limitations

- Only mission 1 is verified. The rest of the campaign, skirmish and
  multiplayer are untested; Nintendo WFC and Discord integration are off.
- Internal resolution is the console's 640×480 (the `widescreen` canvas
  trick inherited from the Mario Kart runtime is used); no scaling, aspect or
  resolution work yet.
- No gamepad support, no relative mouse, no key remapping; the HOME menu is
  not implemented; `SYSCONF` is missing from the emulated NAND.
- Upstream WiiCompiled features (frame interpolation, high internal
  resolution, music ducking) are present in the settings overlay (F10) but
  have not been validated with this game.
- macOS only. The browser (WebAssembly + WebGPU) and PS Vita targets are not
  built yet; `build-web/` is a probe.
- Automated mission scripts became unreliable after D26 (the blind walk
  drifts into the canyon wall); manual play is the regression check.
- The build has only been exercised on the author's machine and on a clean
  clone of this repository on the same machine; no CI yet. The upstream
  `Launcher/` (installer and Wheel Wizard integration for Mario Kart Wii) is
  kept as imported and is not used by this port.

## Versioning

- `0.1.x`: macOS, mission 1 playable. Patch releases for fixes that do not
  add a target or a feature area.
- `0.2.0`: scaling, resolution and PC controls (task 09).
- `0.3.0`: browser build (task 10).
- `1.0.0`: the full campaign plays on at least one target.

No public releases yet: `0.1.0` is the build at commit `7b36e44` plus this
file and the version string in the settings overlay. There is deliberately no
`VERSION` file at the repository root: on a case-insensitive filesystem it
shadows libc++'s `<version>` header because `port/` is on the include path.
