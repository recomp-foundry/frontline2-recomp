# Origin

This repository is a fork of [WiiCompiled](https://github.com/patchzyy/Wiicompiled),
imported unmodified at commit `bd219e38a3e9a1a5047ce41d8cd01210bef3af43`
(2026-10-09, "fix: drive compile progress from ninja's [done/total] counter (#285)")
and retargeted from Mario Kart Wii to one disc: RBWE01 revision 0
(`main.dol` SHA-256 `9e557a6c9d316de3a3db8e9790b8d8c54ffc9cd1656dee89a90a2d7f964df87f`).

It is GPL-3.0-or-later like its upstream; `LICENSE` and `THIRD-PARTY-NOTICES.md`
are kept as imported.

Changes taken from other WiiCompiled forks (each in its own commit, with the source named):

| Change | Source |
| --- | --- |
| Stricter jump table detection | [Strikers-WiiCompiled](https://github.com/BlackAndBlue95/Strikers-WiiCompiled) `JumpTableDetector.cs` |
| `#if 0` blocks hidden from marker scanning | [NSMB-Wii-Multiplayer-Recompilation](https://github.com/WitherBlitz/NSMB-Wii-Multiplayer-Recompilation) `NativeSourceParsing.cs` |
| Warning instead of abort on branches outside the image | NSMB-Wii-Multiplayer-Recompilation `Program.cs` |

No game code or data is part of this repository: the user's own disc is
extracted and translated at build time.
