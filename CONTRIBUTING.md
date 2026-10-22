# Contributing to Frontline 2 Recomp

Thanks for wanting to help. This project is a fork of
[WiiCompiled](https://github.com/patchzyy/Wiicompiled) and keeps its ground
rules; the few additions below are about this game.

## The short version

- Code is judged on quality, not on where it came from.
- You must understand and be able to explain every line you submit.
- PR descriptions and review answers must be written by you, **not** generated.
- Accuracy is the bar for anything touching game behavior.

## Code quality

We don't care how your code came into existence. What we care about is whether
it meets or improves the project's patterns and standards, and the only way we
measure that is by reading it. Low-quality code won't be merged, regardless of
origin.

## If you use AI tools

That's fine (this port was developed with AI assistance too), but:

1. **You must be able to explain your changes.** If a reviewer asks why a line
   exists or what a function does and you can't answer, the PR will be closed.
   "The AI wrote it" is not an explanation.
2. **Write your own PR description.** It exists so reviewers know what you
   changed and why, in your words.
3. **Show the measurement.** Fixes in this port are made from evidence (a GX
   trace, a memory watch, a call trace, a capture) rather than by trying patches
   until something looks right. Put that evidence in the PR.

## Pull requests

- Keep PRs focused: one change per PR. Small PRs get reviewed fast.
- Explain **what** and **why**. Reference the issue if there is one.
- For anything affecting game behavior: identical behavior to the Wii is the
  goal. Be prepared to show your change doesn't diverge from the original game
  (Dolphin comparison, logs, captures, whatever fits).
- Changes to the runtime files imported from upstream go through the re-keying
  tool (`projects/bw2/remap/`), not by hand; see `projects/bw2/tools/iterate.sh`.
- Code and comments in English, in the style of the file you touch.
- Review feedback is about the code, not about you.

## Bug reports

Use the issue template. Attach the run log from
`~/Library/Application Support/FrontlineRecomp/Logs/` and, for visual bugs, a
screenshot or a short clip. Check first that the problem does not also happen
on Dolphin or a real Wii: if it does, it is the game's behavior and will not be
changed.

## Legal

- **Never** include game code, assets or data in a PR, an issue, or anywhere
  else in this project: no DOL, no RELs/RSOs/SEL, no translated code
  (`generated/`), no textures, audio or video files. Your own screenshots and
  recordings are fine.
- By contributing, you agree your contributions are licensed under
  [GPL v3.0](LICENSE), like the rest of the project.
