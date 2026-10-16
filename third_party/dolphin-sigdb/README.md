# Dolphin signature database

`totaldb.dsy` is copied verbatim from the Dolphin emulator project
(`Data/Sys/totaldb.dsy`, https://github.com/dolphin-emu/dolphin), via the copy
redistributed in NWiiRecomp's `third_party/dolphin-sigdb` (only this data file
is used; no NWiiRecomp code).

- Copyright: the Dolphin Emulator Project and contributors
- License: GPL-2.0-or-later, full text in `COPYING`

`projects/bw2/tools/bw2_function_map.py` matches it against the disc's
`main.dol` to name Wii SDK functions. It only affects names in the function
map, never generated code.
