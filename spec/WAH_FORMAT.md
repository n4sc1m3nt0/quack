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


---

## Format v1.1: curves per position

Version 1.1 adds optional blocks, `[curves]` and the pair `[shape_heel]` and `[shape_toe]` (the next section but one), and one field, `format_version`, in `[meta]`. Nothing above changes: a v1.1 file is a v1 file plus these additions, so the reader of this toolkit, which ignores keys and blocks it does not know, still reads the v1 part of it. This section is read together with the *Versioning* paragraph above; for a file that says `format_version = "1.1"`, this section is the rule.

### What it holds

The v1 `[filter]` block gives the two ends of the sweep (heel and toe) and one Q range. A capture measures more: the centre frequency at each pedal position, and the Q and the gain, which it can read reliably only at the heel and the toe (from the holds). `[curves]` stores what was measured, and only that.

```toml
[meta]
name           = "Clyde McCoy '67"
format_version = "1.1"

[curves]
position = [0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0]
freq     = [420, 480, 560, 660, 780, 920, 1100, 1300, 1550, 1850, 2180]
q        = [4.1, 4.6]
gain_db  = [17.0, 17.4]
```

| Field | Type | Required | Notes |
|---|---|---|---|
| position | array of floats | yes | Pedal position, 0.0 (heel) to 1.0 (toe) |
| freq | array of floats | yes | Centre frequency in Hz, one value per position |
| q | array of floats | no | One value per position, or two (heel, toe). Left out when not measured |
| gain_db | array of floats | no | Peak gain in dB, same sizes as `q` |

### Rules

A reader refuses the whole file (it gives no profile) if any rule is broken.

1. **Version.** `format_version` absent or `"1.0"` means v1; `"1.1"` means v1.1. Any other value is refused, with or without a `[curves]` block: a reader does not guess at a version it does not know.
2. **Block and version agree.** A `[curves]` block in a file that does not say `"1.1"` is refused.
3. **position.** From 2 to 128 values; the first is 0.0 and the last is 1.0; strictly increasing. The positions need not be evenly spaced.
4. **freq.** One value per position; each finite and greater than 0. The values are stored as measured: they are not required to rise.
5. **q and gain_db.** Each either left out (not measured), or with one value per position, or with exactly two values (heel, toe). Any other count is refused. `q` is finite and greater than 0; `gain_db` is finite and may be negative.
6. **Numbers only, on one line.** Every item of the four arrays is a number, and each array is written on one line. An item that is not a number, or a key written as a single value or as an array over several lines, refuses the file: it is never dropped or read as "not measured". A comment goes on its own line: after a value on the same line it makes the array unreadable, and the file is refused.

Keys the reader does not know are ignored, as in the rest of the format.

### For writers

- Write `format_version = "1.1"` exactly when writing `[curves]`; put `[curves]` after `[dynamics]`.
- Leave out `q` or `gain_db` that were not measured; do not write an empty array.
- Write no file for a `Curves` that breaks the rules above: `wah::profile_to_toml` gives an empty string and `wah::save_toml` gives false and leaves the file alone. `wah::check_curves` is the one check, shared with the reader, and says which rule is broken.
- Keep the v1 fields filled. For a capture, `freq_heel` and `freq_toe` should equal the first and the last value of `freq`, and `q_min`/`q_max` should agree with `q`. The reader does not check this.
- The engine does not read `[curves]` yet: it keeps using the `[filter]` fields until the engine learns the curves.

## Format v1.1: the shape at the heel and at the toe

When a capture cannot describe the pedal's response at an end of the sweep with a band-pass (a 2-pole, a 2-pole with a shelf, a 4-pole), it measures it: a table of the gain in dB at each frequency of a grid, once with the pedal at the heel and once at the toe. The pair of blocks `[shape_heel]` and `[shape_toe]` stores those two tables, and only that. This section is read together with the *Format v1.1* section above; its rules continue the numbering of that section.

```toml
[meta]
name           = "Measured pedal"
format_version = "1.1"

[shape_heel]
freq = [20, 28, 40, 57, 80, 113, 160]
db   = [-12.0, -9.5, -6.0, 0.5, 6.0, 9.5, 4.0]

[shape_toe]
freq = [20, 28, 40, 57, 80, 113, 160]
db   = [-14.0, -12.5, -10.0, -4.5, 3.0, 8.5, 12.0]
```

(The arrays are shortened to fit the page; a capture writes 64 values.)

| Field | Type | Required | Notes |
|---|---|---|---|
| freq | array of floats | yes | Frequency in Hz of each cell of the table |
| db | array of floats | yes | Gain in dB at each frequency, one value per `freq` |

### Rules

A reader refuses the whole file (it gives no profile) if any rule is broken; the reason names the block and the key.

7. **Block and version agree.** `[shape_heel]` or `[shape_toe]` in a file that does not say `"1.1"` is refused (rule 1 says which versions a reader knows).
8. **Both or none.** `[shape_heel]` without `[shape_toe]`, or the other way round, is refused: a pedal with one end measured and the other modelled has no profile in this format yet.
9. **freq and db.** In each block, `freq` has from 2 to 128 values, each finite and greater than 0, strictly increasing; `db` has one value per `freq`, each finite (it may be negative). The two blocks need not have the same frequencies or the same number of values.
10. **Numbers only, on one line.** Rule 6 holds for the four arrays of the two blocks: every item is a number, each array is on one line, and a comment goes on its own line (after the array on the same line, the file is refused).
11. **Independent of `[curves]`.** A file may have `[curves]`, the two shape blocks, both or neither; neither requires the other.

Keys the reader does not know are ignored, as in the rest of the format.

### For writers

- Write `format_version = "1.1"` when writing `[curves]`, the shape blocks, or both; put `[shape_heel]` and then `[shape_toe]` after `[curves]` (after `[dynamics]` if there is none).
- Write no file for a `Shape` that breaks the rules above: `wah::profile_to_toml` gives an empty string and `wah::save_toml` gives false and leaves the file alone. `wah::check_shape` is the one check, shared with the reader, and says which rule is broken.
- `wah::profile_from_summary` writes the blocks when the shapes of both ends are Measured. A Measured end next to an end that is modelled (or has no valid shape) gives no profile, by rule 8.
- The engine does not read the shape blocks yet: it keeps using the `[filter]` fields until the engine learns the shapes (M7).

---

## Text and value rules (rounds 32 and 33)

This section says how a reader reads the text and the values of a `.wah` file and how a writer writes them. It adds no field and changes no field of v1 or v1.1; it settles what the examples above already do.

1. **Comments.** A `#` that is not inside a string starts a comment that runs to the end of the line. A comment may stand on its own line or follow a header, a string, a number or an array (as in the Full Format block above). One exception, rule 6 of format v1.1: after an array of the `[curves]` block a comment refuses the file.
2. **Strings.** A string is written in double quotes on one line. These escapes are read: `\n` (line break), `\t`, `\r`, `\b`, `\f`, `\\` (backslash), `\"` (quote) and `\uXXXX`. A backslash with any other letter is kept as it was typed. A string with no closing quote refuses the file, and so does text that is neither a comment nor empty after the closing quote.
3. **Several lines.** A `description` (or any string) may be written between `"""` and `"""`. The line break right after the opening `"""` is dropped; inside it a `#`, a `[header]` and a `key = value` are text. A `"""` string that is never closed refuses the file. A writer does not use this form: it writes a line break as `\n`, so that every value is on one line.
4. **Headers.** Each header (`[meta]`, `[filter]`, ...) is written once. A header written twice refuses the file; the reader never keeps the first block and drops the second, or the other way round.
5. **Numbers.** A writer writes a number as the shortest text that reads back as the same 32-bit float: 4.2 is written `4.2`, and 450 is written `450`.
6. **Numbers.** A number field (every field of `[filter]`, `[levels]` and `[dynamics]` except `topology`) holds one number that is finite and fits a 32-bit float. `nan`, `inf`, a number too big for a float, a word, an empty value and an array refuse the file, and the reason names the section and the key. A number too small for a float is accepted: a denormal such as `7e-45` is read as it is, and `1e-50` is read as 0. (Before, such a field kept its default in silence.)
7. **Words.** `topology` is `"bandpass_2pole"` or `"bandpass_4pole"`, and `curve` is `"linear"`, `"log"` or `"custom"`. Any other value, a number or an array refuses the file; it is no longer read as the 2-pole or ignored.
8. **lut.** With `curve = "custom"` the `lut` is exactly 128 numbers written on one line; any other count, an item that is not a number, a missing `lut` or a single value refuses the file. With any other curve the `lut` is not read, whatever it holds (a writer writes `lut = []`). The «Full .wah» example above shortens its `lut` with `...` to fit the page; a real file writes all 128 numbers.
9. **One key, once.** A key written twice in a section refuses the file, even with the same value. The same key in two sections is fine. (Before, the last one won in silence.)
