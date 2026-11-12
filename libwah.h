/**
 * libwah.h
 * WahCapture DSP Core
 *
 * Shared C++ library used by:
 *   - capture tool (desktop)
 *   - editor (desktop)
 *   - VST3/AU plugin (JUCE)
 *   - Daisy Seed hardware firmware
 *
 * No platform-specific dependencies. Pure C++17.
 * TOML parsing via toml++ (header-only, bundled).
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <cmath>

namespace wah {

// ─────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────

constexpr int   LUT_SIZE               = 128;
constexpr float POSITION_THRESHOLD     = 0.002f;  // min delta to trigger coeff recompute
constexpr float COEFF_INTERP_MS        = 5.0f;    // coefficient interpolation window
constexpr int   MAX_SHAPE_POINTS       = 128;     // most cells in a v1.1 [shape_heel] or [shape_toe] block (spec/WAH_FORMAT.md)
constexpr int   MAX_CURVE_POINTS       = 128;     // most positions in a v1.1 [curves] block (spec/WAH_FORMAT.md)

// ─────────────────────────────────────────────
// Profile data structures
// ─────────────────────────────────────────────

enum class Topology {
    Bandpass2Pole,
    Bandpass4Pole
};

enum class SweepCurve {
    Linear,
    Log,
    Custom
};

struct Meta {
    std::string name;
    std::string author;
    std::string make;
    std::string model;
    std::string description;
    std::string created;
};

struct Filter {
    Topology    topology        = Topology::Bandpass2Pole;
    float       freq_heel       = 450.0f;   // Hz
    float       freq_toe        = 2100.0f;  // Hz
    float       q_min           = 4.0f;
    float       q_max           = 4.0f;
    float       peak_gain_db    = 18.0f;
    float       bass_shelf_db   = 0.0f;
    float       bass_shelf_freq = 200.0f;   // Hz
};

struct Sweep {
    SweepCurve              curve = SweepCurve::Log;
    std::array<float, LUT_SIZE> lut{};  // used when curve == Custom
};

struct Levels {
    float input_gain_db  = 0.0f;
    float output_gain_db = 0.0f;
    float mix            = 1.0f;
};

struct Dynamics {
    float resonance_low  = 0.0f;  // at -20 dBu
    float resonance_mid  = 0.0f;  // at -10 dBu
    float resonance_high = 0.0f;  // at   0 dBu
};

// Format v1.1 (D6): what a capture measured at each pedal position. Optional: a v1 profile has none and the engine keeps
// using the Filter fields. q and gain_db are empty when not measured; otherwise one value per position, or just two
// (heel, toe) when only the ends were measured (the holds). The rules the reader enforces are in spec/WAH_FORMAT.md.
struct Curves {
    std::vector<float> position;  // 0.0 (heel) to 1.0 (toe), strictly increasing, 2 to MAX_CURVE_POINTS values
    std::vector<float> freq;      // Hz, one per position
    std::vector<float> q;         // empty, or one per position, or 2 (heel, toe)
    std::vector<float> gain_db;   // empty, or one per position, or 2 (heel, toe)
};

// Format v1.1 (D71): the gain in dB at each frequency of a grid, measured once with the pedal at the heel and once at the toe. Used when
// a capture cannot describe an end of the sweep with a band-pass (a Measured shape). Optional: both tables or none. The rules the
// reader enforces are in spec/WAH_FORMAT.md; the engine ignores them until M7.
struct ShapeTable {
    std::vector<float> freq;  // Hz, finite, above 0, strictly increasing, 2 to MAX_SHAPE_POINTS values
    std::vector<float> db;    // finite, one per freq
};
struct Shape {
    ShapeTable heel;
    ShapeTable toe;
};

struct Profile {
    Meta     meta;
    Filter   filter;
    Sweep    sweep;
    Levels   levels;
    Dynamics dynamics;
    std::optional<Curves> curves;  // v1.1; nullopt for a v1 profile
    std::optional<Shape>  shape;   // v1.1; nullopt for a v1 profile or a capture that did not measure the ends (D71)
};

// ─────────────────────────────────────────────
// Parser
// ─────────────────────────────────────────────

/**
 * Load a .wah profile from a file path.
 * Returns nullopt on parse error.
 */
std::optional<Profile> load(const std::string& path);

/**
 * Save a .wah profile to a file path.
 * Returns false on write error.
 */
bool save(const Profile& profile, const std::string& path);

/**
 * Serialize a profile to a TOML string (for in-memory use).
 */
std::string serialize(const Profile& profile);

// ─────────────────────────────────────────────
// DSP Engine
// ─────────────────────────────────────────────

/**
 * Biquad coefficient set for a single second-order section.
 */
struct BiquadCoeffs {
    float b0, b1, b2;   // numerator
    float a1, a2;       // denominator (a0 normalized to 1.0)
};

/**
 * Biquad state (per channel, per section).
 */
struct BiquadState {
    float z1 = 0.0f;
    float z2 = 0.0f;
};

/**
 * Main DSP engine. One instance per audio channel (mono: one instance).
 *
 * Usage:
 *   Engine engine;
 *   engine.init(profile, sample_rate);
 *   engine.set_position(0.5f);           // called from UI/MIDI thread
 *   engine.process(buffer, num_samples); // called from audio thread
 */
class Engine {
public:
    Engine() = default;
    ~Engine() = default;

    /**
     * Initialize with a profile and sample rate.
     * Must be called before process().
     * Safe to call again to load a new profile (causes a brief interruption).
     */
    void init(const Profile& profile, float sample_rate);

    /**
     * Set normalized pedal position [0.0, 1.0].
     * 0.0 = heel (low frequency), 1.0 = toe (high frequency).
     * Thread-safe — may be called from UI or MIDI thread.
     * Triggers coefficient recomputation if delta > POSITION_THRESHOLD.
     */
    void set_position(float position);

    /**
     * Process a mono audio buffer in-place.
     * num_samples should match the audio interface buffer size.
     * Called from the audio thread.
     */
    void process(float* buffer, int num_samples);

    /**
     * Reset DSP state (clear biquad delay lines).
     */
    void reset();

    // Current state accessors (for metering/display)
    float get_position() const;
    float get_center_freq() const;
    float get_current_q() const;

private:
    Profile     profile_;
    float       sample_rate_    = 48000.0f;
    float       position_       = 0.0f;
    float       center_freq_    = 450.0f;

    // Up to 2 cascaded biquad sections (4-pole uses both)
    BiquadCoeffs    target_coeffs_[2]{};
    BiquadCoeffs    current_coeffs_[2]{};
    BiquadState     state_[2]{};

    // Coefficient interpolation
    int     interp_samples_remaining_  = 0;
    int     interp_samples_total_      = 0;

    // Input level tracking for dynamics
    float   rms_level_          = 0.0f;
    float   rms_smooth_coeff_   = 0.99f;

    void    compute_coeffs(float center_freq, float q, float gain_db,
                           BiquadCoeffs* out, int num_stages);
    float   position_to_freq(float position) const;
    float   compute_q(float position) const;
    float   compute_dynamic_scale(float rms_linear) const;
    void    update_rms(const float* buffer, int num_samples);
};

// ─────────────────────────────────────────────
// Capture / Fitting (capture tool only)
// Not needed in firmware or plugin
// ─────────────────────────────────────────────

/**
 * A single measurement sample: pedal position inferred from frequency,
 * with extracted filter parameters at that position.
 */
struct MeasurementSample {
    float position;         // 0.0–1.0, inferred from center_freq
    float center_freq;      // Hz
    float q;
    float peak_gain_db;
    float input_level_dbu;  // -20, -10, or 0
};

// fit_profile is declared in wah_fit.h (with FitOptions)

/**
 * Infer normalized pedal position from a measured center frequency,
 * given known heel and toe frequencies.
 *
 * Returns a value in [0.0, 1.0] where:
 *   0.0 = heel (center_freq == heel_freq)
 *   1.0 = toe  (center_freq == toe_freq)
 *
 * Uses logarithmic interpolation (wah pedals sweep logarithmically).
 */
float infer_position(float center_freq, float heel_freq, float toe_freq);

} // namespace wah
