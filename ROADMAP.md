# Quack roadmap

Written in round 2 (docs only) from the round 1 discussion, 2026-11-01. Round 3 (docs only) added D18 to D22.

## Goal

Two products: the **capture tool** (measure a real wah pedal into a `.wah` profile) and the **plugin** (play any profile in a DAW). The Daisy hardware device is a future extra and is out of scope for now.

## Decisions

Confirmed by the owner on 2026-11-01 unless marked otherwise.

| ID | Decision |
| --- | --- |
| D1 | Round 2 is docs only: this ROADMAP, a corrected README (`libwah` shown as in progress), sample profiles marked as nominal. |
| D2 | The first code round (round 4; round 3 was docs only) is a failing test first: a band-pass of known centre and Q, and the estimator must recover it within a tolerance. |
| D3 | Superseded by D13 (single capture mode). |
| D4 | Source impedance in two phases. Phase 1: capture with a low-impedance source, documented. Phase 2: optional `[input]` block and a "guitar interaction" module in the plugin. |
| D5 | Profiles in `examples/` are labelled nominal/synthetic until real captures exist. |
| D6 | Format v1.1: curves per position for frequency, Q and gain, plus `format_version`. v1 profiles stay valid. |
| D7 | The fit picks 2-pole, 4-pole or 2-pole with shelf from the residual. A `measured` topology is the last resort so no pedal is left out. |
| D8 | Closed by D18 to D21 (round 3). Was: one framework for plugin and capture app, once the licence is decided. |
| D9 | The README claims accuracy only after the closed-loop test and one real capture compared with the physical pedal. |
| D10 | The capture app gets a virtual-pedal mode (the engine as device under test), for tests and demos without hardware. |
| D11 | Time and simplicity promises are removed from the README until the first real capture. |
| D12 | First real pedal: borrow a modern Cry Baby and validate the capture on it first. No pedal available yet. |
| D13 | One capture mode only: a guided, breathing-style movement (hold at heel, rise, hold at toe, fall) with several passes per input level. |
| D14 | No angle-stop mode in v1. It comes back only if the virtual-pedal test shows the guide is not enough. |
| D15 | A live marker of the foot position, as feedback only. It never enters the data. |
| D16 | Adaptive pass count with a confidence meter: minimum 4, maximum 20, the target (fast, normal, maximum) chosen by the user. |
| D17 | Robust aggregation: passes weighted by coherence, bad passes dropped. |
| D18 | Licence: the project is AGPLv3 (owner, 2026-11-01). The `LICENSE` file holds the licence text; the README no longer says MIT. |
| D19 | One framework shared by the plugin and the capture app: JUCE, used under its AGPLv3 option. |
| D20 | The player and the capture are separate targets built on one `libwah`: a light plugin that only plays `.wah` profiles, and a standalone capture app. No capture inside the plugin and no web app for capture (audio-device control, real-time safety, one code base). A web page to view and share profiles is a possible later extra. |
| D21 | The plugin and the capture app talk only through `.wah` files in a shared profiles folder. The plugin picks up new profiles without a restart (or with a refresh button), has an "open profiles folder" button and a short note on where to get the capture app, and labels each profile nominal or captured (D5, D9). The plugin does **not** launch the capture app: the DAW usually holds the audio interface the capture needs, hosts may sandbox the plugin, and the install path is unknown. |
| D23 | Measurement API (owner, 2026-11-01): a new function `wah::measure_transfer_function(stimulus, response, ...)` in `wah_measure.h` returns H(f) and the coherence; `analyze()` stays untouched until the capture uses the new path, then it is removed. The D2 test moves to the new path in round 7 (same four cases, same tolerances) and gains a peak-gain criterion of ±1.5 dB. |
| D22 | **Open, small.** Plugin formats: VST3 (SDK under MIT since 3.8), and whether to add AU (macOS only, Apple SDK) and CLAP. Also AGPL-3.0-only or AGPL-3.0-or-later for the source headers (my suggestion: or-later). Both can wait until M8. |

## Milestones

One small task per round.

1. **M1, docs and honesty:** D1, D5, D11. Round 3 added D18 to D22 and the `LICENSE` file.
2. **M2, failing test:** D2. Done in round 4 (`test_estimator.cpp`, fails as expected). It tests centre and Q only; the peak gain needs the stimulus as a second input to `analyze()`, an API decision for M3.
3. **M3, measurement core** (round 6: `measure_transfer_function`, H(f) and coherence, done; round 7: `estimate_bandpass`, centre, Q and gain from H(f), D2 test migrated and passing, done; still to do: latency alignment and loopback calibration, the real stimulus, moving the capture to the new path and removing `analyze()`): continuous stimulus (noise or multisine), short-window H(f), coherence, loopback calibration of the chain (interface and DI box divided out, latency aligned).
4. **M4, fit and aggregation:** per-position fit (D7), adaptive passes (D16), robust aggregation (D17).
5. **M5, capture flow:** the guide and the live marker (D13, D15), the virtual pedal (D10).
6. **M6, format v1.1:** D6.
7. **M7, engine rewrite:** state-variable filter instead of interpolated biquads, real shelf, `sweep.curve` honoured, dynamics acting on Q and peak gain, atomic position. Closed loop: synthetic capture, profile, engine, measure again, compare. The error budget is set in M3.
8. **M8, plugin:** JUCE (D19), light player only (D20, D21); formats per D22.
9. **M9, real captures and validation:** D12, D9, gate G1.

## Known issues found in the round 1 review

- `analyze()` reads the spectrum of the signal, not the transfer function of the pedal (the new path `measure_transfer_function` and `estimate_bandpass` exists since rounds 6 and 7; `analyze()` goes when the capture moves over). The capture state machine produces no stimulus.
- `test_analyze` passes with Q = 57 and peak gain = 103 dB; `test_capture` passes with Q from 102 to 3823 and a negative `signal_quality`. The tests are too weak.
- `wah::load`, `wah::save` and `wah::serialize` in `libwah.cpp` are stubs; the working code is `load_toml` and `save_toml` in `wah_toml.cpp`.
- The bass shelf is computed and discarded.
- `set_position` always interpolates in log and ignores `sweep.curve`.
- The dynamics scale the output amplitude per stage, not Q and peak gain.
- `set_position` claims to be thread-safe and is not.
- `README.md` said MIT and there was no `LICENSE` file (fixed in round 3, D18). (`package.json` stays at 0.0.1 on purpose: it only marks the project root, the version is `APP_VERSION`, see `CLAUDE.md`.)
- Not checked: the peak gain scaling (`10^(dB/40)`), and the quality of `wah_fit.cpp`.

Round 5 made the build warning-free. The warnings that pointed at real defects (shelf discarded, `wah::load/save/serialize` stubs, `fit_bass_shelf` not implemented, `fit` options ignored) are now marked in the code with a comment, not hidden; the defects are still open.

## Gates

Only the owner marks these done.

| Gate | What | State |
| --- | --- | --- |
| G1 | Validation run: a real pedal captured and compared with the profile. | open |
| G2 | Choice of framework and licence for the plugin (D8). The owner decided it in D18 to D21 on 2026-11-01; the gate stays open until the owner marks it. | open |
