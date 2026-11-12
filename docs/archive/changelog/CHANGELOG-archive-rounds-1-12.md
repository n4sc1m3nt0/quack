# Changelog archive — rounds 1-12

Moved out of `CHANGELOG.md` verbatim by `scripts/archive-changelog.mjs`. Nothing was edited or deleted;
this file has no fingerprint and is not part of the fingerprint triple. Find an entry with
`grep -n "^## Round 12" docs/archive/changelog/CHANGELOG-archive-*.md`.

## Round 12 — aggregating passes, M4 step 1 (v0.0.12)

- `wah_measure.h` / `wah_measure.cpp`: `aggregate_passes(passes, num_points = 11)` folds the passes of a capture into one centre-versus-position curve (`AggregatedCurve`: positions, centres, support per position, indices of the dropped passes). Per pass, unusable points are ignored, points at the same position are merged, and the centre is interpolated linearly in log frequency at each position (not across a gap wider than 0.25, not beyond the first and last point). The reference is the weighted median over all passes; a pass is dropped when it differs from it by more than 15 % at more than a quarter of the positions, or has no usable point; with fewer than 3 passes nothing is dropped for disagreement. The result is the coherence-weighted median over the kept passes. A position nobody measured has centre 0 and support 0 (D33, D34).
- `test_measure.cpp`: 16 new checks (79 in total) against a known, non-straight curve (400 Hz times 4 to the power p^1.5) with a breathing-style pass (holds at heel and toe, a rise, a fall) and 2 % noise: six clean passes give the curve within 3 % with none dropped and full support; a pass 40 % off everywhere, and one wrong over the second half, are dropped; one wrong at 2 positions of 11 is kept and outvoted, at 3 of 11 it is dropped; an early stop and a gap in the middle leave the curve right with less support; two passes follow the heavy one; two passes that disagree drop nothing; one glitch window moves nothing; a pass of unusable points is dropped; a single pass gives its own curve; 21 positions work; no passes, fewer than 2 positions and only empty passes are refused.
- Written first against a stub: 11 of the first 14 checks failed (3 expect a refusal); 12k and 12l were added later. While writing the tests, one of mine was wrong, not the code: a pass I meant to be wrong at 2 positions was wrong at 3 once interpolated, so the rule dropped it as it should; the check now uses points exactly on the positions. Broken on purpose in throwaway copies (15 mutations: drop threshold 50 %, drop at half the positions, never drop, mean instead of median, weights ignored, nearest point instead of interpolation, bridging gaps, no limit at the ends, same-position points not merged, centre 0 accepted, empty pass kept, dropping with 2 passes, support always 1, wrong position grid, position outside 0..1 accepted): all turned the test red, two of them by crashing. Two checks were too loose at first (12g and 12f) and were tightened until their mutations turned red.
- This round is about 250 changed lines in three files, more than the usual 200: the aggregation and the drop rule are one decision, and half of it is the test.
- ROADMAP: D32 confirmed, D33 recorded (owner, 2026-11-09); D34 recorded, to confirm.
- Not done: Q and gain from the holds, the confidence meter and the stopping rule, the per-position fit; anything on real hardware.

## Round 11 — tracking a moving pedal, M3 step 5, M3 closed (v0.0.11)

- `wah_measure.h` / `wah_measure.cpp`: `track_bandpass(stimulus, dut, loopback, num_samples, sample_rate, max_delay, window_s = 0.25, segment_size = 2048, freq_min, freq_max, min_coherence)` returns one `TrackPoint` (window centre time and a `BandpassEstimate`) per half-window. The latency comes from the loopback over the whole recording, the loopback transfer function is measured once (the chain does not move), each window of the pedal is divided by it and fitted by `estimate_bandpass`. It returns an empty vector for null input, a window of 0 s, a segment size that is not a power of two, a loopback whose latency is not found, or a window shorter than 4 segments. The division now lives in one internal helper shared with `measure_calibrated` (round 9 checks all still pass).
- Measured in `test_measure.cpp` (63 checks in total, 16 new) with a pedal of Q 5 and +6 dB whose centre moves on a straight line in log frequency, 400 Hz to 1600 Hz in 1 s and back in 1 s, behind a 420-sample latency and a 1.5 kHz high-pass: in the 18 windows without a turnaround the centre is within 3.2 % (budget 5 %). **Q and gain are not within the agreed budget in moving windows: up to 39 % (budget 25 %) and 3.5 dB.** The windows that contain a turnaround of the movement are not held to the budget. A static pedal with a 0.5 s window and segment 4096 gives centre within 0.8 %, Q within 6.3 % and gain within 0.38 dB (9 windows). This is the measured budget that M3 had to set (D31, D32).
- Other checks: a cable instead of the pedal (flat after calibration) is refused in every window; a latency of 3000 samples (longer than a segment) is still removed; the frequency range and the coherence floor reach `estimate_bandpass`; null inputs, unrelated loopback, bad window and segment size are refused.
- Written first against a stub: 5 of the first 13 failed (the check on the centre passed vacuously with no windows, so it was corrected to require at least 15 windows; the other 3 expect a refusal). Broken on purpose in throwaway copies (12 mutations: latency not applied to the pedal, loopback not trimmed, no division, time at the window start, no overlap, segment size ignored, frequency range ignored, coherence floor ignored, no 4-segment guard, latency taken from the pedal, coherence as the maximum, a redundant guard removed): the loopback-not-trimmed one survived at first, because the test latency of 420 samples was shorter than a segment; check 10h (3000 samples) now catches it. The redundant `window > recording` guard was an equivalent mutation (no window fits, so the result is empty anyway) and was removed from the code.
- ROADMAP: D29 and D30 confirmed, D31 recorded (owner, 2026-11-09), D32 recorded (to confirm); M3 is closed.
- Not done: anything on real hardware; the capture code still uses `analyze()` (M5, D30).

## Round 10 — the stimulus, M3 step 4 (v0.0.10)

- `wah_measure.h` / `wah_measure.cpp`: `generate_stimulus(num_samples, seed, rms)`: uniform white noise from a seeded xorshift32, mean removed, scaled to the exact RMS (D28: 0.1 RMS, one pass of 2^17 samples; D29, to confirm). The same arguments give the same array, so the capture can play one stimulus through the loopback and through the pedal. The peak stays at about 1.73 times the RMS, so it cannot clip. It refuses (empty vector) a length below 1, a seed of 0 and an RMS outside (0, 0.5].
- `test_measure.cpp`: 10 new checks (47 in total): same seed gives the identical array, another seed gives unrelated noise (correlation below 0.01), RMS within 0.1 %, mean zero, peak between 1.2 and 1.8 times the RMS, spectrum within 2 dB of flat at eight frequencies from 100 Hz to 10 kHz, bad input refused, and the generated stimulus carries a whole calibrated measurement (latency 420 samples, tilting chain) with centre, Q and gain recovered. Written first against a stub: 7 of the 10 failed (the three refusals pass with a stub).
- Broken on purpose in throwaway copies: seed ignored, mean not removed, RMS not scaled, low-passed (tilted) noise, Gaussian-like noise, seed 0 accepted, no RMS limit, length off by one, RMS 0 accepted. All nine turned the test red.
- ROADMAP: D27 confirmed and D28 recorded (owner, 2026-11-09); D29 and D30 recorded, to confirm.
- Not done: tracking while the foot moves (round 11), anything on real hardware. The capture code still uses `analyze()` and makes no stimulus; D30 proposes that this belongs to M5.

## Round 9 — loopback calibration, M3 step 3b (v0.0.9)

- `wah_measure.h` / `wah_measure.cpp`: `measure_calibrated(stimulus, dut, loopback, num_samples, sample_rate, max_delay)`. It estimates the latency on the loopback (`estimate_delay`), trims both recordings by it, measures both transfer functions and returns the pedal minus the loopback in dB, with the lower of the two coherences per bin (D27). It refuses (valid = false) when the loopback latency is not found or either measurement is refused.
- `test_measure.cpp`: 11 new checks (37 in total). A pedal (800 Hz, Q 5, +6 dB) behind a chain with a 420-sample latency and a 1.5 kHz high-pass: the calibrated magnitude is within 1 dB of the pedal alone (100 Hz to 5 kHz) and the fit gives 797 Hz, Q 4.94, +5.97 dB (centre within 2 %, Q within 10 %, gain within 1 dB are required). Without calibration the fit is refused outright (a check that the test chain is not trivial). A noisy loopback lowers the coherence and never raises it; a silent pedal gives coherence 0 in every bin; refused: null inputs, an unrelated loopback, negative `max_delay`, too short once trimmed. Written first against a stub: 4 of the 11 failed (the rest expect a refusal).
- Broken on purpose in throwaway copies: no trim, only the pedal trimmed, only the loopback trimmed, subtraction reversed, no division, coherence from the pedal only, coherence as the maximum, latency estimated on the pedal, loopback refusal ignored. All nine turned the test red.
- ROADMAP: D24 confirmed by the owner (2026-11-08); D27 recorded (to confirm).
- Not done: the stimulus generator, moving the capture off `analyze()`, anything on real hardware.

## Round 8 — latency of the chain, M3 step 3a (v0.0.8)

- `wah_measure.h` / `wah_measure.cpp`: `estimate_delay(stimulus, response, num_samples, max_delay)` returns the latency in whole samples by FFT cross-correlation over the lags 0..max_delay, with the normalised peak (0..1). It also finds a polarity-inverting chain, and refuses (valid = false) on null or silent input, too little audio, or a best |correlation| below 0.2. Meant for the loopback path, not for the pedal's own output (D24).
- `test_measure.cpp`: 13 new checks. Delay found exactly for 0, 1, 137, 1500 and 4800 samples behind a low-pass, peak above 0.99 for a pure delay, inverted polarity, noise as strong as the signal; refused: delay beyond `max_delay`, unrelated signals, null, negative `max_delay`, too short, silent. Written first against a stub: 4 of the 13 failed (the others expect a refusal, which a stub gives anyway).
- Broken on purpose in throwaway copies: no absolute value, no 0.2 threshold, lags one short, correlation direction swapped, no silence test, no normalisation, no length guard. Each turned the test red. **Survived:** the FFT size without the `max_delay` margin (circular wrap): it only matters for looped or periodic stimuli, which these noise tests do not cover. The margin stays in the code; a test for it belongs with the real stimulus (D25).
- ROADMAP: D24 (to confirm), D25 and D26 recorded; `chicla.policy.json` now also lists `.idea` and `cmake-build-debug` (D26, applied by the owner on 2026-11-08).
- Loopback calibration (dividing the chain out) is round 9, not this one: with it the round would pass 200 lines.

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
