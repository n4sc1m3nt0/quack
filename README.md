# Quack

**One expression pedal. Every wah ever made.**

Quack is an open source toolkit for capturing the character of real wah pedals into portable `.wah` profile files — and playing them back on hardware or in your DAW. Inspired by the way neural amp modelers have replaced amp collections, Quack does the same for wah pedals.

---

## The idea

A wah pedal is a bandpass filter swept by your foot. The difference between a 1967 Vox V846 and a modern Cry Baby is a handful of numbers — center frequency range, Q, peak gain, the curve of the sweep. Quack measures those numbers from a real pedal and stores them in a simple text file.

That file loads onto a Daisy-based hardware device, a VST3/AU plugin in your DAW, or a standalone desktop app. The pedal under your foot always feels the same. The sound is whatever you loaded.

---

## What's in this repo

| Component | Status | Description |
|---|---|---|
| `libwah` | ✓ Complete | Core DSP library (C++17, zero dependencies) |
| `capture-tool` | 🚧 In progress | Desktop app to measure a real wah pedal and output a `.wah` file |
| `editor` | 🔴 Planned | Desktop app to build `.wah` profiles from scratch by hand |
| `plugin` | 🔴 Planned | VST3 / AU plugin (JUCE) for DAW use |
| `firmware` | 🔴 Planned | Reference firmware for a Daisy Seed hardware device |
| `spec/` | ✓ Complete | `.wah` file format specification |
| `examples/` | ✓ Sample profiles included | Example `.wah` profiles |

---

## Capturing a pedal

You need a wah pedal, an audio interface, and a transformer isolation box (DI box). No expression pedal input required — the capture tool infers pedal position from the audio signal itself.

**Setup:**
```
Audio interface output → wah pedal input → wah pedal output → audio interface input
```

**The capture process takes under two minutes:**

1. The tool checks your signal path is connected
2. Hold the pedal at heel — confirm
3. Hold the pedal at toe — confirm
4. Rock the pedal slowly following the on-screen guide — three passes at three input levels
5. Name your profile, export your `.wah`

That's it. No technical knowledge required. The math is invisible.

---

## The .wah format

A `.wah` file is a plain TOML text file — human-readable, hand-editable, and tiny.

```toml
[meta]
name        = "Clyde McCoy '67"
author      = "rodrigo"
make        = "Vox"
model       = "V846"
description = "1967 original Fasel, bought in London"
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

The full format specification is in [`spec/WAH_FORMAT.md`](spec/WAH_FORMAT.md).

---

## Hardware

The reference hardware device is built around a **Daisy Seed** — a small STM32H7-based audio board with an onboard codec. The enclosure is minimal: audio in, audio out, expression pedal input, power, and an encoder with a small OLED to select profiles.

Profiles load from an SD card or onboard flash. Hot-swap profiles mid-gig by swapping the card.

> **Note:** The Daisy Seed's codec expects a buffered input signal. Use a DI box or ensure your signal chain has a buffer before the device. A JFET input stage is intentionally omitted to keep the hardware small and simple.

---

## DAW use

The JUCE plugin loads any `.wah` file and accepts pedal position from:

- MIDI CC (default CC11, configurable)
- DAW automation lane
- Audio sidechain envelope follower (auto-wah)

CPU cost is negligible — a few biquad filters at 48kHz.

---

## How accurate is it?

Very. A wah pedal is a linear resonant bandpass filter — one of the simplest systems in audio electronics. Unlike amp capturing, which approximates complex tube nonlinearities with neural networks, wah capture measures something that already has a clean mathematical description.

For modern production pedals the result is indistinguishable from the real thing. For vintage pedals with hand-wound inductors, the `[dynamics]` block captures the subtle resonance compression that gives them their organic character — measured across three input levels during capture.

---

## Building from source

```bash
git clone https://github.com/yourusername/quack
cd quack
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Current state:** The `libwah` core library is complete with zero external dependencies. It compiles on any system with CMake 3.16+ and a C++17 compiler.

Requires:
- CMake 3.16+
- C++17 compiler (GCC, Clang, MSVC)
- No external libraries (fully self-contained)

Future components (UI tools, plugin, firmware) will have additional dependencies as needed.

---

## Contributing

Profiles, bug reports, and pull requests are welcome. If you capture a pedal and want to share it, open a PR adding it to `examples/` — include make, model, and any relevant notes about the specific unit.

If you find a pedal whose character the current model doesn't capture well, open an issue with details. Edge cases are interesting.

---

## License

MIT
