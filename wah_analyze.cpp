/**
 * wah_analyze.cpp
 * Frequency domain analysis implementation.
 */

#include "wah_analyze.h"
#include <cmath>
#include <algorithm>
#include <complex>

namespace wah {

// ─────────────────────────────────────────────
// FFT Implementation (Cooley-Tukey)
// ─────────────────────────────────────────────

namespace {

using Complex = std::complex<float>;

/**
 * In-place Cooley-Tukey FFT.
 * Size must be a power of 2.
 */
void fft_inplace(Complex* x, int N) {
    if (N <= 1) return;

    // Bit-reversal permutation
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }

    // Butterflies
    for (int len = 2; len <= N; len <<= 1) {
        float angle = 2.0f * M_PI / len;
        Complex wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < N; i += len) {
            Complex w(1);
            for (int j = 0; j < len / 2; j++) {
                Complex u = x[i + j];
                Complex v = x[i + j + len / 2] * w;
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

/**
 * Next power of 2 >= n.
 */
int next_pow2(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

/**
 * Find the index of the maximum magnitude in a vector.
 */
int find_peak_index(const std::vector<float>& magnitudes, int start_bin, int end_bin) {
    int peak_idx = start_bin;
    float peak_mag = magnitudes[start_bin];
    for (int i = start_bin + 1; i <= end_bin && i < (int)magnitudes.size(); i++) {
        if (magnitudes[i] > peak_mag) {
            peak_mag = magnitudes[i];
            peak_idx = i;
        }
    }
    return peak_idx;
}

/**
 * Interpolate peak frequency using parabolic interpolation for sub-bin accuracy.
 * Assumes peak is at magnitude[peak_idx], with neighbors at peak_idx-1 and peak_idx+1.
 */
float interpolate_peak_frequency(const std::vector<float>& magnitudes, int peak_idx,
                                  float freq_per_bin) {
    if (peak_idx <= 0 || peak_idx >= (int)magnitudes.size() - 1) {
        return peak_idx * freq_per_bin;
    }

    float left = magnitudes[peak_idx - 1];
    float center = magnitudes[peak_idx];
    float right = magnitudes[peak_idx + 1];

    // Parabolic interpolation: delta = 0.5 * (left - right) / (2*center - left - right)
    float denom = 2.0f * center - left - right;
    if (std::abs(denom) < 1e-10f) {
        return peak_idx * freq_per_bin;
    }
    float delta = 0.5f * (left - right) / denom;

    return (peak_idx + delta) * freq_per_bin;
}

/**
 * Find -3dB bandwidth around a peak.
 * Returns the width in bins (can be fractional due to interpolation).
 */
float measure_3db_bandwidth(const std::vector<float>& magnitudes, int peak_idx) {
    if (peak_idx <= 0 || peak_idx >= (int)magnitudes.size() - 1) {
        return 1.0f;  // degenerate case
    }

    float peak_mag = magnitudes[peak_idx];
    float threshold = peak_mag - 3.0f;  // -3 dB is 0.707x in linear (but we work in dB)

    // Find left -3dB point
    int left_idx = peak_idx;
    for (int i = peak_idx - 1; i >= 0; i--) {
        if (magnitudes[i] < threshold) {
            left_idx = i;
            break;
        }
        left_idx = i;
    }

    // Find right -3dB point
    int right_idx = peak_idx;
    for (int i = peak_idx + 1; i < (int)magnitudes.size(); i++) {
        if (magnitudes[i] < threshold) {
            right_idx = i;
            break;
        }
        right_idx = i;
    }

    return (float)(right_idx - left_idx);
}

/**
 * Estimate noise floor as the median magnitude in dB.
 * Lower magnitudes are more likely to be noise.
 */
float estimate_noise_floor(const std::vector<float>& magnitudes, int exclude_start, int exclude_end) {
    std::vector<float> noise_region;
    for (int i = 0; i < (int)magnitudes.size(); i++) {
        if (i < exclude_start || i > exclude_end) {
            noise_region.push_back(magnitudes[i]);
        }
    }
    if (noise_region.empty()) return -100.0f;  // default if no noise region

    std::sort(noise_region.begin(), noise_region.end());
    return noise_region[noise_region.size() / 2];  // median
}

}  // namespace

// ─────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────

void apply_hann_window(float* buffer, int num_samples) {
    for (int i = 0; i < num_samples; i++) {
        float w = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (num_samples - 1)));
        buffer[i] *= w;
    }
}

std::vector<float> compute_magnitude_spectrum(const float* buffer, int num_samples,
                                               float /*sample_rate*/) {
    // Pad to next power of 2 for efficient FFT
    int fft_size = next_pow2(num_samples);
    std::vector<Complex> x(fft_size, Complex(0));

    // Copy and window input
    for (int i = 0; i < num_samples; i++) {
        float w = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (num_samples - 1)));
        x[i] = Complex(buffer[i] * w, 0);
    }

    // Perform FFT
    fft_inplace(x.data(), fft_size);

    // Convert to magnitude spectrum (dB)
    std::vector<float> magnitude(fft_size / 2);
    for (int i = 0; i < fft_size / 2; i++) {
        float mag = std::abs(x[i]);
        // Normalize by FFT size and apply scaling for display
        mag = 2.0f * mag / fft_size;
        // Convert to dB (20 * log10), with floor at -120 dB
        magnitude[i] = (mag > 1e-6f) ? 20.0f * std::log10(mag) : -120.0f;
    }

    return magnitude;
}

FrequencyResponse analyze(const float* buffer, int num_samples, float sample_rate,
                          float freq_min, float freq_max) {
    FrequencyResponse result{0, 0, 0, false};

    // Sanity checks
    if (!buffer || num_samples < 256) {
        return result;  // buffer too small
    }

    // Compute magnitude spectrum
    std::vector<float> magnitudes = compute_magnitude_spectrum(buffer, num_samples, sample_rate);

    // Convert frequency range to bin indices
    float freq_per_bin = sample_rate / (2.0f * magnitudes.size());
    int start_bin = (int)(freq_min / freq_per_bin);
    int end_bin = (int)(freq_max / freq_per_bin);

    start_bin = std::max(1, start_bin);  // avoid DC
    end_bin = std::min((int)magnitudes.size() - 1, end_bin);

    if (start_bin >= end_bin) {
        return result;  // invalid frequency range
    }

    // Find peak
    int peak_idx = find_peak_index(magnitudes, start_bin, end_bin);
    float peak_mag_db = magnitudes[peak_idx];

    // Interpolate peak frequency for sub-bin accuracy
    float center_freq = interpolate_peak_frequency(magnitudes, peak_idx, freq_per_bin);

    // Estimate noise floor
    int exclude_start = std::max(0, peak_idx - 10);
    int exclude_end = std::min((int)magnitudes.size() - 1, peak_idx + 10);
    float noise_floor = estimate_noise_floor(magnitudes, exclude_start, exclude_end);

    // Compute gain relative to noise floor
    float gain_db = peak_mag_db - noise_floor;

    // Measure Q from -3dB bandwidth
    float bandwidth_bins = measure_3db_bandwidth(magnitudes, peak_idx);
    float bandwidth_hz = bandwidth_bins * freq_per_bin;
    float q = (bandwidth_hz > 0.1f) ? center_freq / bandwidth_hz : 1.0f;

    // Sanity check: valid signal if peak is significantly above noise
    bool valid = (gain_db > 6.0f) && (center_freq >= freq_min) && (center_freq <= freq_max);

    result.center_freq = center_freq;
    result.q = q;
    result.peak_gain_db = gain_db;
    result.valid = valid;

    return result;
}

}  // namespace wah
