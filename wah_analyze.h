/**
 * wah_analyze.h
 * Frequency domain analysis for capture tool.
 *
 * Extracts center frequency, Q, and peak gain from a measured audio buffer
 * captured at a fixed pedal position.
 */

#pragma once

#include <vector>

namespace wah {

struct FrequencyResponse {
    float center_freq;    // Hz
    float q;              // quality factor
    float peak_gain_db;   // dB
    bool  valid;          // false if analysis failed (e.g., insufficient signal)
};

/**
 * Analyze a buffer of audio to extract frequency response characteristics.
 *
 * @param buffer        Audio samples (float, normalized -1.0 to 1.0)
 * @param num_samples   Buffer length
 * @param sample_rate   Sample rate (Hz)
 * @param freq_min      Minimum frequency to search (Hz, default 20)
 * @param freq_max      Maximum frequency to search (Hz, default 5000)
 *
 * @return FrequencyResponse with center_freq, q, peak_gain_db, and valid flag
 *
 * Algorithm:
 * 1. Apply Hann window to reduce spectral leakage
 * 2. Compute FFT
 * 3. Find peak magnitude in the target frequency range
 * 4. Measure -3dB bandwidth to compute Q
 * 5. Measure gain relative to noise floor
 */
FrequencyResponse analyze(const float* buffer, int num_samples, float sample_rate,
                          float freq_min = 20.0f, float freq_max = 5000.0f);

/**
 * Hann window for FFT pre-processing.
 * Reduces spectral leakage for more accurate peak detection.
 */
void apply_hann_window(float* buffer, int num_samples);

/**
 * Simple magnitude spectrum from FFT.
 * Returns magnitude in dB relative to full scale.
 * Returned vector has size num_samples/2 (DC to Nyquist).
 * freq_bin_hz = (bin_index / vector.size()) * (sample_rate / 2)
 */
std::vector<float> compute_magnitude_spectrum(const float* buffer, int num_samples,
                                               float sample_rate);

} // namespace wah
