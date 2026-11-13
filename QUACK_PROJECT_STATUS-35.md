# Project status

Created by `chicla init`. This file is the memory of the project between sessions: the state, the decisions, the gates and the next task. Keep it true; the agent reads it first, every session.

## Instructions

1. **Cold start.** Read `CLAUDE.md`, then only the Instructions block, the Fingerprint paragraph and the newest «Last updated» entry of this file. One command prints exactly these:
   ```
   f=$(ls QUACK_PROJECT_STATUS-*.md | sort -t- -k2 -n | tail -1); n=$(grep -n '^\*\*Current version:\*\*' $f | head -1 | cut -d: -f1); sed -n "1,$((n-1))p" $f; sed -n '/^\*\*Fingerprint:\*\*/,/^\*\*Last updated (round/p' $f
   ```
   Open everything else on demand: search for a term, do not re-read everything.
2. **Short commands.** `next` means the task under «Next up».
3. **Roadmap.** If the project has a `ROADMAP.md`, read only the part for the task at hand.
4. **Map.** `CLAUDE.md` holds the rules; this file holds the process and the state; `CHANGELOG.md` holds what each round did.
5. **Keep the docs true.** New work goes into the plan in its place, never done silently. Record what is finished.
6. **History is never deleted.** Move old entries to an archive; do not erase them.
7. **Round consistency.** The round in the changelog and in this file, the version in `version.mjs` and the newest files must agree.
8. **Status file number.** `QUACK_PROJECT_STATUS-N.md`, one running integer, raised by `chicla bump-round` each time the file is delivered.
9. **App version.** Edit `APP_VERSION` in `version.mjs` in every output that changes the project.
10. **Session-start check.** After item 1 run `chicla bump-round --verify`. If it fails, STOP and tell the owner; do not work on top of files that disagree.
11. **Fingerprint.** The paragraph below, the third line of `version.mjs` and the first line of `CHANGELOG.md` carry the same numbers. `chicla bump-round` writes them; never edit them by hand. If they do not match, stop and tell the owner.

**Current version:** v0.0.35 (round 35: no line lost in silence, rule 6 dropped, CTest and the CI workflow)

**Fingerprint:** seq=35 · app=version.mjs ·
app_sha256_12=94557a759b14 (of everything below the app file's own
fingerprint comment; CHANGED at this seq — rewritten by `scripts/bump-round.mjs`) ·
changelog=CHANGELOG.md ·
changelog_sha256_12=ccfe9e218c93 (hash of `CHANGELOG.md` below its own
fingerprint line; recomputed by `scripts/bump-round.mjs` at seq 35) · matches the
`STATUS_FILE_FINGERPRINT` comment on line 2–3 of the app file and the
`CHANGELOG_FINGERPRINT` comment on line 1 of `CHANGELOG.md`. If any of
these three don't match what you're looking at, stop and see
instructions item 11 above before doing anything else.

**Last updated (round 35):** No line is lost in silence (D72): a line with no `=`, a header with no closing `]` and a key before any section are refused with `line N:` in the reason, an array left open on its line says `the array must be written on one line`, and a `[meta]` value without quotes is read as before. Rule 6 of v1.1 (and rule 10 of the shape blocks) is dropped: a comment after an array is read. The seven programs are registered with CTest (`ctest --output-on-failure` from the build folder), and `.github/workflows/ci.yml` runs them on ubuntu with gcc and clang, on macOS with clang, and under the sanitizers (ubuntu, gcc). 16 new checks (`test_toml` 173; group 35 has 15); 12 red before the code; 9 mutations of the reader, all red in the end (two needed a check added). Run: clean rebuild, no warnings; all seven exit 0 and `ctest` passes 7 of 7; with `-DWAH_SANITIZE=ON` all seven exit 0 with no sanitizer report; both files of `examples/` still load; the commands of the clang job were run here and pass. Not run: the workflow on GitHub (gate G3, the owner's); macOS; CMake 3.16 (the workflow's `ctest --test-dir` needs 3.20); anything on real hardware; MSVC. Round size: over the rule (five project files and the workflow, tests +37); no exception was given by the owner. **KNOWN DEFECT (M7), unchanged:** the engine's gain is half in dB and its shelf is a stub. Noticed, not fixed: clang warns `wah_capture.h:130: private field 'buffer_size_' is not used` (g++ does not); a misspelt key is ignored (by the spec); the spec's first line says v1.0; `M_PI` and the gcc-style flags stop an MSVC build. Remove from the repository: `git rm -- QUACK_PROJECT_STATUS-34.md`. New in the repository: `.github/workflows/ci.yml`.

## Snapshot

- Done: round 35: no line lost in silence (a line with no `=`, a header with no closing `]`, a key before any section, an array left open: refused with the line number), rule 6 dropped (a comment after an array is read), the seven programs in CTest, `.github/workflows/ci.yml` (gcc and clang on Linux, clang on macOS, sanitizers), and 16 checks (D72 recorded).
- Done: round 34: the blocks `[shape_heel]` and `[shape_toe]` (spec rules 7 to 11, `Shape`, `check_shape`, reader, writer, `profile_from_summary` for two Measured ends), the sanitizer option and its step in `CLAUDE.md`, and 26 checks (D71 recorded); M6 ends.
- Done: round 33: the silent losses refused with a reason (numbers, `nan`/`inf`, `topology`, `curve`, the custom `lut`, a key twice), a denormal read, and 36 checks (D70 recorded).
- Done: round 32: the points noticed in round 29 (comments after values, strings with escapes and `"""`, a repeated header refused, the float text, `libwah.h` mode), G2 closed by the owner, and 30 checks (D69 recorded).
- Done: round 31: `check_curves`, a writer that refuses what the reader would, a reader that says why, the shelf range, `CLAUDE.md` corrected, and 26 checks (round 30 decisions confirmed, D68 recorded).
- Done: round 30: `profile_from_summary` (a `CaptureSummary` into a `Profile` with its `[curves]`), 21 checks (round 29 decisions confirmed; D67 recorded).
- Done: round 29: the format v1.1 (`Curves`, `[curves]`, `format_version`, the spec section, reader and writer) and 54 checks (round 28 decisions confirmed; D66 recorded).
- Done: round 28: `track_shape`, `shape_pass_points`, the Vox through the virtual pedal and 7 checks (round 27 decisions confirmed; D65 done; M5 closed).
- Done: round 27: `wah::simulate_pass`, the closed loop on Cry Baby and 11 checks (round 26 decisions confirmed, D64 recorded, D65 proposed).
- Done: round 26: the shapes in `capture_summary` and 4 checks (round 25 decisions confirmed).
- Archived «Done» lines (nothing deleted): `docs/archive/status/status-archive-done-1-10.md`, `docs/archive/status/status-archive-done-11-11.md`, `docs/archive/status/status-archive-done-12-12.md`, `docs/archive/status/status-archive-done-13-13.md`, `docs/archive/status/status-archive-done-14-14.md`, `docs/archive/status/status-archive-done-15-15.md`, `docs/archive/status/status-archive-done-16-16.md`, `docs/archive/status/status-archive-done-17-17.md`, `docs/archive/status/status-archive-done-18-18.md`, `docs/archive/status/status-archive-done-19-19.md`, `docs/archive/status/status-archive-done-20-20.md`, `docs/archive/status/status-archive-done-21-21.md`, `docs/archive/status/status-archive-done-22-22.md`, `docs/archive/status/status-archive-done-23-23.md`, `docs/archive/status/status-archive-done-24-24.md`, `docs/archive/status/status-archive-done-25-25.md`

## Next up

**Open decisions:** (a) G3: the owner runs `.github/workflows/ci.yml` on GitHub (push or pull request) and reports each job; `chicla ci-report <logs.zip>` reads the logs of a run. My suggestion: do it before round 36, so that what the other compilers and macOS say is known before more code is written. (b) The clang warning `private field 'buffer_size_' is not used` (`wah_capture.h:130`): my suggestion is to remove the field if nothing needs it, in a few lines of round 36, with the CI job as the check; if the CI log shows other warnings, the same round takes them. (c) Windows with MSVC: my suggestion is a round of its own after the first CI run (`_USE_MATH_DEFINES`, an MSVC case for the warning flags, the job in the workflow); not before the owner wants it. (d) The next task: M7 (engine rewrite, `ROADMAP.md`) is large; my suggestion is to start with its first small piece, the gain in dB (round 27 measured it at half), written first as a closed-loop check on the Cry Baby, and to leave the shelf and the state-variable filter to later rounds. **Task:** round 36 per (b) and (d) once the owner confirms or changes the suggestions, with the CI result of (a) in hand if it is there. See `ROADMAP.md`, M7 and D72.

## Gates

A gate is a step only the owner can do (a review, a release, a run on another machine). The agent never marks one done.

| Gate | What | State |
| --- | --- | --- |
| G1 | Validation run: a real pedal captured and compared with the profile | open |
| G2 | Choice of framework and licence for the plugin (D8). Decided in D18 to D21 | closed by the owner, 2026-11-11 (round 32, «fecha o g2»; the agent only recorded it, D69) |
| G3 | First run of `.github/workflows/ci.yml` on GitHub: ubuntu gcc, ubuntu clang, macOS clang, and the sanitizer job; the owner reports each job | open |
