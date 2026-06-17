# WahCapture — Claude Code Handoff Document

## Project Overview

WahCapture is an open source, fully offline desktop tool for capturing the character of real wah pedals into portable `.wah` profile files. It consists of three components:

1. **Capture tool** — desktop app (Windows/Mac/Linux) that measures a physical wah pedal connected via audio interface and outputs a `.wah` file
2. **Editor** — desktop app for building `.wah` profiles from scratch by hand, without a physical pedal
3. **libwah** — shared C/C++ DSP core used by the capture tool, editor, VST3/AU plugin, and hardware firmware

The project is distributed via GitHub releases as prebuilt binaries. No server, no account, no internet connection required at any point.

---

## The .wah File Format

A `.wah` file is a human-readable TOML text file. It fully describes the character of a wah pedal as a set of filter parameters extracted from measurement.

```toml
[meta]
name = "Clyde McCoy '67"
author = "rodrigo"
make = "Vox"
model = "V846"
description = "1967 original Fasel, bought in London"
created = "2026-06-17T14:32:00Z"

[filter]
topology = "bandpass_2pole"   # "bandpass_2pole" or "bandpass_4pole"
freq_heel = 420               # Hz, center frequency at heel (pedal fully back)
freq_toe = 2180               # Hz, center frequency at toe (pedal fully forward)
q_min = 4.1                   # Q at the position where it is lowest
q_max = 4.6                   # Q at the position where it is highest
peak_gain_db = 17.2           # dB boost at resonant peak
bass_shelf_db = -2.1          # dB, low frequency shelf relative to unity
bass_shelf_freq = 200         # Hz, shelf frequency

[sweep]
curve = "log"                 # "linear", "log", or "custom"
lut = [0.000, 0.012, ...]     # 128-point lookup table, used when curve = "custom"
                              # maps normalized pedal position (0.0–1.0) to
                              # normalized frequency position (0.0–1.0)

[levels]
input_gain_db = 0.0
output_gain_db = 0.0
mix = 1.0                     # 1.0 = fully wet

[dynamics]
resonance_low = 0.0           # resonance nonlinearity at quiet input level
resonance_mid = 0.3           # resonance nonlinearity at medium input level
resonance_high = 0.6          # resonance nonlinearity at hot input level
                              # these describe how Q and peak_gain compress
                              # as input level increases (0.0 = no compression)
```

### Notes on the format

- No fingerprint or signing — the project is open source and offline; any such mechanism is trivially bypassable. Authorship is by community convention (upload timestamp on sharing platforms).
- `topology` is inferred automatically during capture from which model fits the measured data better. Users do not set this manually.
- `q_min` / `q_max` rather than a single Q value because some pedals (especially vintage) exhibit meaningful Q variation across the sweep range.
- The `dynamics` block captures how the pedal's resonance compresses at higher input levels — the "alive" quality of vintage inductors. Three input level stages are used during capture to populate these values.

---

## libwah — DSP Core

The DSP core is a standalone C/C++ library that all other components depend on. It must have no platform-specific dependencies.

### Responsibilities

- Parse `.wah` TOML files
- Maintain biquad filter state
- Accept normalized pedal position (0.0–1.0) as input
- Apply sweep curve LUT interpolation to map position → center frequency
- Recompute biquad coefficients when pedal position changes beyond threshold (0.002)
- Interpolate smoothly between old and new coefficients to avoid zippering artifacts
- Apply resonance nonlinearity scaling based on current input RMS level
- Process audio buffers

### Biquad implementation notes

- Target sample rates: 44100, 48000, 96000 Hz
- Buffer sizes: 32–256 samples (must work at 32 for low-latency hardware use)
- Coefficient recomputation is triggered by pedal position change, not per-sample
- Interpolation window for coefficient transitions: ~5ms
- Levenberg-Marquardt nonlinear least squares is used during capture for parameter fitting — this is not needed in the realtime DSP path

### Topology implementations required

- `bandpass_2pole` — standard second-order bandpass biquad
- `bandpass_4pole` — two cascaded second-order bandpass biquads

---

## Capture Tool

### Hardware setup

```
Audio interface output → wah pedal input
Wah pedal output → audio interface input
Audio interface → computer via USB
```

No expression pedal input is required. Pedal position is inferred from the measured center frequency using the heel and toe calibration anchors. This works reliably because the heel/toe captures provide exact frequency anchors at both ends of the sweep.

The tool assumes a buffered input signal (the user is expected to use a DI box / transformer isolation box). No JFET input buffer is needed in the software path.

### Capture flow — exact sequence

**Step 0 — Signal detection (automatic)**

Before anything else, the tool plays a test tone and checks that signal is being received at the audio interface input. If no signal is detected, show: "No signal detected — check your connections." Do not proceed until signal is confirmed.

**Step 1 — Heel calibration**

Prompt: "Place your pedal in the heel position and hold it"
User confirms with a button. Tool captures frequency response at heel position at three input levels (-20 dBu, -10 dBu, 0 dBu). Takes approximately 3 seconds per level automatically after confirmation. No user interaction during capture.

**Step 2 — Toe calibration**

Prompt: "Now move to the full toe position and hold it"
Same as heel. Three input levels, automatic, ~3 seconds per level.

**Step 3 — Sweep, Stage 1 (quiet)**

Prompt: "Stage 1 of 3 — Rock the pedal slowly following the guide"
Display: animated breathing bar that completes one full cycle (heel→toe→heel) every 5 seconds.
Duration: three full cycles (~15 seconds).
Input level: -20 dBu.

**Step 4 — Sweep, Stage 2 (medium)**

Prompt: "Stage 2 of 3 — Rock the pedal slowly following the guide"
Same breathing bar, same timing.
Input level: -10 dBu.

**Step 5 — Sweep, Stage 3 (hot)**

Prompt: "Stage 3 of 3 — Rock the pedal slowly following the guide"
Same breathing bar, same timing.
Input level: 0 dBu.

**Step 6 — Metadata and export**

Form fields:
- Name (required)
- Author
- Make
- Model
- Description (multiline)

Button: "Export .wah"

No result screen, no parameter display to the user. The capture is either good or needs to be retaken. A "Retake" button should be available alongside "Export .wah".

### Total capture time

Under 2 minutes for a complete capture including all three sweep stages.

### Fitting algorithm

After all sweep stages are complete, the tool runs Levenberg-Marquardt nonlinear least squares fitting to extract:

- Center frequency at each measured pedal position
- Q at each position (may vary — store as q_min/q_max)
- Peak gain
- Bass shelf frequency and gain
- Sweep curve shape → output as LUT if nonlinear, "log" or "linear" if it matches a standard curve within tolerance
- Topology: fit both 2-pole and 4-pole models, use whichever has lower residual error
- Dynamics: resonance_low/mid/high from comparing Q and peak_gain across the three input level stages

---

## Editor

The editor allows users to create `.wah` profiles from scratch without a physical pedal. It is a separate tool from the capture tool but shares the same libwah core for the filter model.

### UI components

- **Frequency range** — heel and toe frequency sliders (20 Hz – 5 kHz range)
- **Q knob** — single value; if the user wants q_min ≠ q_max they set both explicitly
- **Peak gain** — slider, 0–24 dB
- **Bass shelf** — gain slider + frequency slider
- **Sweep curve editor** — XY graph, draggable curve, exports to LUT. Presets: linear, log
- **Topology selector** — 2-pole / 4-pole dropdown
- **Levels** — input gain, output gain, mix
- **Dynamics** — three sliders for resonance_low/mid/high
- **Frequency response preview** — shows the bandpass curve at heel, midpoint, and toe positions. Updates live as parameters change. This is the central UI element.
- **Metadata fields** — same as capture tool export screen

---

## VST3/AU Plugin

A thin JUCE wrapper around libwah for DAW use. The plugin:

- Loads a `.wah` file via a file picker
- Accepts pedal position from: MIDI CC (configurable, default CC11), DAW automation lane, or audio sidechain envelope follower (auto-wah mode)
- Is essentially zero CPU — a few biquads at 48kHz
- Supports VST3, AU, and standalone targets from a single JUCE codebase

Auto-wah parameters (envelope sensitivity, attack ms, release ms, direction) are plugin-side only and are not stored in the `.wah` file — they are properties of how the filter is driven, not of the pedal itself.

---

## Hardware Device (Reference Implementation)

A Daisy Seed-based hardware wah pedal that loads `.wah` profiles from SD card or onboard flash.

### Hardware

- **Daisy Seed** (STM32H7, 480 MHz, AK4556 codec onboard) — main board
- **Expression pedal input** — TRS jack, reads pot position via ADC
- **SD card slot** — for profile storage and hot-swapping
- **Rotary encoder + small OLED** — for profile selection without a computer
- **Relay true bypass** — signal passes through relay when effect is off
- **Input impedance note** — the AK4556 codec input impedance is ~10kΩ. The device expects a buffered input signal. This should be documented clearly. A JFET input buffer is intentionally omitted to keep the enclosure minimal.

### Firmware flow

```
Boot
→ Read .wah from SD (priority) or onboard flash
→ Parse profile via libwah
→ Initialize biquad coefficients

Audio loop:
→ Read expression pedal ADC → normalize 0.0–1.0
→ Apply sweep curve LUT interpolation → center frequency
→ If position delta > 0.002: recompute biquad coefficients
→ Interpolate coefficients over ~5ms window
→ Process audio buffer
→ Output
```

Buffer size target: 32–64 samples for sub-1ms latency.

---

## Repository Structure

```
wahcapture/
├── libwah/                  # Shared DSP core (C/C++, no platform deps)
│   ├── wah_parser.h/cpp     # .wah TOML parser
│   ├── wah_dsp.h/cpp        # Biquad engine, sweep interpolation
│   └── wah_fit.h/cpp        # Levenberg-Marquardt fitting (capture only)
├── capture-tool/            # Desktop capture application
│   ├── src/
│   └── CMakeLists.txt
├── editor/                  # Desktop editor application
│   ├── src/
│   └── CMakeLists.txt
├── plugin/                  # JUCE VST3/AU plugin
│   ├── src/
│   └── CMakeLists.txt
├── firmware/                # Daisy Seed reference firmware
│   ├── src/
│   └── CMakeLists.txt
├── spec/
│   └── WAH_FORMAT.md        # .wah file format specification (public doc)
├── examples/                # Example .wah files
└── README.md
```

---

## Technology Choices

| Component | Technology | Reason |
|---|---|---|
| DSP core | C/C++ | Shared across desktop, plugin, firmware |
| TOML parsing | toml++ (header-only) | No dependencies, works on embedded |
| Curve fitting | Custom L-M implementation or ceres-solver | Accuracy of parameter extraction |
| Desktop UI | To be decided — JUCE or Qt | Cross-platform, JUCE reuse with plugin |
| Plugin format | JUCE | VST3/AU/Standalone from one codebase |
| Firmware platform | Daisy Seed / libDaisy | STM32H7, onboard codec, well-documented |
| Build system | CMake | Cross-platform, works with CLion |
| Distribution | GitHub Releases | Prebuilt binaries + source |

---

## Key Design Decisions and Rationale

**No server, no account, no internet.** The tool is downloaded from GitHub and runs entirely locally. This is a hard constraint, not a preference.

**No fingerprinting or DRM.** The project is open source. Any protection mechanism is trivially bypassable by reading the source. Authorship is by community convention (upload timestamps). Complexity not justified.

**Position inferred from frequency, not a separate input.** The heel/toe calibration anchors make this accurate enough. Eliminates the need for a separate expression pedal input during capture.

**Three separate sweep stages at different input levels** rather than interleaved multi-level measurement during a single sweep. Simpler implementation, cleaner measurements, one variable changing at a time.

**Slow sweep rhythm (~5 seconds per cycle).** Gives analysis windows time to settle at each frequency. The breathing bar guides the user to this pace naturally.

**No result screen after capture.** Parameters are meaningless to most users. The real validation is playing the profile. A retake option is sufficient.

**Buffered input assumed on hardware.** AK4556 input impedance is ~10kΩ. A JFET front end is omitted to keep the enclosure minimal. Documented requirement rather than engineered solution.

---

## What Is Not In Scope (Intentionally)

- Harmonic distortion capture (nonlinear pedal behavior beyond resonance nonlinearity)
- Auto-wah parameters in the `.wah` format (these are playback-side settings, not pedal properties)
- Cloud sync or profile marketplace (out of scope for the tool itself; community handles this)
- Wah pedals with built-in dirt stages (filter-only capture; dirt is out of scope)
