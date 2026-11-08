/**
 * test_estimator.cpp
 * Round 7, decision D2 (moved to the new measurement path by D23): white noise is
 * sent through a band-pass of KNOWN centre frequency, Q and gain; the transfer
 * function is measured with wah::measure_transfer_function() and
 * wah::estimate_bandpass() must recover the centre, the Q and the peak gain
 * within a tolerance.
 *
 * Round 4 wrote this test against wah::analyze(), which reads one spectrum and
 * failed on all four cases. The four cases and the centre and Q tolerances are
 * unchanged; the peak-gain criterion was added with the new API.
 *
 * Exit codes: 0 all cases pass, 1 at least one case fails, 2 the test setup
 * itself is wrong (the generated filter does not have the intended response).
 */

#include "wah_measure.h"
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

const double kPi = 3.14159265358979323846;

// Tolerances, fixed BEFORE any estimator passes. Do not widen them to make the
// test pass. Basis: a throwaway reference (averaged spectrum plus a model fit,
// not in the repository) gets the centre within 1.1 % and Q within 6.6 % on the
// first three cases and within 15.7 % on Q=8 at 2 kHz, so 20 % for Q is the
// honest floor for a simple estimator. The owner may tighten it later.
const double kCenterTol = 0.05;  // +-5 % of the centre frequency
const double kQTol      = 0.20;  // +-20 % of Q
const double kGainTolDb = 1.5;   // +-1.5 dB of peak gain (added with the new API, D23)

struct Biquad { double b0, b1, b2, a1, a2; };

// RBJ band-pass, constant 0 dB peak gain: peak gain 1 at f0, -3 dB bandwidth f0/Q.
Biquad bandpass(double f0, double q, double fs) {
    double w0 = 2.0 * kPi * f0 / fs;
    double alpha = std::sin(w0) / (2.0 * q);
    double a0 = 1.0 + alpha;
    return { alpha / a0, 0.0, -alpha / a0, -2.0 * std::cos(w0) / a0, (1.0 - alpha) / a0 };
}

double magnitude_at(const Biquad& f, double freq, double fs) {
    double w = 2.0 * kPi * freq / fs;
    std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
    return std::abs((f.b0 + f.b1 * z1 + f.b2 * z2) / (1.0 + f.a1 * z1 + f.a2 * z2));
}

// Deterministic white noise in [-1, 1] (xorshift32), same every run.
std::vector<double> white_noise(int n) {
    std::uint32_t s = 2463534242u;
    std::vector<double> v(n);
    for (int i = 0; i < n; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        v[i] = (double)s / 2147483648.0 - 1.0;
    }
    return v;
}

// Independent check that the generated filter really has the intended f0 and Q,
// computed from the analytic response, not with the code under test.
bool filter_is_as_intended(const Biquad& f, double f0, double q, double fs) {
    double peak = magnitude_at(f, f0, fs);
    double lo = f0, hi = f0;
    while (magnitude_at(f, lo, fs) > peak / std::sqrt(2.0)) lo -= 0.05;
    while (magnitude_at(f, hi, fs) > peak / std::sqrt(2.0)) hi += 0.05;
    double q_measured = f0 / (hi - lo);
    return std::abs(peak - 1.0) < 0.01 && std::abs(q_measured - q) / q < 0.02;
}

}  // namespace

int main() {
    const double fs = 48000.0;
    const int n = 1 << 17;  // about 2.7 s

    struct Case { double f0, q, gain_db; };
    const Case cases[] = { {400, 3, 0}, {800, 5, 6}, {1600, 3, -3}, {2000, 8, 10} };

    std::vector<double> noise = white_noise(n);
    int failed = 0;

    for (const Case& c : cases) {
        Biquad f = bandpass(c.f0, c.q, fs);
        if (!filter_is_as_intended(f, c.f0, c.q, fs)) {
            std::cout << "SETUP ERROR: the generated filter for f0=" << c.f0
                      << " Q=" << c.q << " does not have the intended response\n";
            return 2;
        }

        const double gain = std::pow(10.0, c.gain_db / 20.0);
        std::vector<float> out(n), in(n);
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (int i = 0; i < n; i++) {
            double x = noise[i];
            double y = f.b0 * x + f.b1 * x1 + f.b2 * x2 - f.a1 * y1 - f.a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            in[i] = (float)x;
            out[i] = (float)(gain * y);
        }

        wah::TransferFunction tf = wah::measure_transfer_function(in.data(), out.data(), n, (float)fs);
        wah::BandpassEstimate r = wah::estimate_bandpass(tf);

        double center_err = std::abs(r.center_freq - c.f0) / c.f0;
        double q_err = std::abs(r.q - c.q) / c.q;
        double gain_err = std::abs(r.peak_gain_db - c.gain_db);
        bool ok = r.valid && center_err <= kCenterTol && q_err <= kQTol && gain_err <= kGainTolDb;

        std::cout << (ok ? "ok   " : "FAIL ") << "f0=" << c.f0 << " Q=" << c.q << " gain=" << c.gain_db
                  << " dB  ->  valid=" << (r.valid ? "yes" : "no")
                  << " f0=" << r.center_freq << " (" << center_err * 100 << " % off)"
                  << " Q=" << r.q << " (" << q_err * 100 << " % off)"
                  << " gain=" << r.peak_gain_db << " dB (" << gain_err << " dB off)\n";
        if (!ok) failed++;
    }

    // Noisy response (independent noise as strong as the stimulus; at 0.3 the coherence
    // gate was not needed, at 1.0 the fit fails without it): same case as
    // 800 Hz / Q 5 / +6 dB and the same tolerances.
    {
        Biquad f = bandpass(800.0, 5.0, fs);
        std::vector<float> in(n), out(n);
        std::vector<double> nz = white_noise(n);
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (int i = 0; i < n; i++) {
            double x = noise[i];
            double y = f.b0 * x + f.b1 * x1 + f.b2 * x2 - f.a1 * y1 - f.a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            in[i] = (float)x;
            out[i] = (float)(2.0 * y + 1.0 * nz[(i * 5 + 3) % n]);
        }
        wah::BandpassEstimate r = wah::estimate_bandpass(wah::measure_transfer_function(in.data(), out.data(), n, (float)fs));
        bool ok = r.valid && std::abs(r.center_freq - 800.0) / 800.0 <= kCenterTol &&
                  std::abs(r.q - 5.0) / 5.0 <= kQTol && std::abs(r.peak_gain_db - 6.02) <= kGainTolDb;
        std::cout << (ok ? "ok   " : "FAIL ") << "noisy response: f0=" << r.center_freq << " Q=" << r.q
                  << " gain=" << r.peak_gain_db << " dB\n";
        if (!ok) failed++;
    }

    // Not a band-pass: a comb filter (y = x + x delayed by 20 samples) is measured with
    // high coherence but its shape is wrong, so no band-pass estimate may be given.
    {
        std::vector<float> in(n), out(n);
        for (int i = 0; i < n; i++) { in[i] = (float)noise[i]; out[i] = (float)(noise[i] + (i >= 20 ? noise[i - 20] : 0.0)); }
        bool refused = !wah::estimate_bandpass(wah::measure_transfer_function(in.data(), out.data(), n, (float)fs)).valid;
        std::cout << (refused ? "ok   " : "FAIL ") << "comb filter: no band-pass estimate\n";
        if (!refused) failed++;
    }

    // Refusals: an estimate must not be invented from nothing.
    {
        std::vector<float> a(n), b(n);
        std::vector<double> other = white_noise(n);  // same generator, shifted below
        for (int i = 0; i < n; i++) { a[i] = (float)noise[i]; b[i] = (float)other[(i * 7 + 13) % n]; }
        wah::TransferFunction tf = wah::measure_transfer_function(a.data(), b.data(), n, (float)fs);
        bool refused = !wah::estimate_bandpass(tf).valid;
        std::cout << (refused ? "ok   " : "FAIL ") << "unrelated noise: no estimate\n";
        if (!refused) failed++;
        bool refused2 = !wah::estimate_bandpass(wah::TransferFunction()).valid;
        std::cout << (refused2 ? "ok   " : "FAIL ") << "invalid transfer function: no estimate\n";
        if (!refused2) failed++;
    }

    if (failed) {
        std::cout << "\n✗ Test FAILED: " << failed << " check(s) failed (cases outside tolerance or refusals)"
                  << " (centre +-" << kCenterTol * 100 << " %, Q +-" << kQTol * 100
                  << " %, gain +-" << kGainTolDb << " dB)\n";
        return 1;
    }
    std::cout << "\n✓ Test PASSED\n";
    return 0;
}
