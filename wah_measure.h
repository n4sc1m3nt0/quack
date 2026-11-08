/**
 * wah_measure.h
 * Transfer function of a device under test, from a known stimulus and the
 * measured response (ROADMAP, M3). Replaces wah::analyze(), which reads the
 * spectrum of one buffer and cannot tell the pedal from the stimulus.
 *
 * Method: Welch averaging of Hann-windowed segments with 50 % overlap,
 *   H1(f) = Sxy / Sxx        (gain and phase from stimulus x to response y)
 *   coherence(f) = |Sxy|^2 / (Sxx * Syy)   (1 = fully explained by the stimulus)
 */

#pragma once

#include <vector>

namespace wah {

struct TransferFunction {
    std::vector<float> freq_hz;       // bin centre frequencies, DC to Nyquist
    std::vector<float> magnitude_db;  // 20*log10 |H(f)|, response relative to stimulus
    std::vector<float> coherence;     // 0..1, per bin
    int   num_segments = 0;           // how many segments were averaged
    bool  valid = false;              // false on bad input (null, too short, bad size)
};

/**
 * @param stimulus      The signal sent to the device (num_samples floats)
 * @param response      The signal measured at its output, same length and aligned
 *                      in time with the stimulus (latency removed by the caller)
 * @param num_samples   Length of both buffers
 * @param sample_rate   Hz
 * @param segment_size  FFT segment length, a power of two, >= 64 (default 4096)
 *
 * valid is false if a buffer is null, the buffers hold fewer than 4 segments,
 * or segment_size is not a power of two >= 64.
 */
TransferFunction measure_transfer_function(const float* stimulus, const float* response,
                                           int num_samples, float sample_rate,
                                           int segment_size = 4096);

/** Result of fitting a 2-pole band-pass to a measured transfer function. */
struct BandpassEstimate {
    float center_freq = 0;   // Hz
    float q = 0;             // quality factor
    float peak_gain_db = 0;  // gain at the centre, relative to the stimulus
    float fit_rmse_db = 0;   // weighted RMS distance between model and measurement
    bool  valid = false;     // false if the measurement is unusable (see below)
};

/**
 * Fit |H(f)| = g / sqrt(1 + Q^2 (f/f0 - f0/f)^2) to the measured magnitude, in dB,
 * using only the bins between freq_min and freq_max whose coherence is at least
 * min_coherence, each weighted by coherence / f (equal weight per octave).
 *
 * valid is false if tf is not valid, fewer than 20 bins qualify, or the best fit
 * has an RMS error above 3 dB (the response is not a band-pass).
 */
BandpassEstimate estimate_bandpass(const TransferFunction& tf,
                                   float freq_min = 20.0f, float freq_max = 5000.0f,
                                   float min_coherence = 0.5f);

} // namespace wah
