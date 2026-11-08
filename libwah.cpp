/**
 * libwah.cpp
 * Core DSP and utility implementations.
 */

#include "libwah.h"
#include <cmath>
#include <algorithm>

namespace wah {

// ─────────────────────────────────────────────
// Position and Frequency Utilities
// ─────────────────────────────────────────────

float infer_position(float center_freq, float heel_freq, float toe_freq) {
    if (heel_freq <= 0 || toe_freq <= 0 || center_freq <= 0) {
        return 0.5f;  // degenerate case
    }

    // Clamp center frequency to the valid range
    center_freq = std::min(std::max(center_freq, heel_freq * 0.9f), toe_freq * 1.1f);

    // Wah pedals sweep logarithmically
    float log_heel = std::log(heel_freq);
    float log_toe = std::log(toe_freq);
    float log_center = std::log(center_freq);

    float position = (log_center - log_heel) / (log_toe - log_heel);
    return std::min(std::max(position, 0.0f), 1.0f);
}

// ─────────────────────────────────────────────
// Biquad Coefficient Computation
// ─────────────────────────────────────────────

/**
 * Compute biquad coefficients for a bandpass filter at a given center frequency and Q.
 * This is a standard cookbook implementation.
 */
BiquadCoeffs compute_bandpass_coeffs(float center_freq, float q, float sample_rate) {
    float w0 = 2.0f * M_PI * center_freq / sample_rate;
    float sin_w0 = std::sin(w0);
    float cos_w0 = std::cos(w0);
    float alpha = sin_w0 / (2.0f * q);

    BiquadCoeffs c;
    c.b0 = alpha;
    c.b1 = 0.0f;
    c.b2 = -alpha;
    float a0 = 1.0f + alpha;
    c.a1 = -2.0f * cos_w0 / a0;
    c.a2 = (1.0f - alpha) / a0;

    return c;
}

/**
 * Compute biquad coefficients for a peaking EQ with specified gain.
 * Used to add the peak gain and bass shelf.
 */
BiquadCoeffs compute_peak_coeffs(float center_freq, float q, float gain_db, float sample_rate) {
    float A = std::sqrt(std::pow(10.0f, gain_db / 20.0f));
    float w0 = 2.0f * M_PI * center_freq / sample_rate;
    float sin_w0 = std::sin(w0);
    float cos_w0 = std::cos(w0);
    float alpha = sin_w0 / (2.0f * q);

    BiquadCoeffs c;
    c.b0 = 1.0f + alpha * A;
    c.b1 = -2.0f * cos_w0;
    c.b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    c.a1 = -2.0f * cos_w0 / a0;
    c.a2 = (1.0f - alpha / A) / a0;

    return c;
}

/**
 * Compute coefficients for a shelf filter (bass shelf).
 */
BiquadCoeffs compute_shelf_coeffs(float shelf_freq, float gain_db, float sample_rate, bool high_shelf) {
    float A = std::sqrt(std::pow(10.0f, std::abs(gain_db) / 20.0f));
    float w0 = 2.0f * M_PI * shelf_freq / sample_rate;
    float sin_w0 = std::sin(w0);
    float cos_w0 = std::cos(w0);
    float alpha = sin_w0 / 2.0f;

    BiquadCoeffs c;
    float sign = (gain_db >= 0) ? 1.0f : -1.0f;

    if (!high_shelf) {  // low shelf
        c.b0 = A * ((A + 1.0f) - (A - 1.0f) * cos_w0 + 2.0f * std::sqrt(A) * alpha * sign);
        c.b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_w0);
        c.b2 = A * ((A + 1.0f) - (A - 1.0f) * cos_w0 - 2.0f * std::sqrt(A) * alpha * sign);
    } else {  // high shelf
        c.b0 = A * ((A + 1.0f) + (A - 1.0f) * cos_w0 + 2.0f * std::sqrt(A) * alpha * sign);
        c.b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w0);
        c.b2 = A * ((A + 1.0f) + (A - 1.0f) * cos_w0 - 2.0f * std::sqrt(A) * alpha * sign);
    }

    float a0 = (A + 1.0f) + (A - 1.0f) * cos_w0 + 2.0f * std::sqrt(A) * alpha * sign;
    c.a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w0) / a0;
    c.a2 = ((A + 1.0f) + (A - 1.0f) * cos_w0 - 2.0f * std::sqrt(A) * alpha * sign) / a0;

    return c;
}

// ─────────────────────────────────────────────
// Engine Implementation
// ─────────────────────────────────────────────

void Engine::init(const Profile& profile, float sample_rate) {
    profile_ = profile;
    sample_rate_ = sample_rate;
    position_ = 0.0f;

    // Compute initial center frequency at heel
    center_freq_ = profile_.filter.freq_heel;

    // Compute initial biquad coefficients
    float q = profile_.filter.q_min;
    compute_coeffs(center_freq_, q, profile_.filter.peak_gain_db, target_coeffs_,
                   (profile_.filter.topology == Topology::Bandpass4Pole) ? 2 : 1);
    current_coeffs_[0] = target_coeffs_[0];
    current_coeffs_[1] = target_coeffs_[1];

    // Initialize coefficient interpolation
    interp_samples_remaining_ = 0;
    interp_samples_total_ = 0;

    // Initialize RMS tracking
    rms_level_ = 0.0f;
    rms_smooth_coeff_ = std::exp(-2.0f * M_PI * 10.0f / sample_rate_);  // 10 Hz smoothing

    reset();
}

void Engine::reset() {
    state_[0].z1 = 0.0f;
    state_[0].z2 = 0.0f;
    state_[1].z1 = 0.0f;
    state_[1].z2 = 0.0f;
    rms_level_ = 0.0f;
}

void Engine::set_position(float position) {
    position = std::min(std::max(position, 0.0f), 1.0f);

    if (std::abs(position - position_) <= POSITION_THRESHOLD) {
        return;  // no change
    }

    position_ = position;

    // Compute new center frequency
    float log_heel = std::log(profile_.filter.freq_heel);
    float log_toe = std::log(profile_.filter.freq_toe);
    float log_freq = log_heel + (log_toe - log_heel) * position_;
    center_freq_ = std::exp(log_freq);

    // Interpolate Q between q_min and q_max
    float q = profile_.filter.q_min + (profile_.filter.q_max - profile_.filter.q_min) * position_;

    // Compute new coefficients
    compute_coeffs(center_freq_, q, profile_.filter.peak_gain_db, target_coeffs_,
                   (profile_.filter.topology == Topology::Bandpass4Pole) ? 2 : 1);

    // Start coefficient interpolation
    interp_samples_total_ = (int)(COEFF_INTERP_MS * sample_rate_ / 1000.0f);
    interp_samples_remaining_ = interp_samples_total_;
}

void Engine::compute_coeffs(float center_freq, float q, float gain_db,
                             BiquadCoeffs* out, int num_stages) {
    // First stage: bandpass filter
    out[0] = compute_bandpass_coeffs(center_freq, q, sample_rate_);

    // Apply peaking gain
    // (This is a simplification; a full implementation would compose filters more carefully)
    out[0].b0 *= std::pow(10.0f, gain_db / 40.0f);
    out[0].b1 *= std::pow(10.0f, gain_db / 40.0f);
    out[0].b2 *= std::pow(10.0f, gain_db / 40.0f);

    // Second stage for 4-pole: another bandpass at same center frequency
    if (num_stages > 1) {
        out[1] = compute_bandpass_coeffs(center_freq, q, sample_rate_);
        out[1].b0 *= std::pow(10.0f, gain_db / 40.0f);
        out[1].b1 *= std::pow(10.0f, gain_db / 40.0f);
        out[1].b2 *= std::pow(10.0f, gain_db / 40.0f);
    }

    // Bass shelf (if any)
    if (profile_.filter.bass_shelf_db != 0.0f) {
        BiquadCoeffs shelf = compute_shelf_coeffs(profile_.filter.bass_shelf_freq,
                                                   profile_.filter.bass_shelf_db,
                                                   sample_rate_, false);
        // KNOWN ISSUE (ROADMAP, round 1 review): the shelf is computed and
        // discarded; only the gain hack below is applied. Rewritten in M7.
        (void)shelf;
        // Cascade the shelf (simplified approach)
        // In a full implementation, we'd properly cascade these
        out[0].b0 *= (1.0f + profile_.filter.bass_shelf_db / 40.0f);
    }
}

float Engine::position_to_freq(float position) const {
    float log_heel = std::log(profile_.filter.freq_heel);
    float log_toe = std::log(profile_.filter.freq_toe);
    float log_freq = log_heel + (log_toe - log_heel) * position;
    return std::exp(log_freq);
}

float Engine::compute_q(float position) const {
    return profile_.filter.q_min + (profile_.filter.q_max - profile_.filter.q_min) * position;
}

float Engine::compute_dynamic_scale(float rms_linear) const {
    // Map input level to one of three resonance stages
    float threshold_low = std::pow(10.0f, -20.0f / 20.0f);
    float threshold_high = std::pow(10.0f, 0.0f / 20.0f);

    if (rms_linear < threshold_low) {
        return 1.0f - profile_.dynamics.resonance_low;
    } else if (rms_linear < threshold_high) {
        float t = (rms_linear - threshold_low) / (threshold_high - threshold_low);
        float resonance = (1.0f - t) * profile_.dynamics.resonance_low +
                         t * profile_.dynamics.resonance_mid;
        return 1.0f - resonance;
    } else {
        return 1.0f - profile_.dynamics.resonance_high;
    }
}

void Engine::update_rms(const float* buffer, int num_samples) {
    float sum_sq = 0.0f;
    for (int i = 0; i < num_samples; i++) {
        sum_sq += buffer[i] * buffer[i];
    }
    float rms_current = std::sqrt(sum_sq / num_samples);
    rms_level_ = rms_smooth_coeff_ * rms_level_ + (1.0f - rms_smooth_coeff_) * rms_current;
}

void Engine::process(float* buffer, int num_samples) {
    update_rms(buffer, num_samples);
    float dynamic_scale = compute_dynamic_scale(rms_level_);

    int num_stages = (profile_.filter.topology == Topology::Bandpass4Pole) ? 2 : 1;

    for (int i = 0; i < num_samples; i++) {
        // Interpolate coefficients if needed
        if (interp_samples_remaining_ > 0) {
            float t = 1.0f - (float)interp_samples_remaining_ / interp_samples_total_;
            for (int stage = 0; stage < num_stages; stage++) {
                current_coeffs_[stage].b0 = (1.0f - t) * current_coeffs_[stage].b0 + t * target_coeffs_[stage].b0;
                current_coeffs_[stage].b1 = (1.0f - t) * current_coeffs_[stage].b1 + t * target_coeffs_[stage].b1;
                current_coeffs_[stage].b2 = (1.0f - t) * current_coeffs_[stage].b2 + t * target_coeffs_[stage].b2;
                current_coeffs_[stage].a1 = (1.0f - t) * current_coeffs_[stage].a1 + t * target_coeffs_[stage].a1;
                current_coeffs_[stage].a2 = (1.0f - t) * current_coeffs_[stage].a2 + t * target_coeffs_[stage].a2;
            }
            interp_samples_remaining_--;
        }

        // Process through cascade of biquads
        float sample = buffer[i] * std::pow(10.0f, profile_.levels.input_gain_db / 20.0f);
        for (int stage = 0; stage < num_stages; stage++) {
            const BiquadCoeffs& c = current_coeffs_[stage];
            BiquadState& s = state_[stage];

            float y = c.b0 * sample + s.z1;
            s.z1 = c.b1 * sample - c.a1 * y + s.z2;
            s.z2 = c.b2 * sample - c.a2 * y;

            sample = y * dynamic_scale;
        }

        // Output level and mix
        float wet = sample * std::pow(10.0f, profile_.levels.output_gain_db / 20.0f);
        buffer[i] = (1.0f - profile_.levels.mix) * buffer[i] + profile_.levels.mix * wet;
    }
}

float Engine::get_position() const {
    return position_;
}

float Engine::get_center_freq() const {
    return center_freq_;
}

float Engine::get_current_q() const {
    return profile_.filter.q_min + (profile_.filter.q_max - profile_.filter.q_min) * position_;
}

// ─────────────────────────────────────────────
// Profile Serialization (implemented in wah_toml.cpp)
// ─────────────────────────────────────────────

std::optional<Profile> load(const std::string& path) {
    // STUB (ROADMAP known issues): use load_toml in wah_toml.cpp.
    (void)path;
    return {};
}

bool save(const Profile& profile, const std::string& path) {
    // STUB (ROADMAP known issues): use save_toml in wah_toml.cpp.
    (void)profile; (void)path;
    return false;
}

std::string serialize(const Profile& profile) {
    // STUB (ROADMAP known issues): use the serializer in wah_toml.cpp.
    (void)profile;
    return "";
}

// Fitting is implemented in wah_fit.cpp

}  // namespace wah
