/**
 * wah_fit.cpp
 * Levenberg-Marquardt curve fitting implementation.
 */

#include "wah_fit.h"
#include <cmath>
#include <algorithm>

namespace wah {

// ─────────────────────────────────────────────
// Sweep Curve Fitting
// ─────────────────────────────────────────────

/**
 * Fit a logarithmic sweep curve: freq = heel * exp(position * log(toe/heel))
 * Returns the fit error (RMSE).
 */
float fit_log_sweep(const std::vector<MeasurementSample>& samples,
                    float heel_freq, float toe_freq) {
    if (samples.empty()) return 1e6f;

    float log_heel = std::log(heel_freq);
    float log_toe = std::log(toe_freq);
    float sum_sq_error = 0.0f;

    for (const auto& s : samples) {
        float expected_freq = heel_freq * std::exp(s.position * std::log(toe_freq / heel_freq));
        float error = s.center_freq - expected_freq;
        sum_sq_error += error * error;
    }

    return std::sqrt(sum_sq_error / samples.size());
}

/**
 * Fit a linear sweep curve: freq = heel + position * (toe - heel)
 * Returns the fit error (RMSE).
 */
float fit_linear_sweep(const std::vector<MeasurementSample>& samples,
                       float heel_freq, float toe_freq) {
    if (samples.empty()) return 1e6f;

    float sum_sq_error = 0.0f;
    for (const auto& s : samples) {
        float expected_freq = heel_freq + s.position * (toe_freq - heel_freq);
        float error = s.center_freq - expected_freq;
        sum_sq_error += error * error;
    }

    return std::sqrt(sum_sq_error / samples.size());
}

/**
 * Build a 128-point LUT that maps normalized position [0,1] to frequency position [0,1].
 * Uses measured data to fit the curve.
 */
void build_sweep_lut(const std::vector<MeasurementSample>& samples,
                     float heel_freq, float toe_freq,
                     std::array<float, LUT_SIZE>& lut) {
    // Initialize LUT at endpoints
    lut[0] = 0.0f;
    lut[LUT_SIZE - 1] = 1.0f;

    // For each LUT point, find the best-fit frequency position
    for (int i = 1; i < LUT_SIZE - 1; i++) {
        float position = (float)i / (LUT_SIZE - 1);

        // Find samples near this position
        float best_freq_position = position;  // default to linear
        float best_error = 1e6f;

        for (const auto& s : samples) {
            if (std::abs(s.position - position) < 0.05f) {
                float freq_position = (s.center_freq - heel_freq) / (toe_freq - heel_freq);
                float error = std::abs(freq_position - position);
                if (error < best_error) {
                    best_error = error;
                    best_freq_position = freq_position;
                }
            }
        }

        lut[i] = std::min(1.0f, std::max(0.0f, best_freq_position));
    }
}

/**
 * Determine sweep curve type by fitting log, linear, and LUT approaches.
 * Returns the best SweepCurve and fills the LUT if needed.
 */
SweepCurve determine_sweep_curve(const std::vector<MeasurementSample>& samples,
                                  float heel_freq, float toe_freq,
                                  std::array<float, LUT_SIZE>& lut) {
    float log_error = fit_log_sweep(samples, heel_freq, toe_freq);
    float linear_error = fit_linear_sweep(samples, heel_freq, toe_freq);

    // Log sweep is nearly always better for wah pedals (exponential frequency sweep)
    // Use linear only if log is significantly worse
    if (log_error < linear_error * 1.2f) {
        return SweepCurve::Log;
    }

    // If linear is much better, use it
    if (linear_error < log_error * 0.8f) {
        return SweepCurve::Linear;
    }

    // Otherwise, build a custom LUT
    build_sweep_lut(samples, heel_freq, toe_freq, lut);
    return SweepCurve::Custom;
}

// ─────────────────────────────────────────────
// Q and Gain Fitting
// ─────────────────────────────────────────────

/**
 * Extract Q min/max and peak gain by analyzing measurements at different positions.
 * Uses linear interpolation between heel and toe values.
 */
void fit_q_and_gain(const std::vector<MeasurementSample>& samples,
                    float heel_q, float toe_q,
                    float heel_gain_db, float toe_gain_db,
                    float& out_q_min, float& out_q_max,
                    float& out_peak_gain_db) {
    if (samples.empty()) {
        out_q_min = heel_q;
        out_q_max = toe_q;
        out_peak_gain_db = (heel_gain_db + toe_gain_db) / 2.0f;
        return;
    }

    // Collect measured Q and gain across all positions
    float min_q = heel_q;
    float max_q = toe_q;
    float sum_gain = 0.0f;
    int count = 0;

    for (const auto& s : samples) {
        min_q = std::min(min_q, s.q);
        max_q = std::max(max_q, s.q);
        sum_gain += s.peak_gain_db;
        count++;
    }

    if (count == 0) {
        out_q_min = heel_q;
        out_q_max = toe_q;
        out_peak_gain_db = (heel_gain_db + toe_gain_db) / 2.0f;
        return;
    }

    out_q_min = std::min(heel_q, min_q);
    out_q_max = std::max(toe_q, max_q);
    out_peak_gain_db = sum_gain / count;
}

// ─────────────────────────────────────────────
// Topology Detection
// ─────────────────────────────────────────────

/**
 * Estimate the fitting error for a given topology by comparing
 * measured Q values to the expected model.
 */
float estimate_topology_error(const std::vector<MeasurementSample>& samples,
                              float q_min, float q_max,
                              Topology topology) {
    if (samples.empty()) return 0.0f;

    // For 4-pole, Q should be higher (cascaded sections increase Q)
    // Estimate: 4-pole Q ≈ (2-pole Q)^2 / constant
    float scale = (topology == Topology::Bandpass4Pole) ? 1.5f : 1.0f;

    float sum_sq_error = 0.0f;
    for (const auto& s : samples) {
        float expected_q = q_min + s.position * (q_max - q_min);
        expected_q *= scale;
        float error = s.q - expected_q;
        sum_sq_error += error * error;
    }

    return std::sqrt(sum_sq_error / samples.size());
}

Topology detect_topology(const std::vector<MeasurementSample>& samples,
                        float q_min, float q_max) {
    float error_2pole = estimate_topology_error(samples, q_min, q_max, Topology::Bandpass2Pole);
    float error_4pole = estimate_topology_error(samples, q_min, q_max, Topology::Bandpass4Pole);

    // Use 4-pole if its error is significantly lower
    return (error_4pole < error_2pole * 0.85f) ? Topology::Bandpass4Pole : Topology::Bandpass2Pole;
}

// ─────────────────────────────────────────────
// Dynamics Extraction
// ─────────────────────────────────────────────

/**
 * Extract resonance nonlinearity at three input levels.
 * Resonance nonlinearity is estimated from how Q and peak_gain compress
 * as input level increases.
 */
void extract_dynamics(const std::vector<MeasurementSample>& samples,
                      float q_min, float q_max,
                      Dynamics& out_dynamics) {
    // Separate samples by input level
    std::vector<MeasurementSample> low_level;   // -20 dBu
    std::vector<MeasurementSample> mid_level;   // -10 dBu
    std::vector<MeasurementSample> high_level;  // 0 dBu

    for (const auto& s : samples) {
        if (s.input_level_dbu <= -20.0f) {
            low_level.push_back(s);
        } else if (s.input_level_dbu <= -10.0f) {
            mid_level.push_back(s);
        } else {
            high_level.push_back(s);
        }
    }

    // Helper: compute average compression from Q/gain at a level vs. expected
    auto compute_compression = [](const std::vector<MeasurementSample>& level_samples,
                                   float q_min, float q_max) -> float {
        if (level_samples.empty()) return 0.0f;

        float sum_compression = 0.0f;
        for (const auto& s : level_samples) {
            float expected_q = q_min + s.position * (q_max - q_min);
            // Compression is how much Q is reduced from ideal
            float compression = std::max(0.0f, (expected_q - s.q) / expected_q);
            sum_compression += compression;
        }
        return sum_compression / level_samples.size();
    };

    out_dynamics.resonance_low = compute_compression(low_level, q_min, q_max);
    out_dynamics.resonance_mid = compute_compression(mid_level, q_min, q_max);
    out_dynamics.resonance_high = compute_compression(high_level, q_min, q_max);

    // Clamp to [0, 1] range
    out_dynamics.resonance_low = std::min(1.0f, std::max(0.0f, out_dynamics.resonance_low));
    out_dynamics.resonance_mid = std::min(1.0f, std::max(0.0f, out_dynamics.resonance_mid));
    out_dynamics.resonance_high = std::min(1.0f, std::max(0.0f, out_dynamics.resonance_high));
}

// ─────────────────────────────────────────────
// Bass Shelf Fitting
// ─────────────────────────────────────────────

/**
 * Fit bass shelf parameters (optional).
 * For now, we skip this—it requires frequency response measurement below 200 Hz
 * which is less common in a simple wah capture.
 */
void fit_bass_shelf(const std::vector<MeasurementSample>& samples,
                    float& shelf_freq, float& shelf_db) {
    shelf_freq = 200.0f;
    shelf_db = 0.0f;
}

// ─────────────────────────────────────────────
// Main Fitting Function
// ─────────────────────────────────────────────

std::optional<Profile> fit_profile(
    const std::vector<MeasurementSample>& samples,
    float heel_freq,
    float toe_freq,
    float heel_q,
    float toe_q,
    float heel_gain_db,
    float toe_gain_db,
    const FitOptions& options) {

    // Sanity checks
    if (samples.empty() || heel_freq <= 0 || toe_freq <= 0) {
        return {};
    }

    Profile profile;
    profile.meta.name = "Fitted Wah";
    profile.meta.author = "wahcapture";
    profile.meta.created = "";  // Will be set by caller

    // Fit sweep curve
    profile.sweep.curve = determine_sweep_curve(samples, heel_freq, toe_freq, profile.sweep.lut);

    // Fit Q and gain
    float q_min, q_max, peak_gain_db;
    fit_q_and_gain(samples, heel_q, toe_q, heel_gain_db, toe_gain_db,
                   q_min, q_max, peak_gain_db);

    // Detect topology
    Topology topology = detect_topology(samples, q_min, q_max);

    // Extract dynamics
    Dynamics dynamics;
    extract_dynamics(samples, q_min, q_max, dynamics);

    // Fit bass shelf (currently stubbed)
    float shelf_freq, shelf_db;
    fit_bass_shelf(samples, shelf_freq, shelf_db);

    // Populate filter structure
    profile.filter.topology = topology;
    profile.filter.freq_heel = heel_freq;
    profile.filter.freq_toe = toe_freq;
    profile.filter.q_min = q_min;
    profile.filter.q_max = q_max;
    profile.filter.peak_gain_db = peak_gain_db;
    profile.filter.bass_shelf_freq = shelf_freq;
    profile.filter.bass_shelf_db = shelf_db;

    profile.dynamics = dynamics;

    return profile;
}

}  // namespace wah
