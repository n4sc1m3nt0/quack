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

#include <cstdint>
#include <limits>
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

/** The shapes fit_shape() can choose between (D7). */
enum class ShapeModel {
    None,            // nothing fits (see fit_shape)
    BandPass2,       // 2-pole band-pass
    BandPass2Shelf,  // the same band-pass times a first-order low shelf
    BandPass4,       // the product of two 2-pole band-passes (round 18)
    Measured,        // no model fits within 3 dB: the measurement itself, as a table (round 19)
};

/** Points of the table of a Measured shape (D58). */
constexpr int kShapeTablePoints = 64;

/** Result of fit_shape(). The shelf fields are 0 unless model is BandPass2Shelf, the second-centre fields 0 unless BandPass4. */
struct ShapeFit {
    ShapeModel model = ShapeModel::None;
    float center_freq = 0;     // Hz, of the band-pass
    float q = 0;               // quality factor of the band-pass
    float peak_gain_db = 0;    // band-pass gain at its centre, without the shelf; for BandPass4 the SUM of the two
                               // peak-gain parameters (only their sum can be measured, D56)
    float center2_freq = 0;    // BandPass4: Hz, centre of the second (higher) band-pass; center_freq is the lower
    float q2 = 0;              // BandPass4: Q of the second band-pass
    float shelf_freq_hz = 0;   // corner frequency of the low shelf
    float shelf_gain_db = 0;   // gain of the shelf below its corner (dB; negative cuts the lows)
    float fit_rmse_db = 0;     // weighted RMS distance between the chosen model and the measurement (0 for Measured)
    float best_model_rmse_db = 0;  // the lowest residual of the models that were fitted (the 2-pole only if within 3 dB);
                               // for a model result it equals fit_rmse_db; for Measured it says how far the models were
    std::vector<float> table_freq_hz;  // Measured: kShapeTablePoints centres, evenly spaced in log frequency
    std::vector<float> table_db;       // Measured: the magnitude in dB at each (empty for the models)
    bool  valid = false;
};

/**
 * Fit the shape of a calibrated transfer function (M4, D7) with two models, in dB, on the bins
 * between freq_min and freq_max whose coherence is at least min_coherence, each weighted by
 * coherence / f as in estimate_bandpass():
 *   2-pole:           g / sqrt(1 + Q^2 (f/f0 - f0/f)^2)
 *   2-pole and shelf: the same times sqrt((G^2 + (f/fs)^2) / (1 + (f/fs)^2)), G the shelf gain
 *                     (linear) below its corner fs and 1 above it. The corner is at or below the
 *                     centre, the shelf gain within +-20 dB and Q is at least 1, so that a wide band-pass
 *                     cancelled by a shelf cannot imitate a flat response.
 *   4-pole:           the product of two 2-pole band-passes, the first centre the lower, the two at
 *                     between 10 % and 2 octaves apart, each Q at least 1; one gain (the sum of the two).
 * The choice (D7): the simplest model (2-pole, shelf, 4-pole) whose residual is at or below 1.5 dB;
 * if none is, the one with the lowest residual if that is at or below 3 dB; otherwise nothing.
 *
 * valid is false (model None) if tf is not valid, fewer than 20 bins qualify, or all
 * residuals are above 3 dB (a flat or random response, or a shape for a later round).
 */
ShapeFit fit_shape(const TransferFunction& tf,
                   float freq_min = 20.0f, float freq_max = 5000.0f,
                   float min_coherence = 0.5f);

/** Result of looking for the latency between a stimulus and a response. */
struct DelayEstimate {
    int   samples = 0;   // how many samples the response lags the stimulus, 0..max_delay
    float peak = 0;      // |normalised correlation| at that lag, 0..1 (1 = identical shape)
    bool  valid = false; // false on bad input or when no lag stands out (see below)
};

/**
 * Latency of a measurement chain, in whole samples, by cross-correlating the stimulus
 * with the response over the lags 0..max_delay (FFT, so the cost is a few ms for seconds
 * of audio). A polarity-inverting chain is found too (the largest |correlation| wins).
 *
 * Use it on the LOOPBACK path (interface and DI box, no pedal). Do not use it on a
 * pedal's own output: a band-pass rings, its correlation peaks once per cycle and the
 * strongest peak need not be the first. The loopback latency is then applied to the
 * pedal measurement too (M3, loopback calibration).
 *
 * Whole samples only: a fraction of a sample is a phase error and leaves the magnitude
 * that measure_transfer_function() returns unchanged.
 *
 * valid is false if a buffer is null, max_delay < 0, num_samples < 1024 or less than
 * 4 * max_delay, either signal is silent, or the best |correlation| is below 0.2.
 */
DelayEstimate estimate_delay(const float* stimulus, const float* response,
                             int num_samples, int max_delay);

/**
 * Transfer function of the pedal with the measurement chain divided out (M3, D24, D27).
 *
 * The stimulus is played twice: through the chain alone (`loopback`, interface and DI box,
 * a cable in place of the pedal) and through the chain and the pedal (`dut`). The latency
 * is estimated on the loopback (max_delay as in estimate_delay) and trims both recordings.
 * Then, bin by bin, magnitude_db = H_dut - H_loopback (dB) and coherence = the lower of
 * the two coherences, so a bin the loopback measured badly cannot look good. num_segments
 * is the lower of the two as well.
 *
 * valid is false if estimate_delay() refuses the loopback, or if either measurement is
 * refused by measure_transfer_function() (null, too short once trimmed, bad segment size).
 */
TransferFunction measure_calibrated(const float* stimulus, const float* dut, const float* loopback,
                                    int num_samples, float sample_rate, int max_delay,
                                    int segment_size = 4096);

/**
 * The stimulus of a measurement: uniform white noise from a seeded xorshift32, mean removed,
 * scaled to the exact RMS (D28, D29). The same arguments give the same array on every run, so
 * the capture plays the very same stimulus through the loopback and through the pedal.
 * Uniform noise has a bounded peak (about 1.73 times the RMS), so it cannot clip.
 *
 * @param num_samples  Length (one pass of the capture: 2^17 = 131072, 2.7 s at 48 kHz)
 * @param seed         Non-zero; different seeds give unrelated noise (default 1)
 * @param rms          Linear RMS level, > 0 and <= 0.5 (default 0.1 = -20 dBFS)
 *
 * Returns an empty vector if num_samples < 1, seed is 0, or rms is outside (0, 0.5].
 */
std::vector<float> generate_stimulus(int num_samples, std::uint32_t seed = 1, float rms = 0.1f);

/** The band-pass estimate of one analysis window of a moving pedal. */
struct TrackPoint {
    float time_s = 0;           // centre of the window, in seconds from the start of the stimulus
    BandpassEstimate estimate;  // valid = false when this window is not a usable band-pass
};

/**
 * Follow the band-pass while the pedal moves (M3, D32). The latency is estimated on the
 * loopback over the whole recording, both recordings are trimmed by it, the loopback transfer
 * function is measured once over the whole recording (the chain does not move), and then
 * every window of window_s seconds, advancing by half a window, gets the pedal's transfer
 * function, divided by the loopback's (coherence = the lower of the two), and an
 * estimate_bandpass() with the given range and coherence floor.
 *
 * Measured with a full heel-to-toe in 1 s, window 0.25 s, segment 2048: the centre is within
 * 5 % except in the windows that contain a turnaround of the movement. Q and gain per moving
 * window are only indicative (up to about 40 % and 3.5 dB off): a window short enough to
 * follow the foot has too little frequency resolution. Take Q and gain from a static hold
 * with a longer window (0.5 s, segment 4096: Q within 10 %, gain within 1 dB).
 *
 * Returns an empty vector if a buffer is null, window_s <= 0, segment_size is not a power of
 * two >= 64, the loopback latency is not found, or a window is shorter than 4 segments.
 */
std::vector<TrackPoint> track_bandpass(const float* stimulus, const float* dut, const float* loopback,
                                       int num_samples, float sample_rate, int max_delay,
                                       float window_s = 0.25f, int segment_size = 2048,
                                       float freq_min = 20.0f, float freq_max = 5000.0f,
                                       float min_coherence = 0.5f);

/** The shape fit of one analysis window of a moving pedal (round 28). */
struct ShapeTrackPoint {
    float time_s = 0;  // centre of the window, in seconds from the start of the stimulus
    ShapeFit shape;    // valid = false when this window has no usable measurement
};

/**
 * Follow the shape while the pedal moves (M5, D65): the same windows as track_bandpass() (latency from the loopback,
 * the loopback measured once, every window divided by it), but each is fitted by fit_shape() instead of
 * estimate_bandpass(), so a window of a pedal with a low shelf, a 4-pole or a shape no model fits (Measured) is read
 * as such and not refused. The cost is that of fit_shape() (three models) per window. Arguments and refusals as for
 * track_bandpass(): an empty vector if a buffer is null, window_s <= 0, segment_size is not a power of two >= 64,
 * the loopback latency is not found, or a window is shorter than 4 segments.
 */
std::vector<ShapeTrackPoint> track_shape(const float* stimulus, const float* dut, const float* loopback,
                                         int num_samples, float sample_rate, int max_delay,
                                         float window_s = 0.25f, int segment_size = 2048,
                                         float freq_min = 20.0f, float freq_max = 5000.0f,
                                         float min_coherence = 0.5f);

/** One analysis window of one pass: where the guide was, and what the tracker measured. */
struct PassPoint {
    float position = 0;     // normalised guide position, 0 heel to 1 toe (D33)
    float center_freq = 0;  // Hz, from a valid BandpassEstimate
    float weight = 0;       // > 0, for example the mean coherence of the window
};

/** Several passes folded into one centre-versus-position curve. */
struct AggregatedCurve {
    std::vector<float> position;     // num_points values: 0, 1/(n-1) ... 1
    std::vector<float> center_freq;  // Hz per position; 0 where no pass has data
    std::vector<int>   support;      // how many kept passes had data at each position
    std::vector<float> std_error;    // relative standard error of the median centre per position (D35); infinity if fewer than 3 kept passes had data (D38)
    std::vector<int>   dropped;      // indices (in the input) of the passes dropped
    bool valid = false;              // false on bad input or when nothing is left
};

/**
 * Robust aggregation of the passes of a capture (M4, D17, D33, D34).
 *
 * Per pass: points with an invalid position (outside 0..1), centre or weight (<= 0, NaN) are
 * ignored; points at the same position are merged; the centre is interpolated linearly in log
 * frequency at each of the num_points positions, but not across a gap wider than 0.25 and not
 * beyond the pass's first and last point. A reference curve is the weighted median over all
 * passes. A pass is dropped when its centre differs from the reference by more than 15 % at
 * more than a quarter of the positions (missing positions do not count), or when it has no
 * usable point; with fewer than 3 passes nothing is dropped for disagreement. The result is
 * the coherence-weighted median over the kept passes.
 *
 * valid is false if passes is empty, num_points < 2, or no kept pass has any data.
 */
AggregatedCurve aggregate_passes(const std::vector<std::vector<PassPoint>>& passes, int num_points = 11);

/** How sure a capture is, as the relative standard error of the median centre (D36). */
enum class CaptureTarget { Fast, Normal, Maximum };  // 6 %, 3 %, 1.5 %

enum class CaptureState {
    NeedMorePasses,         // fewer than 4 kept passes (D37)
    ConfidenceNotReached,   // 4 or more kept passes, error still above the target
    Done,                   // 4 or more kept passes and error at or below the target
    MaximumReached          // 20 passes recorded without reaching the target
};

/** The quantity that keeps a capture from being sure (D49, D51). */
enum class CaptureLimit { Curve, HeelQ, HeelGain, ToeQ, ToeGain };

struct CaptureProgress {
    CaptureState state = CaptureState::NeedMorePasses;
    bool  stop = false;     // true for Done and MaximumReached
    int   passes_recorded = 0, passes_kept = 0, passes_dropped = 0;
    float confidence = 0;   // worst std_error over the positions; infinity when not assessable
    float target = 0;       // the error the chosen target asks for
    float gain_target_db = 0;   // the gain error the target asks for, in dB (D46)
    float heel_q_error = std::numeric_limits<float>::infinity();   // from aggregate_holds; infinity when not assessable or not looked at
    float heel_gain_error_db = std::numeric_limits<float>::infinity();
    float toe_q_error = std::numeric_limits<float>::infinity();
    float toe_gain_error_db = std::numeric_limits<float>::infinity();
    float worst_ratio = 0;      // largest error over its target among the quantities looked at; at most 1 when all are sure
    CaptureLimit limiting = CaptureLimit::Curve;  // the quantity with that largest ratio
    bool  valid = false;    // false if passes is empty or num_points < 2
};

/**
 * Where a capture stands after the passes recorded so far (M4, D16, D35 to D38): aggregates
 * them (aggregate_passes), takes the worst std_error as the confidence, and says whether to
 * stop. Done if at least 4 passes are kept and the confidence is at or below the target;
 * otherwise MaximumReached once 20 passes are recorded (dropped ones count);
 * otherwise NeedMorePasses below 4 kept passes, else ConfidenceNotReached.
 */
CaptureProgress capture_progress(const std::vector<std::vector<PassPoint>>& passes, CaptureTarget target,
                                 int num_points = 11);

/** Q, gain and centre of a pedal held still. */
struct HoldEstimate {
    float center_freq = 0;   // Hz
    float q = 0;
    float peak_gain_db = 0;
    int   windows_used = 0;  // valid windows the result is the median of
    bool  valid = false;
};

/**
 * Measure a pedal held still (M4, D32, D39 to D43): the caller gives the hold as a time
 * interval of the recording, in seconds from the start of the stimulus (from the guide).
 * The loopback latency and transfer function are found as in track_bandpass(). The windows of
 * window_s seconds, advancing by half a window, start settle_s seconds after the start of the
 * hold and end before its end; each is calibrated and fitted by estimate_bandpass(). The
 * result is the median over the valid windows of centre, Q and gain, each window weighted by
 * its mean coherence over the fitted range.
 *
 * Measured, a static pedal over a hold of 1 s or more: centre within 2 %, Q within 10 %, gain
 * within 1 dB (D32).
 *
 * valid is false if a buffer is null, the loopback latency is not found, the interval is
 * empty, starts before 0 or ends after the recording (trimmed by the latency), window_s <= 0,
 * settle_s < 0, segment_size is not a power of two >= 64, the window is shorter than 4
 * segments, fewer than 2 windows are valid, or the centres of the valid windows differ by more
 * than 5 % (largest over smallest: the pedal was still moving).
 */
HoldEstimate estimate_hold(const float* stimulus, const float* dut, const float* loopback,
                           int num_samples, float sample_rate, int max_delay,
                           float hold_start_s, float hold_end_s,
                           float window_s = 0.5f, int segment_size = 4096, float settle_s = 0.2f,
                           float freq_min = 20.0f, float freq_max = 5000.0f, float min_coherence = 0.5f);

/** One end of the sweep (heel or toe), from the holds of all the passes. */
struct HoldSummary {
    float center_freq = 0, q = 0, peak_gain_db = 0;                     // weighted medians over the kept holds
    float center_error = std::numeric_limits<float>::infinity();        // standard error of each median (D45); infinity below 3 kept holds
    float q_error = std::numeric_limits<float>::infinity();
    float gain_error_db = std::numeric_limits<float>::infinity();
    int   kept = 0;                                                     // holds the medians come from
    std::vector<int> dropped;                                           // indices in the input of the holds left out
    bool  valid = false;                                                // false if no usable hold
};

struct HoldsAggregate { HoldSummary heel, toe; };

/**
 * Robust aggregation of the holds of all passes, per end (M4, D17, D44 to D47). The weight of
 * a hold is its windows_used. A hold that is not valid, or whose centre, Q or window count is
 * not positive (or not finite), is ignored and listed in dropped. The reference is the
 * weighted median of each quantity over the usable holds; a hold is dropped when its centre
 * is more than 15 % (in log frequency), its Q more than 25 % (in log Q) or its gain more than
 * 2 dB from the reference; with fewer than 3 usable holds nothing is dropped for
 * disagreement. The result is the weighted median over the kept holds, and its error is
 * 1.2533 * 1.4826 * the weighted median absolute deviation / sqrt(kept): relative (log) for
 * centre and Q, in dB for the gain, infinite below 3 kept holds.
 */
HoldsAggregate aggregate_holds(const std::vector<HoldEstimate>& heel_holds, const std::vector<HoldEstimate>& toe_holds);

/**
 * The same, with the holds of all the passes (D48 to D51): the heel and toe holds are
 * aggregated (aggregate_holds) and their Q errors (against the target of the centre) and gain
 * errors (against 0.5, 0.25 or 0.1 dB) join the curve error. Done needs all of them at or
 * below their targets as well as 4 kept passes; the holds add no minimum of their own, their
 * error is infinite below 3 kept holds. `limiting` and `worst_ratio` say which quantity is
 * furthest from its target.
 */
CaptureProgress capture_progress(const std::vector<std::vector<PassPoint>>& passes,
                                 const std::vector<HoldEstimate>& heel_holds, const std::vector<HoldEstimate>& toe_holds,
                                 CaptureTarget target, int num_points = 11);

/** The shape of a pedal held still (M5, D39 to D43, round 20). */
struct HoldShape {
    ShapeFit shape;              // fit_shape() of the calibrated measurement of the hold
    int   windows_checked = 0;   // windows whose own fit had a centre (a model, not Measured) for the movement check
    bool  valid = false;
};

/**
 * Fit the shape of a pedal held still (M5, D30, D39 to D43): the caller gives the hold as a time
 * interval of the recording, in seconds from the start of the stimulus. The loopback latency and
 * transfer function are found as in estimate_hold(), and the windows (window_s, advancing by half
 * a window, starting settle_s after the start of the hold and ending before its end) are the same.
 * The result is fit_shape() of the calibrated transfer function of the whole span from the first
 * window to the end of the hold. No I/O.
 *
 * The pedal must not be moving: each window is also fitted by fit_shape(); the centres of the
 * windows that gave a model (not Measured) must not differ by more than 5 % (largest over
 * smallest), and at least 2 windows must give one, because with fewer the movement cannot be
 * assessed (a Measured shape has no centre; proposed D61).
 *
 * valid is false if a buffer is null, the loopback latency is not found, the interval is empty,
 * starts before 0 or ends after the recording (trimmed by the latency), window_s <= 0,
 * settle_s < 0, segment_size is not a power of two >= 64, the window is shorter than 4 segments,
 * fewer than 2 windows fit in the hold, fit_shape() refuses the span, or the movement check fails.
 */
HoldShape fit_hold_shape(const float* stimulus, const float* dut, const float* loopback,
                         int num_samples, float sample_rate, int max_delay,
                         float hold_start_s, float hold_end_s,
                         float window_s = 0.5f, int segment_size = 4096, float settle_s = 0.2f,
                         float freq_min = 20.0f, float freq_max = 5000.0f, float min_coherence = 0.5f);

} // namespace wah
