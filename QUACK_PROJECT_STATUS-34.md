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

**Current version:** v0.0.34 (round 34: the blocks `[shape_heel]` and `[shape_toe]`; M6 ends)

**Fingerprint:** seq=34 · app=version.mjs ·
app_sha256_12=5151c2ac7e4b (of everything below the app file's own
fingerprint comment; CHANGED at this seq — rewritten by `scripts/bump-round.mjs`) ·
changelog=CHANGELOG.md ·
changelog_sha256_12=b05094fee5fb (hash of `CHANGELOG.md` below its own
fingerprint line; recomputed by `scripts/bump-round.mjs` at seq 34) · matches the
`STATUS_FILE_FINGERPRINT` comment on line 2–3 of the app file and the
`CHANGELOG_FINGERPRINT` comment on line 1 of `CHANGELOG.md`. If any of
these three don't match what you're looking at, stop and see
instructions item 11 above before doing anything else.

**Last updated (round 34):** The format v1.1 has the blocks `[shape_heel]` and `[shape_toe]` (D71, M6 ends): the spec section first (rules 7 to 11), then `Shape` in `Profile`, `check_shape` (the one check for the reader and the writer, whose reasons start with the block and the key), the reader (version, both or none, arrays, check), the writer that refuses a bad shape, and `profile_from_summary`, which writes the two tables when both ends are Measured and gives no profile for a Measured end next to any other. The engine ignores the shapes (M7). 26 new checks (21 in `test_toml`, 157; 5 in `test_guided`, 113); 15 red before the code; 23 mutations, all red in the end. Run: clean rebuild, no warnings; all seven test programs exit 0, and under `-DWAH_SANITIZE=ON` (new option; now part of closing a round, `CLAUDE.md` §4) all seven exit 0 with no sanitizer report. Not run: anything on real hardware; a compiler other than g++; a system other than Linux; CMake 3.16. Round size: over the rule (five source files, tests +235; code about 150 lines); no exception was given by the owner. **KNOWN DEFECT (M7), unchanged:** the engine's gain is half in dB and its shelf is a stub. Noticed, not fixed: a line with no `=`, an unclosed header and a key before any section are skipped in silence; a `[meta]` value without quotes is read as text; a misspelt key is ignored (by the spec); the spec's first line says v1.0; no CTest; `M_PI` and the gcc-style flags stop an MSVC build. Remove from the repository: `git rm -- QUACK_PROJECT_STATUS-33.md`.

## Snapshot

- Done: round 34: the blocks `[shape_heel]` and `[shape_toe]` (spec rules 7 to 11, `Shape`, `check_shape`, reader, writer, `profile_from_summary` for two Measured ends), the sanitizer option and its step in `CLAUDE.md`, and 26 checks (D71 recorded); M6 ends.
- Done: round 33: the silent losses refused with a reason (numbers, `nan`/`inf`, `topology`, `curve`, the custom `lut`, a key twice), a denormal read, and 36 checks (D70 recorded).
- Done: round 32: the points noticed in round 29 (comments after values, strings with escapes and `"""`, a repeated header refused, the float text, `libwah.h` mode), G2 closed by the owner, and 30 checks (D69 recorded).
- Done: round 31: `check_curves`, a writer that refuses what the reader would, a reader that says why, the shelf range, `CLAUDE.md` corrected, and 26 checks (round 30 decisions confirmed, D68 recorded).
- Done: round 30: `profile_from_summary` (a `CaptureSummary` into a `Profile` with its `[curves]`), 21 checks (round 29 decisions confirmed; D67 recorded).
- Done: round 29: the format v1.1 (`Curves`, `[curves]`, `format_version`, the spec section, reader and writer) and 54 checks (round 28 decisions confirmed; D66 recorded).
- Done: round 28: `track_shape`, `shape_pass_points`, the Vox through the virtual pedal and 7 checks (round 27 decisions confirmed; D65 done; M5 closed).
- Done: round 27: `wah::simulate_pass`, the closed loop on Cry Baby and 11 checks (round 26 decisions confirmed, D64 recorded, D65 proposed).
- Done: round 26: the shapes in `capture_summary` and 4 checks (round 25 decisions confirmed).
- Done: round 25: `wah::aggregate_shapes` and 13 checks (round 24 decisions confirmed).
- Archived «Done» lines (nothing deleted): `docs/archive/status/status-archive-done-1-10.md`, `docs/archive/status/status-archive-done-11-11.md`, `docs/archive/status/status-archive-done-12-12.md`, `docs/archive/status/status-archive-done-13-13.md`, `docs/archive/status/status-archive-done-14-14.md`, `docs/archive/status/status-archive-done-15-15.md`, `docs/archive/status/status-archive-done-16-16.md`, `docs/archive/status/status-archive-done-17-17.md`, `docs/archive/status/status-archive-done-18-18.md`, `docs/archive/status/status-archive-done-19-19.md`, `docs/archive/status/status-archive-done-20-20.md`, `docs/archive/status/status-archive-done-21-21.md`, `docs/archive/status/status-archive-done-22-22.md`, `docs/archive/status/status-archive-done-23-23.md`, `docs/archive/status/status-archive-done-24-24.md`

## Next up

**Open decisions:** (a) rule 6 (a comment after an array refuses the file) now covers the eight arrays of `[curves]` and the two shape blocks. The owner kept it in round 33 and will decide for all the arrays together. My suggestion: drop it. The reader takes a comment after a value everywhere else, the rule has no technical reason left, and it is the only place where the format's own example style (`key = value  # note`) is refused. It would change rule 6 and rule 10 of the spec and the tests 29bb, 32d1 and the comment case of 34d. (b) Round 35: the lines lost in silence and a CI workflow. My suggestions for the lines: (1) a line that is not blank, not a comment, not a header and has no `=` is refused: `line 12: no = in "freq_heel 450"`; (2) a header with no closing `]` is refused: `line 7: the header is not closed`; (3) a key before any section is refused: `line 3: "name" is before any section`; (4) the reasons of these three carry the line number (the others do not yet; leave them); (5) a continuation line of an array written over several lines is not reported as «no =» but as `key: the array must be written on one line`, so that a multi-line `lut` or `[curves]` array keeps a reason that says what to do; (6) a `[meta]` value without quotes stays as it is. My suggestions for the CI (one file, `.github/workflows/ci.yml`, run on every push and pull request): (1) ubuntu with gcc and with clang, and macOS with clang, each building with CMake and running the seven programs; (2) the sanitizer build as one more job (ubuntu, gcc); (3) register the seven programs with CTest (`enable_testing`, `add_test`) so that the CI and a person run them with one command: a small change to `CMakeLists.txt`; (4) Windows with MSVC left out of this round: it needs `_USE_MATH_DEFINES` and an MSVC case for the warning flags first, and a failing job for those reasons says nothing about the code; add it in a later round if the owner wants it; (5) the workflow can only be run by the owner (a gate): I check its syntax and that its commands work here. (c) The round limit: round 34 went over it on files (five) and tests; if you want the limit to hold, round 35 can be only the lines, and the CI a round of its own (round 36). My suggestion: keep both in round 35, as the CI is one file and a few lines of CMake. **Task:** round 35 per (b) once the owner confirms or changes the suggestions, plus (a) if decided. See `ROADMAP.md`, M6 (done) and M7, D66 to D71.

## Gates

A gate is a step only the owner can do (a review, a release, a run on another machine). The agent never marks one done.

| Gate | What | State |
| --- | --- | --- |
| G1 | Validation run: a real pedal captured and compared with the profile | open |
| G2 | Choice of framework and licence for the plugin (D8). Decided in D18 to D21 | closed by the owner, 2026-11-11 (round 32, «fecha o g2»; the agent only recorded it, D69) |
