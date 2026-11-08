/**
 * wah_measure.cpp
 * Transfer function and coherence by Welch averaging (see wah_measure.h).
 *
 * The FFT here works in double precision with exact twiddle factors; the float
 * FFT in wah_analyze.cpp goes away together with wah::analyze().
 */

#include "wah_measure.h"
#include <cmath>
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

} // namespace wah
