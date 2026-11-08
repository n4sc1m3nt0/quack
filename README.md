# Quack

**One expression pedal. Every wah ever made.**

Quack is an open source toolkit that aims to capture the character of real wah pedals into portable `.wah` profile files and play them back in your DAW. Inspired by the way neural amp modelers have replaced amp collections, Quack wants to do the same for wah pedals.

> **Early stage.** The DSP core is partly built. The capture tool and the plugin are being designed, and the measurement code is being redone. See [`ROADMAP.md`](ROADMAP.md) for the decisions, the plan and the known gaps.

---

## The idea

A wah pedal is a bandpass filter swept by your foot. The difference between a 1967 Vox V846 and a modern Cry Baby is a handful of numbers: center frequency range, Q, peak gain, the curve of the sweep. Quack measures those numbers from a real pedal and stores them in a simple text file.

That file loads in a plugin in your DAW (VST3 first). The pedal under your foot is whatever expression pedal you own, and the sound is whatever profile you loaded.

---

## What's in this repo

| Component | Status | Description |
|---|---|---|
| `libwah` | 🚧 In progress | Core C++17 library, no dependencies: DSP engine, profile parser, fitting, capture logic. The measurement part is being redone. |
| `capture-tool` | 🔴 Planned | Standalone desktop app (JUCE) with one guided capture mode (see below). Separate from the plugin. |
| `editor` | 🔴 Planned | Desktop app to build `.wah` profiles by hand |
| `plugin` | 🔴 Planned | Light player plugin (JUCE) that plays `.wah` profiles in a DAW. VST3 first; AU and CLAP are still to be decided. It does not capture. |
| `firmware` | 💤 Possible later extra | Reference firmware for a Daisy Seed hardware device |
| `spec/` | ✓ v1.0 written | `.wah` file format specification (v1.1 is planned) |
| `examples/` | Nominal | Sample `.wah` profiles with typical values. They were **not** measured from a real pedal. |

---

## Capturing a pedal (planned design, not implemented yet)

You will need a wah pedal, an audio interface and, if you want to keep the signal clean, a DI box.

**Setup:**
```
Audio interface output -> (DI box) -> wah pedal input -> wah pedal output -> audio interface input
```

**The planned flow:**

1. **Calibrate the chain.** With a cable in place of the pedal, the tool measures the interface and the DI box, and divides them out of every later measurement.
2. **Set the levels.** The tool tells you when the input level is right, for three input levels.
3. **Follow the guide with your foot.** A breathing-style guide moves between heel and toe: hold at heel, rise, hold at toe, fall. You make several passes at each level. A confidence meter rises with every pass, and the tool stops when it is confident enough. You can keep going for more precision.
4. **Name your profile and export your `.wah`.**

There are no time estimates yet. They will be added after the first real capture.

---

## The .wah format

A `.wah` file is a plain TOML text file: human-readable, hand-editable, and tiny. The example below is illustrative.

```toml
[meta]
name        = "Example Vintage Wah"
author      = "someone"
make        = "Vox"
model       = "V846"
description = "Illustrative example, not a real capture"
created     = "2026-06-17T14:32:00Z"

[filter]
topology     = "bandpass_2pole"
freq_heel    = 420
freq_toe     = 2180
q_min        = 4.1
q_max        = 4.6
peak_gain_db = 17.2

[sweep]
curve = "log"

[dynamics]
resonance_low  = 0.0
resonance_mid  = 0.3
resonance_high = 0.6
```

The full format specification is in [`spec/WAH_FORMAT.md`](spec/WAH_FORMAT.md). A v1.1 is planned, with curves per pedal position for frequency, Q and gain. v1 profiles will stay valid.

---

## Playing profiles (planned)

The plugin will load any `.wah` file and take the pedal position from:

- MIDI CC (default CC11, configurable)
- DAW automation lane
- Audio sidechain envelope follower (auto-wah)

Profiles live in a shared profiles folder. The capture app writes a `.wah` file there and the plugin picks it up, so the two programs only talk through files. Each profile is labelled nominal or captured. The plugin does not start the capture app.

---

## Hardware (possible later extra)

A small device built around a **Daisy Seed** (an STM32H7-based audio board) is possible later, with audio in and out, an expression pedal input and an encoder with an OLED to select profiles. It is not part of the current work.

> **Note:** The Daisy Seed's codec expects a buffered input signal, and a buffer changes how a wah interacts with a guitar's pickups.

---

## How accurate is it?

Not measured yet, so there is no claim here.

A wah pedal is mostly a resonant bandpass filter, which has a clean mathematical description. But a real pedal also depends on things a plain filter model leaves out: how the pickups and the cable load the pedal's input, the way vintage hand-wound inductors saturate, and how the gain changes along the sweep.

Accuracy claims will be added only after:

1. a closed-loop test: a synthetic pedal is captured, the profile is played back, and the result is compared with the original;
2. a real pedal captured and compared with the physical pedal.

---

## Building from source

```bash
git clone https://github.com/yourusername/quack
cd quack
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Current state:** `libwah` builds with CMake 3.16+ and a C++17 compiler, with no external libraries. Four of the older test programs pass, but they are weak: they do not prove that the measurement works. `wah_test_measure` is strict and passes: it checks the new transfer-function measurement (`wah_measure.h`) against a known filter. `wah_test_estimator` is strict and passes: white noise through a known band-pass, measured with `measure_transfer_function` and fitted with `estimate_bandpass`, recovers the centre, Q and gain (within 1 % in the clean cases, tolerances 5 %, 20 %, 1.5 dB). The old `analyze()` and its test are still there and still weak; the capture code has not moved to the new path yet (milestone M3, see `ROADMAP.md`).

Future components (capture app, plugin) will use JUCE, under its AGPLv3 option.

---

## Contributing

Bug reports and pull requests are welcome. Sharing captured profiles will make sense once the capture tool exists.

---

## License

GNU Affero General Public License v3 (AGPLv3). See [`LICENSE`](LICENSE) and `ROADMAP.md`, D18. JUCE is used under its AGPLv3 option, so the whole project is AGPLv3.
