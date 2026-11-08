<!-- CHANGELOG_FINGERPRINT: seq=7 file=QUACK_PROJECT_STATUS-7.md app_sha256_12=2a5edb9fbaf2 changelog_sha256_12=7895bbacdd48 (bump-round.mjs: app hash unchanged, changelog hash recomputed) -->
# Changelog

## Round 7 — band-pass estimate from H(f), D2 passes (v0.0.7)

- `wah_measure.h` / `wah_measure.cpp`: `estimate_bandpass(tf)` fits a 2-pole band-pass (centre, Q, peak gain) to the measured magnitude in dB, using bins above a minimum coherence, weighted by coherence / f; Nelder-Mead with four starting Qs. It refuses (valid = false) when too few bins qualify or the fit is worse than 3 dB RMS.
- `test_estimator.cpp` moved from `analyze()` to the new path (D23): same four cases and tolerances (centre 5 %, Q 20 %), plus peak gain within 1.5 dB (each case has its own gain: 0, +6, -3, +10 dB). Written first against a stub, 4 of 4 failed. Now all pass, centre and Q within 1 %, gain within 0.04 dB.
- Extra checks: noisy response as strong as the stimulus (same tolerances), a comb filter is refused, unrelated noise is refused, an invalid transfer function is refused.
- Broken on purpose in throwaway copies: Q term wrong, gain ignored, wrong frequency range, no RMS gate, no coherence gate. All turned the test red; the coherence gate was first not caught at noise 0.3, so the noisy case was raised to 1.0, where it is.
- `CLAUDE.md` section 5 and `README.md` updated: the estimator test now passes.

## Round 6 — transfer function and coherence, M3 step 1 (v0.0.6)

- Added `wah_measure.h` / `wah_measure.cpp`: `measure_transfer_function(stimulus, response, ...)` returns the gain in dB and the coherence per frequency bin (Welch averaging, Hann, 50 % overlap, own double-precision FFT). `analyze()` is untouched (D23).
- Added `test_measure.cpp` and target `wah_test_measure` (13 checks): white and coloured stimulus against the analytic response of a known filter (within 1 dB, peak +6 dB within 0.5 dB), coherence falling under added noise, bad input refused. Written first against an empty stub and seen to fail.
- Broken on purpose in throwaway copies: no coherence, a single segment instead of averaging, output-only spectrum, wrong gain. Each turned the test red.
- ROADMAP: D23 recorded. `CLAUDE.md` section 5 and `README.md`: six test programs.

## Round 5 — warnings and housekeeping (v0.0.5)

- The build has no compiler warnings now (it had 13). Cosmetic ones fixed: unused variables and parameters in `wah_fit.cpp`, `wah_analyze.cpp`, `test_capture.cpp`; `[[maybe_unused]]` on the unused `parse_string` helper in `wah_toml.cpp`.
- Warnings that pointed at real defects were not removed but marked in the code (`libwah.cpp`: shelf discarded, `load`/`save`/`serialize` stubs; `wah_fit.cpp`: `fit_bass_shelf` not implemented, `options` ignored). The defects are still open (ROADMAP).
- The `SETUP ERROR` path (exit 2) of `test_estimator.cpp` was exercised with a deliberately wrong filter in a throwaway copy outside the repository.
- `package.json` stays at 0.0.1 on purpose (`CLAUDE.md`: it only marks the project root).
- Five source files touched, over the usual three, because each change is one line.

## Round 4 — failing test for the estimator, D2 (v0.0.4)

- Added `test_estimator.cpp` and the CMake target `wah_test_estimator`: white noise through a band-pass of known centre and Q (4 cases); `analyze()` must recover both. It fails on purpose: the centre is 0.2 % to 7 % off and the Q comes out in the hundreds or thousands instead of 3 to 8.
- Tolerances: centre ±5 %, Q ±20 %. The Q figure was set from a throwaway reference estimator that is not in the repository (15.7 % error on Q=8 at 2 kHz), and it was moved from 15 % to 20 % before any estimator passed.
- The test checks its own filter against the analytic response (exit 2 if it is wrong). No estimator code was changed.
- `CLAUDE.md` section 5 and `README.md` say there are five test programs and that this one is expected to fail until M3.

## Round 3 — docs only (v0.0.3)

- `ROADMAP.md`: D8 closed by D18 (AGPLv3), D19 (JUCE shared), D20 (plugin and capture app separate), D21 (they share only a profiles folder); new small open decision D22 (plugin formats, AGPL only or or-later). D2 moved to round 4.
- Added `LICENSE` (AGPLv3). `README.md`: licence, plugin and capture rows, profiles folder.
- No source file changed.

## Round 2 — docs only (v0.0.2)

- Added `ROADMAP.md`: goal, decisions D1 to D17, milestones, known issues, gates.
- `README.md`: `libwah` is in progress, capture and plugin are planned, accuracy and time claims removed, guided capture described as a plan.
- `examples/*.wah`: descriptions say the profiles are nominal.
- No source file changed.

## Round 1 — project started with `chicla init` (v0.0.1)

- Created `CLAUDE.md`, `AGENTS.md`, `CHICLA_PROJECT_STATUS-1.md`, `CHANGELOG.md`, `version.mjs` and `FILES.manifest`.
- Nothing else has been done yet.
