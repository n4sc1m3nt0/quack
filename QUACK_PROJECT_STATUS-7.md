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

**Current version:** v0.0.7 (round 7: band-pass estimate from H(f), D2 passes, M3 step 2)

**Fingerprint:** seq=7 · app=version.mjs ·
app_sha256_12=2a5edb9fbaf2 (of everything below the app file's own
fingerprint comment; unchanged at this seq — rewritten by `scripts/bump-round.mjs`) ·
changelog=CHANGELOG.md ·
changelog_sha256_12=7895bbacdd48 (hash of `CHANGELOG.md` below its own
fingerprint line; recomputed by `scripts/bump-round.mjs` at seq 7) · matches the
`STATUS_FILE_FINGERPRINT` comment on line 2–3 of the app file and the
`CHANGELOG_FINGERPRINT` comment on line 1 of `CHANGELOG.md`. If any of
these three don't match what you're looking at, stop and see
instructions item 11 above before doing anything else.

**Last updated (round 7):** three source files touched (`wah_measure.h`, `wah_measure.cpp`, `test_estimator.cpp`) plus CMake-free docs. `estimate_bandpass` fits centre, Q and peak gain to H(f). The D2 test now runs on the new path (D23): same four cases and tolerances, plus gain within 1.5 dB; first written against a stub (4 of 4 failed), then all pass with centre and Q within 1 % and gain within 0.04 dB. Mutations in throwaway copies (Q term, gain ignored, wrong range, no RMS gate, no coherence gate) all turned it red; the noisy case was raised from 0.3 to 1.0 times the stimulus because at 0.3 the coherence gate was not covered. Run: clean rebuild, no warnings; all six test programs exit 0 (`wah_test_analyze`, `wah_test_fit`, `wah_test_toml`, `wah_test_capture`, `wah_test_estimator`, `wah_test_measure`). Not run: anything on real hardware; latency alignment and loopback calibration; the capture code still uses `analyze()`. Only the 2-pole model exists (D7 also wants 4-pole, shelf, measured). Remove from the repository: `git rm -- CHICLA_PROJECT_STATUS-6.md` (and `-2` to `-5` if not done).

## Snapshot

- Done: round 7: `estimate_bandpass`, D2 test migrated to the new path and passing.
- Done: round 6: `measure_transfer_function` (H(f) and coherence) with its test, D23 recorded.
- Done: round 5: warnings cleared, known defects marked in the code, exit-2 path of the estimator test exercised.
- Done: round 4: `test_estimator.cpp` (D2), failing as expected.
- Done: round 3: D8 closed (D18 to D21), `LICENSE` added, README licence and plugin/capture description updated.
- Done: round 2: `ROADMAP.md` (D1 to D17), README corrected (D1, D11), sample profiles marked nominal (D5).
- Done: round 1: `chicla init` wrote the instruction files and the state files; `chicla.policy.json` keeps `build/` and `.claude/` out of the manifest, the delivery and the dump; `CLAUDE.md` section 5 holds the build and test commands.

## Next up

**Open decisions:** D22 (plugin formats; AGPL only or or-later), can wait until M8. New for the owner: the real stimulus (white noise as in the tests, or a multisine; the ROADMAP leaves it open). My suggestion: keep noise for now and decide with the loopback measurement. **Task:** round 8, M3 step 3: latency alignment and loopback calibration (estimate the delay between stimulus and response, and divide out the chain), test first with a known delay and a known chain filter. See `ROADMAP.md`, M3.

## Gates

A gate is a step only the owner can do (a review, a release, a run on another machine). The agent never marks one done.

| Gate | What | State |
| --- | --- | --- |
| G1 | Validation run: a real pedal captured and compared with the profile | open |
| G2 | Choice of framework and licence for the plugin (D8). Decided in D18 to D21; only the owner marks the gate | open |
