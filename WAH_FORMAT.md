# .wah File Format Specification
# WahCapture Project — v1.0

## Overview

A `.wah` file is a human-readable TOML text file that fully describes the character
of a wah pedal as a parametric filter model. Files are created either by the
WahCapture capture tool (from a physical pedal measurement) or by the WahCapture
editor (by hand).

Files are plain text, fully editable, and carry no encryption or signing.
Authorship is established by community convention.

---

## Full Format

```toml
[meta]
name        = ""        # required — display name of the profile
author      = ""        # creator's name or handle
make        = ""        # pedal manufacturer (e.g. "Vox", "Dunlop")
model       = ""        # pedal model (e.g. "V846", "Cry Baby GCB-95")
description = ""        # freeform notes — vintage info, mods, etc.
created     = ""        # ISO 8601 datetime, set automatically on export

[filter]
topology        = "bandpass_2pole"  # "bandpass_2pole" or "bandpass_4pole"
freq_heel       = 420               # Hz — center frequency at heel position
freq_toe        = 2180              # Hz — center frequency at toe position
q_min           = 4.1               # lowest Q value across the sweep
q_max           = 4.6               # highest Q value across the sweep
peak_gain_db    = 17.2              # dB boost at the resonant peak
bass_shelf_db   = -2.1              # dB — low frequency shelf relative to unity
bass_shelf_freq = 200               # Hz — shelf corner frequency

[sweep]
curve = "log"           # "linear", "log", or "custom"
lut   = []              # 128-point array, required when curve = "custom"
                        # index = normalized pedal position (0.0–1.0, 128 steps)
                        # value = normalized frequency position (0.0–1.0)
                        # interpolated linearly between points at runtime

[levels]
input_gain_db  = 0.0    # dB gain applied before the filter
output_gain_db = 0.0    # dB gain applied after the filter
mix            = 1.0    # wet/dry mix (1.0 = fully wet, 0.0 = fully dry)

[dynamics]
resonance_low  = 0.0    # resonance nonlinearity at quiet input (-20 dBu)
resonance_mid  = 0.3    # resonance nonlinearity at medium input (-10 dBu)
resonance_high = 0.6    # resonance nonlinearity at hot input (0 dBu)
                        # describes how Q and peak_gain compress as input
                        # level increases. 0.0 = no compression, 1.0 = maximum.
                        # interpolated at runtime based on input RMS level.
```

---

## Field Reference

### [meta]

| Field | Type | Required | Notes |
|---|---|---|---|
| name | string | yes | Display name |
| author | string | no | Creator name or handle |
| make | string | no | Manufacturer |
| model | string | no | Model name |
| description | string | no | Freeform, multiline supported |
| created | string | no | Set automatically by the tool |

### [filter]

| Field | Type | Unit | Typical range |
|---|---|---|---|
| topology | string | — | "bandpass_2pole" or "bandpass_4pole" |
| freq_heel | float | Hz | 300–700 |
| freq_toe | float | Hz | 1500–2500 |
| q_min | float | — | 2.0–8.0 |
| q_max | float | — | 2.0–10.0, >= q_min |
| peak_gain_db | float | dB | 12–22 |
| bass_shelf_db | float | dB | -6 to +3 |
| bass_shelf_freq | float | Hz | 100–400 |

### [sweep]

| Field | Type | Notes |
|---|---|---|
| curve | string | "linear", "log", or "custom" |
| lut | array | 128 floats, values 0.0–1.0. Required if curve = "custom", ignored otherwise |

### [levels]

All values in dB or normalized float. Default values (0.0 / 1.0) represent unity gain / fully wet.

### [dynamics]

Values are dimensionless compression coefficients 0.0–1.0. Set to 0.0 for all three to disable dynamic behavior (appropriate for modern mass-produced pedals with linear inductors).

---

## Topology Notes

**bandpass_2pole** — standard second-order bandpass biquad. Covers the vast majority of wah pedals including Dunlop Cry Baby, Vox V847, Colorsound Wah, most boutique pedals.

**bandpass_4pole** — two cascaded second-order bandpass biquads. Used for pedals with steeper filter skirts or more aggressive resonance. Selected automatically by the capture tool when it produces a significantly better fit than the 2-pole model.

---

## Sweep Curve

The sweep curve maps normalized pedal position (0.0 = heel, 1.0 = toe) to normalized frequency position (0.0 = freq_heel, 1.0 = freq_toe).

**"linear"** — pedal position maps directly to frequency. Mechanical feel.

**"log"** — pedal position maps logarithmically to frequency. More natural, vocal feel. Approximates the behavior of a standard audio-taper pot.

**"custom"** — uses the 128-point LUT. The capture tool outputs this when the measured sweep curve does not match linear or log within a tolerance threshold. Captures the exact taper of a worn or nonstandard pot.

Runtime interpolation between LUT points is linear.

---

## Dynamics / Resonance Nonlinearity

The three `resonance_*` values describe how the filter's Q and peak gain compress as input level increases. This captures the subtle magnetic saturation behavior of vintage hand-wound inductors (Fasel, Thomas) that contributes to the "alive" quality players describe.

At runtime, the firmware/plugin measures input RMS level and interpolates between the three reference points to scale Q and peak_gain accordingly.

Modern mass-produced pedals with linear inductors will have resonance_low = resonance_mid = resonance_high = 0.0. The capture tool will output this automatically when no significant level-dependent variation is detected.

---

## Example Files

### Minimal valid .wah (modern pedal, no dynamics)

```toml
[meta]
name   = "Standard Cry Baby"
author = "rodrigo"
make   = "Dunlop"
model  = "GCB-95"

[filter]
topology        = "bandpass_2pole"
freq_heel       = 450
freq_toe        = 2100
q_min           = 4.8
q_max           = 4.8
peak_gain_db    = 18.0
bass_shelf_db   = 0.0
bass_shelf_freq = 200

[sweep]
curve = "log"
lut   = []

[levels]
input_gain_db  = 0.0
output_gain_db = 0.0
mix            = 1.0

[dynamics]
resonance_low  = 0.0
resonance_mid  = 0.0
resonance_high = 0.0
```

### Full .wah (vintage pedal with dynamics)

```toml
[meta]
name        = "Clyde McCoy '67"
author      = "rodrigo"
make        = "Vox"
model       = "V846"
description = "1967 original Fasel, bought in London"
created     = "2026-06-17T14:32:00Z"

[filter]
topology        = "bandpass_2pole"
freq_heel       = 420
freq_toe        = 2180
q_min           = 4.1
q_max           = 4.6
peak_gain_db    = 17.2
bass_shelf_db   = -2.1
bass_shelf_freq = 200

[sweep]
curve = "custom"
lut   = [0.000, 0.009, 0.019, 0.031, 0.044, 0.058, ...]

[levels]
input_gain_db  = 0.0
output_gain_db = 0.0
mix            = 1.0

[dynamics]
resonance_low  = 0.0
resonance_mid  = 0.3
resonance_high = 0.6
```

---

## Versioning

This is format version 1. If breaking changes are introduced in future versions, a `format_version` field will be added to `[meta]`. Parsers should treat files without this field as version 1.
