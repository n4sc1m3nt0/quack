/**
 * wah_measure.cpp
 * Transfer function and coherence by Welch averaging (see wah_measure.h).
 *
 * The FFT here works in double precision with exact twiddle factors; the float
 * FFT in wah_analyze.cpp goes away together with wah::analyze().
 */

#include "wah_measure.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <complex>

namespace wah {

namespace {

using Cd = std::complex<double>;
const double kPi = 3.14159265358979323846;

bool is_pow2(int n) { return n > 0 && (n & (n - 1)) == 0; }

// In-place radix-2 FFT, n a power of two, twiddles precomputed (size n/2).
void fft(std::vector<Cd>& x, const std::vector<Cd>& tw) {
    const int n = (int)x.size();
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const int step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int k = 0; k < len / 2; k++) {
                Cd u = x[i + k];
                Cd v = x[i + k + len / 2] * tw[k * step];
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
            }
        }
    }
}

}  // namespace

TransferFunction measure_transfer_function(const float* stimulus, const float* response,
                                           int num_samples, float sample_rate,
                                           int segment_size) {
    TransferFunction out;
    if (!stimulus || !response || sample_rate <= 0.0f) return out;
    if (segment_size < 64 || !is_pow2(segment_size)) return out;

    const int S = segment_size;
    const int hop = S / 2;
    if (num_samples < S) return out;
    const int K = (num_samples - S) / hop + 1;
    if (K < 4) return out;

    std::vector<Cd> tw(S / 2);
    for (int k = 0; k < S / 2; k++) tw[k] = std::polar(1.0, -2.0 * kPi * k / S);
    std::vector<double> win(S);
    for (int i = 0; i < S; i++) win[i] = 0.5 * (1.0 - std::cos(2.0 * kPi * i / S));  // periodic Hann

    const int bins = S / 2 + 1;
    std::vector<double> sxx(bins, 0.0), syy(bins, 0.0);
    std::vector<Cd> sxy(bins, Cd(0, 0));
    std::vector<Cd> X(S), Y(S);

    for (int k = 0; k < K; k++) {
        const int off = k * hop;
        for (int i = 0; i < S; i++) {
            X[i] = Cd(stimulus[off + i] * win[i], 0);
            Y[i] = Cd(response[off + i] * win[i], 0);
        }
        fft(X, tw);
        fft(Y, tw);
        for (int b = 0; b < bins; b++) {
            sxx[b] += std::norm(X[b]);
            syy[b] += std::norm(Y[b]);
            sxy[b] += std::conj(X[b]) * Y[b];
        }
    }

    out.freq_hz.resize(bins);
    out.magnitude_db.resize(bins);
    out.coherence.resize(bins);
    const double tiny = 1e-30;
    for (int b = 0; b < bins; b++) {
        out.freq_hz[b] = (float)((double)b * sample_rate / S);
        if (sxx[b] < tiny || syy[b] < tiny) {
            out.magnitude_db[b] = -200.0f;
            out.coherence[b] = 0.0f;
            continue;
        }
        const double mag = std::abs(sxy[b]) / sxx[b];
        double coh = std::norm(sxy[b]) / (sxx[b] * syy[b]);
        if (coh > 1.0) coh = 1.0;
        out.magnitude_db[b] = mag > 1e-10 ? (float)(20.0 * std::log10(mag)) : -200.0f;
        out.coherence[b] = (float)coh;
    }
    out.num_segments = K;
    out.valid = true;
    return out;
}

namespace {

// Model: magnitude in dB of a 2-pole band-pass with peak gain g_db at f0.
double model_db(double f, double f0, double q, double g_db) {
    const double r = f / f0;
    const double d = q * (r - 1.0 / r);
    return g_db - 10.0 * std::log10(1.0 + d * d);
}

struct FitData {
    std::vector<double> f, db, w;
    double fmin, fmax;
};

// Weighted RMS error in dB for p = (ln f0, ln Q, gain dB); large if out of bounds.
double cost(const FitData& d, const double* p) {
    const double f0 = std::exp(p[0]), q = std::exp(p[1]);
    if (f0 < d.fmin || f0 > d.fmax || q < 0.3 || q > 100.0) return 1e6;
    double se = 0, sw = 0;
    for (size_t i = 0; i < d.f.size(); i++) {
        const double e = d.db[i] - model_db(d.f[i], f0, q, p[2]);
        se += d.w[i] * e * e;
        sw += d.w[i];
    }
    return std::sqrt(se / sw);
}

// Nelder-Mead on 3 parameters. Returns the best cost; p holds the best point.
double nelder_mead(const FitData& d, double* p, const double* step) {
    double s[4][3];
    double c[4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) s[i][j] = p[j] + (i == j + 1 ? step[j] : 0.0);
        c[i] = cost(d, s[i]);
    }
    for (int it = 0; it < 600; it++) {
        int lo = 0, hi = 0;
        for (int i = 1; i < 4; i++) { if (c[i] < c[lo]) lo = i; if (c[i] > c[hi]) hi = i; }
        int nx = lo;  // second worst
        for (int i = 0; i < 4; i++) if (i != hi && (nx == lo || c[i] > c[nx]) ) nx = i;
        if (c[hi] - c[lo] < 1e-7) break;
        double m[3] = {0, 0, 0};
        for (int i = 0; i < 4; i++) if (i != hi) for (int j = 0; j < 3; j++) m[j] += s[i][j] / 3.0;
        auto at = [&](double t, double* out) { for (int j = 0; j < 3; j++) out[j] = m[j] + t * (s[hi][j] - m[j]); };
        double xr[3], xe[3], xc[3];
        at(-1.0, xr); double cr = cost(d, xr);
        if (cr < c[lo]) {
            at(-2.0, xe); double ce = cost(d, xe);
            for (int j = 0; j < 3; j++) s[hi][j] = (ce < cr) ? xe[j] : xr[j];
            c[hi] = (ce < cr) ? ce : cr;
        } else if (cr < c[nx]) {
            for (int j = 0; j < 3; j++) s[hi][j] = xr[j];
            c[hi] = cr;
        } else {
            at(cr < c[hi] ? -0.5 : 0.5, xc); double cc = cost(d, xc);
            if (cc < std::min(cr, c[hi])) {
                for (int j = 0; j < 3; j++) s[hi][j] = xc[j];
                c[hi] = cc;
            } else {
                for (int i = 0; i < 4; i++) {
                    if (i == lo) continue;
                    for (int j = 0; j < 3; j++) s[i][j] = s[lo][j] + 0.5 * (s[i][j] - s[lo][j]);
                    c[i] = cost(d, s[i]);
                }
            }
        }
    }
    int lo = 0;
    for (int i = 1; i < 4; i++) if (c[i] < c[lo]) lo = i;
    for (int j = 0; j < 3; j++) p[j] = s[lo][j];
    return c[lo];
}

}  // namespace

BandpassEstimate estimate_bandpass(const TransferFunction& tf, float freq_min, float freq_max,
                                   float min_coherence) {
    BandpassEstimate out;
    if (!tf.valid || tf.freq_hz.size() != tf.magnitude_db.size() ||
        tf.freq_hz.size() != tf.coherence.size() || freq_min <= 0 || freq_max <= freq_min)
        return out;

    FitData d;
    d.fmin = freq_min;
    d.fmax = freq_max;
    for (size_t i = 0; i < tf.freq_hz.size(); i++) {
        const double f = tf.freq_hz[i];
        if (f < freq_min || f > freq_max || tf.coherence[i] < min_coherence) continue;
        d.f.push_back(f);
        d.db.push_back(tf.magnitude_db[i]);
        d.w.push_back(tf.coherence[i] / f);
    }
    if (d.f.size() < 20) return out;

    // Start: highest point of a 5-bin average, Q from its -3 dB width, plus a few fixed Qs.
    const int n = (int)d.f.size();
    std::vector<double> sm(n);
    for (int i = 0; i < n; i++) {
        double s = 0; int c = 0;
        for (int k = std::max(0, i - 2); k <= std::min(n - 1, i + 2); k++) { s += d.db[k]; c++; }
        sm[i] = s / c;
    }
    int pk = 0;
    for (int i = 1; i < n; i++) if (sm[i] > sm[pk]) pk = i;
    int lo = pk, hi = pk;
    while (lo > 0 && sm[lo] > sm[pk] - 3.0) lo--;
    while (hi < n - 1 && sm[hi] > sm[pk] - 3.0) hi++;
    const double bw = d.f[hi] - d.f[lo];
    const double q_guess = bw > 0 ? std::min(50.0, std::max(0.5, d.f[pk] / bw)) : 4.0;

    double best_cost = 1e9;
    double best[3] = {0, 0, 0};
    const double q_starts[] = {q_guess, 2.0, 6.0, 12.0};
    for (double q0 : q_starts) {
        double p[3] = {std::log(d.f[pk]), std::log(q0), sm[pk]};
        const double step[3] = {0.1, 0.3, 3.0};
        const double c = nelder_mead(d, p, step);
        if (c < best_cost) { best_cost = c; for (int j = 0; j < 3; j++) best[j] = p[j]; }
    }
    if (best_cost > 3.0) return out;

    out.center_freq = (float)std::exp(best[0]);
    out.q = (float)std::exp(best[1]);
    out.peak_gain_db = (float)best[2];
    out.fit_rmse_db = (float)best_cost;
    out.valid = true;
    return out;
}

DelayEstimate estimate_delay(const float* stimulus, const float* response,
                             int num_samples, int max_delay) {
    DelayEstimate out;
    if (!stimulus || !response || max_delay < 0) return out;
    if (num_samples < 1024 || (long long)num_samples < 4LL * max_delay) return out;

    double exx = 0, eyy = 0;
    for (int i = 0; i < num_samples; i++) {
        exx += (double)stimulus[i] * stimulus[i];
        eyy += (double)response[i] * response[i];
    }
    if (exx < 1e-30 || eyy < 1e-30) return out;

    // Cross-correlation r[l] = sum_n x[n] y[n+l] through the FFT. P >= n + max_delay, so
    // the circular correlation does not wrap for the lags 0..max_delay.
    int P = 1;
    while (P < num_samples + max_delay) P <<= 1;
    std::vector<Cd> tw(P / 2);
    for (int k = 0; k < P / 2; k++) tw[k] = std::polar(1.0, -2.0 * kPi * k / P);
    std::vector<Cd> X(P, Cd(0, 0)), Y(P, Cd(0, 0));
    for (int i = 0; i < num_samples; i++) { X[i] = stimulus[i]; Y[i] = response[i]; }
    fft(X, tw);
    fft(Y, tw);
    for (int k = 0; k < P; k++) X[k] = std::conj(std::conj(X[k]) * Y[k]);  // conj: inverse FFT trick
    fft(X, tw);                                                           // r[l] = conj(X[l]) / P

    const double norm = std::sqrt(exx * eyy) * P;
    int best = 0;
    double best_abs = -1.0;
    for (int l = 0; l <= max_delay; l++) {
        const double a = std::abs(X[l].real());
        if (a > best_abs) { best_abs = a; best = l; }
    }
    const double peak = best_abs / norm;
    if (peak < 0.2) return out;
    out.samples = best;
    out.peak = (float)(peak > 1.0 ? 1.0 : peak);
    out.valid = true;
    return out;
}

namespace {

// Pedal over loopback, bin by bin: dB difference and the lower coherence (D27).
TransferFunction divide_chain(const TransferFunction& td, const TransferFunction& tl) {
    TransferFunction out;
    out.freq_hz = td.freq_hz;
    out.magnitude_db.resize(td.freq_hz.size());
    out.coherence.resize(td.freq_hz.size());
    for (size_t b = 0; b < td.freq_hz.size(); b++) {
        out.magnitude_db[b] = td.magnitude_db[b] - tl.magnitude_db[b];
        out.coherence[b] = std::min(td.coherence[b], tl.coherence[b]);
    }
    out.num_segments = std::min(td.num_segments, tl.num_segments);
    out.valid = true;
    return out;
}

}  // namespace

TransferFunction measure_calibrated(const float* stimulus, const float* dut, const float* loopback,
                                    int num_samples, float sample_rate, int max_delay,
                                    int segment_size) {
    TransferFunction out;
    if (!stimulus || !dut || !loopback) return out;
    const DelayEstimate d = estimate_delay(stimulus, loopback, num_samples, max_delay);
    if (!d.valid) return out;

    // The latency trims both recordings: the stimulus keeps its start, the responses lose theirs.
    const int n = num_samples - d.samples;
    const TransferFunction td = measure_transfer_function(stimulus, dut + d.samples, n, sample_rate, segment_size);
    const TransferFunction tl = measure_transfer_function(stimulus, loopback + d.samples, n, sample_rate, segment_size);
    if (!td.valid || !tl.valid) return out;

    out = divide_chain(td, tl);
    return out;
}

std::vector<float> generate_stimulus(int num_samples, std::uint32_t seed, float rms) {
    if (num_samples < 1 || seed == 0 || !(rms > 0.0f && rms <= 0.5f)) return std::vector<float>();

    std::vector<double> v(num_samples);
    std::uint32_t s = seed;  // xorshift32, never 0 for a non-zero seed
    double sum = 0.0;
    for (int i = 0; i < num_samples; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        v[i] = (double)s / 2147483648.0 - 1.0;  // uniform in [-1, 1)
        sum += v[i];
    }
    const double mean = sum / num_samples;
    double sq = 0.0;
    for (double& x : v) { x -= mean; sq += x * x; }
    const double gain = (double)rms / std::sqrt(sq / num_samples);

    std::vector<float> out(num_samples);
    for (int i = 0; i < num_samples; i++) out[i] = (float)(v[i] * gain);
    return out;
}

std::vector<TrackPoint> track_bandpass(const float* stimulus, const float* dut, const float* loopback,
                                       int num_samples, float sample_rate, int max_delay,
                                       float window_s, int segment_size,
                                       float freq_min, float freq_max, float min_coherence) {
    std::vector<TrackPoint> out;
    if (!stimulus || !dut || !loopback || sample_rate <= 0.0f || !(window_s > 0.0f)) return out;
    if (segment_size < 64 || !is_pow2(segment_size)) return out;
    const DelayEstimate d = estimate_delay(stimulus, loopback, num_samples, max_delay);
    if (!d.valid) return out;

    const int n = num_samples - d.samples;
    const int win = (int)std::lround((double)window_s * sample_rate);
    if (win < 4 * segment_size) return out;  // 4 segments is what measure_transfer_function() needs; a window longer than the recording gives no windows

    const TransferFunction tl = measure_transfer_function(stimulus, loopback + d.samples, n, sample_rate, segment_size);
    if (!tl.valid) return out;

    const int hop = win / 2;
    for (int off = 0; off + win <= n; off += hop) {
        TrackPoint p;
        p.time_s = (float)((off + win / 2.0) / sample_rate);
        const TransferFunction td = measure_transfer_function(stimulus + off, dut + d.samples + off, win,
                                                              sample_rate, segment_size);
        if (td.valid) p.estimate = estimate_bandpass(divide_chain(td, tl), freq_min, freq_max, min_coherence);
        out.push_back(p);
    }
    return out;
}

std::vector<ShapeTrackPoint> track_shape(const float* stimulus, const float* dut, const float* loopback,
                                          int num_samples, float sample_rate, int max_delay,
                                          float window_s, int segment_size,
                                          float freq_min, float freq_max, float min_coherence) {
    std::vector<ShapeTrackPoint> out;
    if (!stimulus || !dut || !loopback || sample_rate <= 0.0f || !(window_s > 0.0f)) return out;
    if (segment_size < 64 || !is_pow2(segment_size)) return out;
    const DelayEstimate d = estimate_delay(stimulus, loopback, num_samples, max_delay);
    if (!d.valid) return out;

    const int n = num_samples - d.samples;
    const int win = (int)std::lround((double)window_s * sample_rate);
    if (win < 4 * segment_size) return out;

    const TransferFunction tl = measure_transfer_function(stimulus, loopback + d.samples, n, sample_rate, segment_size);
    if (!tl.valid) return out;

    const int hop = win / 2;
    for (int off = 0; off + win <= n; off += hop) {
        ShapeTrackPoint p;
        p.time_s = (float)((off + win / 2.0) / sample_rate);
        const TransferFunction td = measure_transfer_function(stimulus + off, dut + d.samples + off, win, sample_rate, segment_size);
        if (td.valid) p.shape = fit_shape(divide_chain(td, tl), freq_min, freq_max, min_coherence);
        out.push_back(p);
    }
    return out;
}

namespace {

// Weighted median: the first value (in ascending order) where the cumulative weight reaches half.
double weighted_median(std::vector<std::pair<double, double>>& vw) {
    std::sort(vw.begin(), vw.end());
    double total = 0;
    for (const auto& p : vw) total += p.second;
    double cum = 0;
    for (const auto& p : vw) { cum += p.second; if (cum >= 0.5 * total) return p.first; }
    return vw.back().first;
}

}  // namespace

AggregatedCurve aggregate_passes(const std::vector<std::vector<PassPoint>>& passes, int num_points) {
    AggregatedCurve out;
    if (passes.empty() || num_points < 2) return out;
    const int P = (int)passes.size(), G = num_points;
    const double kMaxGap = 0.25, kDrop = std::log(1.15);

    // Every pass on the grid: log centre and weight (weight 0 = no data at that position).
    std::vector<std::vector<double>> lc(P, std::vector<double>(G, 0.0)), wt(P, std::vector<double>(G, 0.0));
    std::vector<bool> has_data(P, false);
    for (int k = 0; k < P; k++) {
        std::vector<PassPoint> v;
        for (const PassPoint& p : passes[k])
            if (p.position >= 0.0f && p.position <= 1.0f && p.center_freq > 0.0f && p.weight > 0.0f) v.push_back(p);
        std::sort(v.begin(), v.end(), [](const PassPoint& a, const PassPoint& b) { return a.position < b.position; });
        std::vector<double> pos, lg, w;  // merged: points at the same position become one
        for (size_t i = 0; i < v.size();) {
            size_t j = i;
            double sw = 0, sl = 0;
            while (j < v.size() && v[j].position - v[i].position < 1e-4) { sw += v[j].weight; sl += v[j].weight * std::log((double)v[j].center_freq); j++; }
            pos.push_back(v[i].position); lg.push_back(sl / sw); w.push_back(sw / (double)(j - i));
            i = j;
        }
        for (int g = 0; g < G && !pos.empty(); g++) {
            const double x = (double)g / (G - 1);
            if (x < pos.front() - 1e-6 || x > pos.back() + 1e-6) continue;
            size_t hi = std::lower_bound(pos.begin(), pos.end(), x - 1e-6) - pos.begin();
            if (std::abs(pos[hi] - x) <= 1e-6) { lc[k][g] = lg[hi]; wt[k][g] = w[hi]; continue; }
            const size_t lo = hi - 1;
            if (pos[hi] - pos[lo] > kMaxGap) continue;
            const double f = (x - pos[lo]) / (pos[hi] - pos[lo]);
            lc[k][g] = lg[lo] + f * (lg[hi] - lg[lo]);
            wt[k][g] = w[lo] + f * (w[hi] - w[lo]);
        }
        for (int g = 0; g < G; g++) if (wt[k][g] > 0) has_data[k] = true;
    }

    // Reference curve over all passes, then the passes that disagree with it are dropped.
    auto median_at = [&](int g, const std::vector<bool>& use, double& value) {
        std::vector<std::pair<double, double>> vw;
        for (int k = 0; k < P; k++) if (use[k] && wt[k][g] > 0) vw.push_back({lc[k][g], wt[k][g]});
        if (vw.empty()) return 0;
        value = weighted_median(vw);
        return (int)vw.size();
    };
    std::vector<bool> keep = has_data;
    if (P >= 3) {
        std::vector<double> ref(G, 0.0);
        std::vector<int> n(G, 0);
        for (int g = 0; g < G; g++) n[g] = median_at(g, keep, ref[g]);
        for (int k = 0; k < P; k++) {
            if (!keep[k]) continue;
            int bad = 0;
            for (int g = 0; g < G; g++) if (n[g] > 0 && wt[k][g] > 0 && std::abs(lc[k][g] - ref[g]) > kDrop) bad++;
            if (4 * bad > G) keep[k] = false;
        }
    }
    for (int k = 0; k < P; k++) if (!keep[k]) out.dropped.push_back(k);

    out.position.resize(G); out.center_freq.assign(G, 0.0f); out.support.assign(G, 0);
    out.std_error.assign(G, std::numeric_limits<float>::infinity());
    bool any = false;
    for (int g = 0; g < G; g++) {
        out.position[g] = (float)g / (G - 1);
        double v = 0;
        out.support[g] = median_at(g, keep, v);
        if (out.support[g] > 0) { out.center_freq[g] = (float)std::exp(v); any = true; }
        if (out.support[g] >= 3) {  // D35, D38: the spread of fewer than 3 passes says nothing
            std::vector<std::pair<double, double>> dev;
            for (int k = 0; k < P; k++) if (keep[k] && wt[k][g] > 0) dev.push_back({std::abs(lc[k][g] - v), wt[k][g]});
            out.std_error[g] = (float)(1.2533 * 1.4826 * weighted_median(dev) / std::sqrt((double)out.support[g]));
        }
    }
    out.valid = any;
    return out;
}

namespace {

float ratio_of(float error, float target) { return error / target; }  // infinity stays infinity

CaptureProgress progress_of(const std::vector<std::vector<PassPoint>>& passes, const HoldsAggregate* holds,
                            CaptureTarget target, int num_points) {
    CaptureProgress out;
    const AggregatedCurve a = aggregate_passes(passes, num_points);
    if (passes.empty() || num_points < 2) return out;
    const float inf = std::numeric_limits<float>::infinity();

    out.valid = true;
    out.passes_recorded = (int)passes.size();
    out.passes_dropped = (int)a.dropped.size();
    out.passes_kept = out.passes_recorded - out.passes_dropped;
    out.target = target == CaptureTarget::Fast ? 0.06f : (target == CaptureTarget::Normal ? 0.03f : 0.015f);
    out.gain_target_db = target == CaptureTarget::Fast ? 0.5f : (target == CaptureTarget::Normal ? 0.25f : 0.1f);
    out.confidence = a.std_error.empty() ? inf : *std::max_element(a.std_error.begin(), a.std_error.end());

    // The quantities looked at, in the order of D49: the curve, then Q and gain at each end.
    float err[5] = {out.confidence, inf, inf, inf, inf}, tgt[5] = {out.target, out.target, out.gain_target_db, out.target, out.gain_target_db};
    int n = 1;
    bool sure = out.confidence <= out.target;
    if (holds) {
        err[1] = out.heel_q_error = holds->heel.q_error;  err[2] = out.heel_gain_error_db = holds->heel.gain_error_db;
        err[3] = out.toe_q_error = holds->toe.q_error;    err[4] = out.toe_gain_error_db = holds->toe.gain_error_db;
        n = 5;
        for (int i = 1; i < 5; i++) if (!(err[i] <= tgt[i])) sure = false;  // an infinite error is not sure
    }
    out.worst_ratio = -1.0f;
    for (int i = 0; i < n; i++)
        if (ratio_of(err[i], tgt[i]) > out.worst_ratio) { out.worst_ratio = ratio_of(err[i], tgt[i]); out.limiting = (CaptureLimit)i; }

    if (out.passes_kept >= 4 && sure) out.state = CaptureState::Done;
    else if (out.passes_recorded >= 20) out.state = CaptureState::MaximumReached;
    else if (out.passes_kept < 4) out.state = CaptureState::NeedMorePasses;
    else out.state = CaptureState::ConfidenceNotReached;
    out.stop = out.state == CaptureState::Done || out.state == CaptureState::MaximumReached;
    return out;
}

}  // namespace

CaptureProgress capture_progress(const std::vector<std::vector<PassPoint>>& passes, CaptureTarget target, int num_points) {
    return progress_of(passes, nullptr, target, num_points);
}

HoldEstimate estimate_hold(const float* stimulus, const float* dut, const float* loopback,
                           int num_samples, float sample_rate, int max_delay,
                           float hold_start_s, float hold_end_s,
                           float window_s, int segment_size, float settle_s,
                           float freq_min, float freq_max, float min_coherence) {
    HoldEstimate out;
    if (!stimulus || !dut || !loopback || !(sample_rate > 0.0f) || !(window_s > 0.0f) || !(settle_s >= 0.0f)) return out;
    if (!(hold_start_s >= 0.0f) || !(hold_end_s > hold_start_s)) return out;
    if (segment_size < 64 || !is_pow2(segment_size)) return out;
    const DelayEstimate d = estimate_delay(stimulus, loopback, num_samples, max_delay);
    if (!d.valid) return out;

    const int n = num_samples - d.samples;
    const int win = (int)std::lround((double)window_s * sample_rate);
    if (win < 4 * segment_size) return out;
    const long long last_end = (long long)std::floor((double)hold_end_s * sample_rate);
    if (last_end > n) return out;  // the hold must lie inside the recording

    const TransferFunction tl = measure_transfer_function(stimulus, loopback + d.samples, n, sample_rate, segment_size);
    if (!tl.valid) return out;

    std::vector<std::pair<double, double>> centre, q, gain;  // (value, weight) per valid window
    double cmin = 0, cmax = 0;
    const int hop = win / 2;
    for (long long off = std::llround((double)(hold_start_s + settle_s) * sample_rate); off + win <= last_end; off += hop) {
        const TransferFunction td = measure_transfer_function(stimulus + off, dut + d.samples + off, win,
                                                              sample_rate, segment_size);
        if (!td.valid) continue;
        const TransferFunction tf = divide_chain(td, tl);
        const BandpassEstimate e = estimate_bandpass(tf, freq_min, freq_max, min_coherence);
        if (!e.valid) continue;
        double sum = 0; int bins = 0;
        for (size_t b = 0; b < tf.freq_hz.size(); b++)
            if (tf.freq_hz[b] >= freq_min && tf.freq_hz[b] <= freq_max) { sum += tf.coherence[b]; bins++; }
        const double w = bins > 0 ? sum / bins : 0.0;
        if (!(w > 0.0)) continue;
        centre.push_back({e.center_freq, w}); q.push_back({e.q, w}); gain.push_back({e.peak_gain_db, w});
        cmin = centre.size() == 1 ? e.center_freq : std::min(cmin, (double)e.center_freq);
        cmax = centre.size() == 1 ? e.center_freq : std::max(cmax, (double)e.center_freq);
    }
    if (centre.size() < 2) return out;           // D41
    if (cmax > 1.05 * cmin) return out;          // D43: the pedal was still moving

    out.center_freq = (float)weighted_median(centre);
    out.q = (float)weighted_median(q);
    out.peak_gain_db = (float)weighted_median(gain);
    out.windows_used = (int)centre.size();
    out.valid = true;
    return out;
}

namespace {

struct EndHold { double lf, lq, g, w; int idx; };  // log centre, log Q, gain in dB, weight, index in the input

double wmed(const std::vector<EndHold>& v, double EndHold::*m) {
    std::vector<std::pair<double, double>> vw;
    for (const EndHold& h : v) vw.push_back({h.*m, h.w});
    return weighted_median(vw);
}

// 1.2533 * 1.4826 * weighted MAD / sqrt(n), D45
double median_error(const std::vector<EndHold>& v, double EndHold::*m, double med) {
    std::vector<std::pair<double, double>> dev;
    for (const EndHold& h : v) dev.push_back({std::abs(h.*m - med), h.w});
    return 1.2533 * 1.4826 * weighted_median(dev) / std::sqrt((double)v.size());
}

HoldSummary summarise_holds(const std::vector<HoldEstimate>& holds) {
    HoldSummary out;
    const float inf = std::numeric_limits<float>::infinity();
    out.center_error = out.q_error = out.gain_error_db = inf;

    std::vector<EndHold> usable;
    for (size_t i = 0; i < holds.size(); i++) {
        const HoldEstimate& h = holds[i];
        const bool ok = h.valid && std::isfinite(h.center_freq) && std::isfinite(h.q) && std::isfinite(h.peak_gain_db) &&
                        h.center_freq > 0.0f && h.q > 0.0f && h.windows_used > 0;
        if (ok) usable.push_back({std::log((double)h.center_freq), std::log((double)h.q), (double)h.peak_gain_db,
                                  (double)h.windows_used, (int)i});
        else out.dropped.push_back((int)i);
    }
    if (usable.empty()) return out;

    std::vector<EndHold> kept = usable;
    if (usable.size() >= 3) {  // D44: below 3 holds there is no consensus to measure against
        const double rf = wmed(usable, &EndHold::lf), rq = wmed(usable, &EndHold::lq), rg = wmed(usable, &EndHold::g);
        kept.clear();
        for (const EndHold& h : usable) {
            if (std::abs(h.lf - rf) > std::log(1.15) || std::abs(h.lq - rq) > std::log(1.25) || std::abs(h.g - rg) > 2.0)
                out.dropped.push_back(h.idx);
            else kept.push_back(h);
        }
    }
    std::sort(out.dropped.begin(), out.dropped.end());

    const double mf = wmed(kept, &EndHold::lf), mq = wmed(kept, &EndHold::lq), mg = wmed(kept, &EndHold::g);
    out.center_freq = (float)std::exp(mf);
    out.q = (float)std::exp(mq);
    out.peak_gain_db = (float)mg;
    out.kept = (int)kept.size();
    if (kept.size() >= 3) {
        out.center_error = (float)median_error(kept, &EndHold::lf, mf);
        out.q_error = (float)median_error(kept, &EndHold::lq, mq);
        out.gain_error_db = (float)median_error(kept, &EndHold::g, mg);
    }
    out.valid = true;
    return out;
}

}  // namespace

HoldsAggregate aggregate_holds(const std::vector<HoldEstimate>& heel_holds, const std::vector<HoldEstimate>& toe_holds) {
    HoldsAggregate out;
    out.heel = summarise_holds(heel_holds);
    out.toe = summarise_holds(toe_holds);
    return out;
}

namespace {

// 2-pole band-pass times a first-order low shelf, in dB (D7). p = (ln f0, ln Q, gain dB, ln fs, shelf dB).
double shelf_model_db(double f, const double* p) {
    const double G = std::pow(10.0, p[4] / 20.0), fs = std::exp(p[3]);
    const double x = (f / fs) * (f / fs);
    return model_db(f, std::exp(p[0]), std::exp(p[1]), p[2]) + 10.0 * std::log10((G * G + x) / (1.0 + x));
}

// Weighted RMS error in dB of the shelf model; large if a parameter is out of bounds. The shelf is a LOW
// shelf: its corner is at or below the centre and its gain within 20 dB, or a wide band-pass and a shelf
// that cancel each other could imitate a flat response; Q is at least 1 for the same reason.
double shelf_cost(const FitData& d, const double* p) {
    const double f0 = std::exp(p[0]), q = std::exp(p[1]), fs = std::exp(p[3]);
    if (f0 < d.fmin || f0 > d.fmax || q < 1.0 || q > 100.0 || fs < d.fmin || fs > f0 || std::abs(p[4]) > 20.0)
        return 1e6;
    double se = 0, sw = 0;
    for (size_t i = 0; i < d.f.size(); i++) {
        const double e = d.db[i] - shelf_model_db(d.f[i], p);
        se += d.w[i] * e * e;
        sw += d.w[i];
    }
    return std::sqrt(se / sw);
}

// Four-pole model: the product of two 2-pole band-passes, in dB. p = (ln f1, ln Q1, ln f2, ln Q2, gain dB):
// the two peak gains only ever appear as their sum, so there is one gain parameter (D56).
double four_model_db(double f, const double* p) {
    return model_db(f, std::exp(p[0]), std::exp(p[1]), p[4]) + model_db(f, std::exp(p[2]), std::exp(p[3]), 0.0);
}

// The first centre is the lower one, the two are between 10 % and 2 octaves apart (further apart they make a
// flat plateau between them, which would fit a flat response), each Q is at least 1 (D56).
double four_cost(const FitData& d, const double* p) {
    const double f1 = std::exp(p[0]), q1 = std::exp(p[1]), f2 = std::exp(p[2]), q2 = std::exp(p[3]);
    if (f1 < d.fmin || f2 > d.fmax || f2 < 1.1 * f1 || f2 > 4.0 * f1 || q1 < 1.0 || q1 > 100.0 || q2 < 1.0 || q2 > 100.0 ||
        std::abs(p[4]) > 60.0)
        return 1e6;
    double se = 0, sw = 0;
    for (size_t i = 0; i < d.f.size(); i++) {
        const double e = d.db[i] - four_model_db(d.f[i], p);
        se += d.w[i] * e * e;
        sw += d.w[i];
    }
    return std::sqrt(se / sw);
}

// Nelder-Mead on N parameters for any cost(p). Returns the best cost; p holds the best point.
template <int N, class Cost>
double nelder_mead_n(Cost cost, double* p, const double* step) {
    double s[N + 1][N], c[N + 1];
    for (int i = 0; i <= N; i++) {
        for (int j = 0; j < N; j++) s[i][j] = p[j] + (i == j + 1 ? step[j] : 0.0);
        c[i] = cost(s[i]);
    }
    for (int it = 0; it < 1500; it++) {
        int lo = 0, hi = 0;
        for (int i = 1; i <= N; i++) { if (c[i] < c[lo]) lo = i; if (c[i] > c[hi]) hi = i; }
        int nx = lo;
        for (int i = 0; i <= N; i++) if (i != hi && (nx == lo || c[i] > c[nx])) nx = i;
        if (c[hi] - c[lo] < 1e-7) break;
        double m[N] = {};
        for (int i = 0; i <= N; i++) if (i != hi) for (int j = 0; j < N; j++) m[j] += s[i][j] / N;
        auto at = [&](double t, double* out) { for (int j = 0; j < N; j++) out[j] = m[j] + t * (s[hi][j] - m[j]); };
        double xr[N], xe[N], xc[N];
        at(-1.0, xr); const double cr = cost(xr);
        if (cr < c[lo]) {
            at(-2.0, xe); const double ce = cost(xe);
            for (int j = 0; j < N; j++) s[hi][j] = (ce < cr) ? xe[j] : xr[j];
            c[hi] = (ce < cr) ? ce : cr;
        } else if (cr < c[nx]) {
            for (int j = 0; j < N; j++) s[hi][j] = xr[j];
            c[hi] = cr;
        } else {
            at(cr < c[hi] ? -0.5 : 0.5, xc); const double cc = cost(xc);
            if (cc < std::min(cr, c[hi])) {
                for (int j = 0; j < N; j++) s[hi][j] = xc[j];
                c[hi] = cc;
            } else {
                for (int i = 0; i <= N; i++) {
                    if (i == lo) continue;
                    for (int j = 0; j < N; j++) s[i][j] = s[lo][j] + 0.5 * (s[i][j] - s[lo][j]);
                    c[i] = cost(s[i]);
                }
            }
        }
    }
    int lo = 0;
    for (int i = 1; i <= N; i++) if (c[i] < c[lo]) lo = i;
    for (int j = 0; j < N; j++) p[j] = s[lo][j];
    return c[lo];
}


ShapeFit set_shelf(ShapeFit out, const double* p, double rmse) {
    out.model = ShapeModel::BandPass2Shelf;
    out.center_freq = (float)std::exp(p[0]);
    out.q = (float)std::exp(p[1]);
    out.peak_gain_db = (float)p[2];
    out.shelf_freq_hz = (float)std::exp(p[3]);
    out.shelf_gain_db = (float)p[4];
    out.fit_rmse_db = (float)rmse;
    out.best_model_rmse_db = (float)rmse;
    out.valid = true;
    return out;
}

ShapeFit set_four(ShapeFit out, const double* p, double rmse) {
    out.model = ShapeModel::BandPass4;
    out.center_freq = (float)std::exp(p[0]);
    out.q = (float)std::exp(p[1]);
    out.center2_freq = (float)std::exp(p[2]);
    out.q2 = (float)std::exp(p[3]);
    out.peak_gain_db = (float)p[4];
    out.fit_rmse_db = (float)rmse;
    out.best_model_rmse_db = (float)rmse;
    out.valid = true;
    return out;
}


// The measurement as a table (D58): kShapeTablePoints cells evenly spaced in log frequency from fmin to fmax,
// each the coherence-weighted mean dB of the bins in it; empty cells are interpolated in log frequency
// between the cells either side, or held at the nearest value at an end. False if no cell has data or a run of
// empty cells is wider than an octave.
bool build_table(const FitData& d, std::vector<float>& freq, std::vector<float>& db) {
    const int N = kShapeTablePoints;
    const double span = std::log(d.fmax / d.fmin);
    std::vector<double> sw(N, 0.0), swv(N, 0.0), centre(N);
    for (int k = 0; k < N; k++) centre[k] = d.fmin * std::exp(span * (k + 0.5) / N);
    for (size_t i = 0; i < d.f.size(); i++) {
        int k = (int)(N * std::log(d.f[i] / d.fmin) / span);
        k = std::min(std::max(k, 0), N - 1);
        const double coh = d.w[i] * d.f[i];  // w = coherence / f
        sw[k] += coh;
        swv[k] += coh * d.db[i];
    }
    const double cell_octaves = span / std::log(2.0) / N;
    std::vector<double> v(N, 0.0);
    std::vector<int> have;
    for (int k = 0; k < N; k++) if (sw[k] > 0) { v[k] = swv[k] / sw[k]; have.push_back(k); }
    if (have.empty()) return false;
    if (have.front() * cell_octaves > 1.0 || (N - 1 - have.back()) * cell_octaves > 1.0) return false;
    for (size_t h = 0; h + 1 < have.size(); h++)
        if ((have[h + 1] - have[h] - 1) * cell_octaves > 1.0) return false;
    for (int k = 0; k < have.front(); k++) v[k] = v[have.front()];
    for (int k = have.back() + 1; k < N; k++) v[k] = v[have.back()];
    for (size_t h = 0; h + 1 < have.size(); h++) {
        const int a = have[h], b = have[h + 1];
        for (int k = a + 1; k < b; k++) {
            const double t = std::log(centre[k] / centre[a]) / std::log(centre[b] / centre[a]);
            v[k] = v[a] + t * (v[b] - v[a]);
        }
    }
    freq.resize(N);
    db.resize(N);
    for (int k = 0; k < N; k++) { freq[k] = (float)centre[k]; db[k] = (float)v[k]; }
    return true;
}
}  // namespace

ShapeFit fit_shape(const TransferFunction& tf, float freq_min, float freq_max, float min_coherence) {
    ShapeFit out;
    if (!tf.valid || tf.freq_hz.size() != tf.magnitude_db.size() ||
        tf.freq_hz.size() != tf.coherence.size() || freq_min <= 0 || freq_max <= freq_min)
        return out;

    FitData d;
    d.fmin = freq_min;
    d.fmax = freq_max;
    for (size_t i = 0; i < tf.freq_hz.size(); i++) {
        const double f = tf.freq_hz[i];
        if (f < freq_min || f > freq_max || tf.coherence[i] < min_coherence) continue;
        d.f.push_back(f);
        d.db.push_back(tf.magnitude_db[i]);
        d.w.push_back(tf.coherence[i] / f);
    }
    if (d.f.size() < 20) return out;

    // The 2-pole first. estimate_bandpass() is invalid above 3 dB, which D7 would refuse anyway.
    const BandpassEstimate bp = estimate_bandpass(tf, freq_min, freq_max, min_coherence);
    if (bp.valid && bp.fit_rmse_db <= 1.5f) {
        out.model = ShapeModel::BandPass2;
        out.center_freq = bp.center_freq;
        out.q = bp.q;
        out.peak_gain_db = bp.peak_gain_db;
        out.fit_rmse_db = bp.fit_rmse_db;
        out.best_model_rmse_db = bp.fit_rmse_db;
        out.valid = true;
        return out;
    }

    // The 2-pole with a shelf: several starts (centre, Q, corner, shelf sign), then a restart of the best.
    const int n = (int)d.f.size();
    int pk = 0;
    for (int i = 1; i < n; i++) if (d.db[i] > d.db[pk]) pk = i;
    double best_cost = 1e9, best[5] = {0, 0, 0, 0, 0};
    const double f0s[] = {d.f[pk], 400.0, 800.0, 1600.0};
    const double qs[] = {2.0, 5.0};
    const double fcs[] = {150.0, 400.0};
    const double sgs[] = {-8.0, 8.0};
    for (double f0 : f0s)
        for (double q : qs)
            for (double fc : fcs)
                for (double sg : sgs) {
                    double p[5] = {std::log(std::min(std::max(f0, (double)freq_min * 1.01), (double)freq_max * 0.99)),
                                   std::log(q), d.db[pk],
                                   std::log(std::min(std::max(fc, (double)freq_min * 1.01), (double)freq_max * 0.99)), sg};
                    const double step[5] = {0.1, 0.3, 3.0, 0.3, 3.0};
                    const double cst = nelder_mead_n<5>([&](const double* q_) { return shelf_cost(d, q_); }, p, step);
                    if (cst < best_cost) { best_cost = cst; for (int j = 0; j < 5; j++) best[j] = p[j]; }
                }
    {
        const double step[5] = {0.05, 0.15, 1.5, 0.15, 1.5};
        const double cst = nelder_mead_n<5>([&](const double* q_) { return shelf_cost(d, q_); }, best, step);
        if (cst < best_cost) best_cost = cst;
    }

    // D53: the first (simplest) model within 1.5 dB; if none, the lowest residual within 3 dB.
    const bool shelf_ok = best_cost <= 3.0;
    if (best_cost <= 1.5) return set_shelf(out, best, best_cost);

    // The 4-pole, only when the simpler two are not enough: several starts for the two centres.
    double best4_cost = 1e9, best4[5] = {0, 0, 0, 0, 0};
    const double c1s[] = {150.0, 300.0, 600.0, 1200.0};
    const double c2s[] = {1.5, 2.5, 3.5};  // second centre as a multiple of the first
    const double q4s[] = {2.0, 5.0};
    for (double c1 : c1s)
        for (double c2 : c2s)
            for (double q4 : q4s) {
                const double lo_f = (double)freq_min * 1.01, hi_f = (double)freq_max * 0.99;
                double p[5] = {std::log(std::min(std::max(c1, lo_f), hi_f / 1.2)), std::log(q4),
                               std::log(std::min(c1 * c2, hi_f)), std::log(q4), d.db[pk]};
                const double step[5] = {0.1, 0.3, 0.1, 0.3, 3.0};
                const double cst = nelder_mead_n<5>([&](const double* q_) { return four_cost(d, q_); }, p, step);
                if (cst < best4_cost) { best4_cost = cst; for (int j = 0; j < 5; j++) best4[j] = p[j]; }
            }
    {
        const double step[5] = {0.05, 0.15, 0.05, 0.15, 1.5};
        const double cst = nelder_mead_n<5>([&](const double* q_) { return four_cost(d, q_); }, best4, step);
        if (cst < best4_cost) best4_cost = cst;
    }
    if (best4_cost <= 1.5) return set_four(out, best4, best4_cost);

    const bool bp_ok = bp.valid, four_ok = best4_cost <= 3.0;
    double lowest = 3.0 + 1e-9;
    int pick = 0;  // 0 none, 1 2-pole, 2 shelf, 3 4-pole
    if (bp_ok && bp.fit_rmse_db < lowest) { lowest = bp.fit_rmse_db; pick = 1; }
    if (shelf_ok && best_cost < lowest) { lowest = best_cost; pick = 2; }
    if (four_ok && best4_cost < lowest) { lowest = best4_cost; pick = 3; }
    if (pick == 1) {
        out.model = ShapeModel::BandPass2;
        out.center_freq = bp.center_freq;
        out.q = bp.q;
        out.peak_gain_db = bp.peak_gain_db;
        out.fit_rmse_db = bp.fit_rmse_db;
        out.best_model_rmse_db = bp.fit_rmse_db;
        out.valid = true;
    } else if (pick == 2) {
        return set_shelf(out, best, best_cost);
    } else if (pick == 3) {
        return set_four(out, best4, best4_cost);
    } else if (build_table(d, out.table_freq_hz, out.table_db)) {
        // D58: no model within 3 dB, but the data are a measurement: the table itself.
        out.model = ShapeModel::Measured;
        out.fit_rmse_db = 0.0f;
        out.best_model_rmse_db = (float)std::min(std::min(best_cost, best4_cost), bp_ok ? (double)bp.fit_rmse_db : 1e9);
        out.valid = true;
    } else {
        out.table_freq_hz.clear();
        out.table_db.clear();
    }
    return out;
}

CaptureProgress capture_progress(const std::vector<std::vector<PassPoint>>& passes,
                                 const std::vector<HoldEstimate>& heel_holds, const std::vector<HoldEstimate>& toe_holds,
                                 CaptureTarget target, int num_points) {
    const HoldsAggregate h = aggregate_holds(heel_holds, toe_holds);
    return progress_of(passes, &h, target, num_points);
}


HoldShape fit_hold_shape(const float* stimulus, const float* dut, const float* loopback,
                         int num_samples, float sample_rate, int max_delay,
                         float hold_start_s, float hold_end_s,
                         float window_s, int segment_size, float settle_s,
                         float freq_min, float freq_max, float min_coherence) {
    HoldShape out;
    if (!stimulus || !dut || !loopback || !(sample_rate > 0.0f) || !(window_s > 0.0f) || !(settle_s >= 0.0f)) return out;
    if (!(hold_start_s >= 0.0f) || !(hold_end_s > hold_start_s)) return out;
    if (segment_size < 64 || !is_pow2(segment_size)) return out;
    const DelayEstimate d = estimate_delay(stimulus, loopback, num_samples, max_delay);
    if (!d.valid) return out;

    const int n = num_samples - d.samples;
    const int win = (int)std::lround((double)window_s * sample_rate);
    if (win < 4 * segment_size) return out;
    const long long last_end = (long long)std::floor((double)hold_end_s * sample_rate);
    if (last_end > n) return out;  // the hold must lie inside the recording

    const TransferFunction tl = measure_transfer_function(stimulus, loopback + d.samples, n, sample_rate, segment_size);
    if (!tl.valid) return out;

    // The same windows as estimate_hold (D39, D40): each is fitted, to check that the pedal stood still (D43).
    const int hop = win / 2;
    const long long first = std::llround((double)(hold_start_s + settle_s) * sample_rate);
    int checked = 0;
    double cmin = 0, cmax = 0;
    for (long long off = first; off + win <= last_end; off += hop) {
        const TransferFunction td = measure_transfer_function(stimulus + off, dut + d.samples + off, win, sample_rate, segment_size);
        if (!td.valid) continue;
        const ShapeFit w = fit_shape(divide_chain(td, tl), freq_min, freq_max, min_coherence);
        if (!w.valid || w.model == ShapeModel::Measured || !(w.center_freq > 0.0f)) continue;
        cmin = checked == 0 ? w.center_freq : std::min(cmin, (double)w.center_freq);
        cmax = checked == 0 ? w.center_freq : std::max(cmax, (double)w.center_freq);
        checked++;
    }
    out.windows_checked = checked;
    if (checked < 2) return out;           // D41: a hold needs 2 windows with a centre; with fewer the movement cannot be assessed (D61 proposed)
    if (cmax > 1.05 * cmin) return out;    // D43: the pedal was still moving

    // The shape: one measurement of the whole span from the first window to the end of the hold.
    const long long span = last_end - first;
    const TransferFunction td = measure_transfer_function(stimulus + first, dut + d.samples + first, (int)span, sample_rate, segment_size);
    if (!td.valid) return out;
    out.shape = fit_shape(divide_chain(td, tl), freq_min, freq_max, min_coherence);
    out.valid = out.shape.valid;
    return out;
}

} // namespace wah
