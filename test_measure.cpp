/**
 * test_measure.cpp
 * Round 6: wah::measure_transfer_function() must recover the transfer function
 * of a known filter from a stimulus and a response, with a coherence that tells
 * a clean measurement from a noisy one.
 *
 * The reference is the analytic response of the filter, computed here in double
 * precision, not with the code under test.
 *
 * Exit codes: 0 all pass, 1 at least one check fails.
 */

#include "wah_measure.h"
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

const double kPi = 3.14159265358979323846;

struct Biquad { double b0, b1, b2, a1, a2; };

// RBJ band-pass, constant 0 dB peak gain.
Biquad bandpass(double f0, double q, double fs) {
    double w0 = 2.0 * kPi * f0 / fs;
    double alpha = std::sin(w0) / (2.0 * q);
    double a0 = 1.0 + alpha;
    return { alpha / a0, 0.0, -alpha / a0, -2.0 * std::cos(w0) / a0, (1.0 - alpha) / a0 };
}

double magnitude_db_at(const Biquad& f, double freq, double fs) {
    double w = 2.0 * kPi * freq / fs;
    std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
    return 20.0 * std::log10(std::abs((f.b0 + f.b1 * z1 + f.b2 * z2) /
                                      (1.0 + f.a1 * z1 + f.a2 * z2)));
}

struct Rng {  // xorshift32, deterministic
    std::uint32_t s;
    explicit Rng(std::uint32_t seed) : s(seed) {}
    double next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (double)s / 2147483648.0 - 1.0; }
};

std::vector<double> run(const Biquad& f, const std::vector<double>& x) {
    std::vector<double> y(x.size());
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (size_t i = 0; i < x.size(); i++) {
        double v = f.b0 * x[i] + f.b1 * x1 + f.b2 * x2 - f.a1 * y1 - f.a2 * y2;
        x2 = x1; x1 = x[i]; y2 = y1; y1 = v; y[i] = v;
    }
    return y;
}

std::vector<float> to_float(const std::vector<double>& v) {
    return std::vector<float>(v.begin(), v.end());
}

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "ok   " : "FAIL ") << what << "\n";
    if (!ok) failures++;
}

const double kFs = 48000.0;
const int kN = 1 << 17;  // about 2.7 s

// Largest |measured - analytic| in dB over the bins between lo and hi Hz whose
// coherence is above min_coh. count gets the number of bins compared.
double max_error_db(const wah::TransferFunction& tf, const Biquad& f, double lo, double hi,
                    double min_coh, int& count) {
    double worst = 0;
    count = 0;
    if (tf.freq_hz.empty() || tf.magnitude_db.size() != tf.freq_hz.size() ||
        tf.coherence.size() != tf.freq_hz.size())
        return 1e9;  // malformed result: counts as a failure, never a crash
    for (size_t i = 0; i < tf.freq_hz.size(); i++) {
        if (tf.freq_hz[i] < lo || tf.freq_hz[i] > hi || tf.coherence[i] < min_coh) continue;
        double e = std::abs(tf.magnitude_db[i] - magnitude_db_at(f, tf.freq_hz[i], kFs));
        if (e > worst) worst = e;
        count++;
    }
    return worst;
}

double value_at(const std::vector<float>& v, const wah::TransferFunction& tf, double freq) {
    if (tf.freq_hz.empty() || v.size() != tf.freq_hz.size()) return NAN;  // malformed: the check fails
    size_t best = 0;
    for (size_t i = 0; i < tf.freq_hz.size(); i++)
        if (std::abs(tf.freq_hz[i] - freq) < std::abs(tf.freq_hz[best] - freq)) best = i;
    return v[best];
}

}  // namespace

int main() {
    Rng rng(2463534242u);
    std::vector<double> white(kN);
    for (double& v : white) v = rng.next();

    Biquad f = bandpass(800.0, 5.0, kFs);
    std::vector<double> y = run(f, white);

    // 1. White stimulus, clean response with +6 dB of extra gain: the magnitude must
    //    follow the filter (+6 dB at the peak) and the coherence must be near 1.
    {
        std::vector<double> y6(kN);
        for (int i = 0; i < kN; i++) y6[i] = 2.0 * y[i];
        wah::TransferFunction tf = wah::measure_transfer_function(
            to_float(white).data(), to_float(y6).data(), kN, (float)kFs);
        check(tf.valid, "1a valid on a clean white-noise measurement");
        check(tf.num_segments >= 40, "1b at least 40 segments averaged");
        int n = 0;
        Biquad f6 = f; f6.b0 *= 2; f6.b1 *= 2; f6.b2 *= 2;
        double err = max_error_db(tf, f6, 100, 5000, 0.9, n);
        check(n > 300 && err < 1.0, "1c magnitude within 1 dB of the analytic response, 100 Hz to 5 kHz");
        double peak = value_at(tf.magnitude_db, tf, 800.0);
        check(std::abs(peak - 6.02) < 0.5, "1d peak gain at 800 Hz is +6 dB within 0.5 dB");
        check(value_at(tf.coherence, tf, 800.0) > 0.99, "1e coherence at the peak above 0.99");
    }

    // 2. Coloured stimulus (strong low-pass, about -40 dB between 100 Hz and 5 kHz).
    //    A spectrum-of-the-output estimator is fooled by this; a transfer function is not.
    {
        std::vector<double> col(kN);
        double s = 0;
        for (int i = 0; i < kN; i++) { s += 0.03 * (white[i] - s); col[i] = s; }
        std::vector<double> yc = run(f, col);
        wah::TransferFunction tf = wah::measure_transfer_function(
            to_float(col).data(), to_float(yc).data(), kN, (float)kFs);
        int n = 0;
        double err = max_error_db(tf, f, 100, 5000, 0.9, n);
        check(tf.valid && n > 300 && err < 1.0, "2  coloured stimulus: magnitude still within 1 dB");
    }

    // 3. Uncorrelated noise added to the response: the coherence must fall where the
    //    filter output is weak (6 kHz) and stay high at the peak.
    {
        std::vector<double> yn = y;
        Rng rn(123456789u);
        for (double& v : yn) v += 0.3 * rn.next();
        wah::TransferFunction tf = wah::measure_transfer_function(
            to_float(white).data(), to_float(yn).data(), kN, (float)kFs);
        check(tf.valid && value_at(tf.coherence, tf, 6000.0) < 0.2,
              "3a noisy response: coherence at 6 kHz below 0.2");
        double c = value_at(tf.coherence, tf, 800.0);
        check(c > 0.75 && c < 0.99, "3b noisy response: coherence at 800 Hz between 0.75 and 0.99");
    }

    // 4. Bad input must be refused, not measured.
    {
        std::vector<float> a(kN, 0.1f);
        check(!wah::measure_transfer_function(nullptr, a.data(), kN, 48000.f).valid, "4a null stimulus");
        check(!wah::measure_transfer_function(a.data(), nullptr, kN, 48000.f).valid, "4b null response");
        check(!wah::measure_transfer_function(a.data(), a.data(), 1000, 48000.f).valid, "4c too short for 4 segments");
        check(!wah::measure_transfer_function(a.data(), a.data(), kN, 48000.f, 1000).valid, "4d segment size not a power of two");
        check(!wah::measure_transfer_function(a.data(), a.data(), kN, 48000.f, 32).valid, "4e segment size below 64");
    }

    if (failures) {
        std::cout << "\n✗ Test FAILED: " << failures << " check(s)\n";
        return 1;
    }
    std::cout << "\n✓ Test PASSED\n";
    return 0;
}
