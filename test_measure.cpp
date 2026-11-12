/**
 * test_measure.cpp
 * Round 6: wah::measure_transfer_function() must recover the transfer function
 * of a known filter from a stimulus and a response, with a coherence that tells
 * a clean measurement from a noisy one.
 *
 * The reference is the analytic response of the filter, computed here in double
 * precision, not with the code under test.
 *
 * Round 8: wah::estimate_delay() must find the latency of a chain (whole samples,
 * any polarity, with noise) and refuse what it cannot find.
 *
 * Round 9: wah::measure_calibrated() must recover a pedal measured through a chain with
 * a latency and a strong tilt, by dividing out a loopback measurement of the chain.
 *
 * Round 10: wah::generate_stimulus() must give repeatable, zero-mean noise at the RMS asked\n * for, flat across the band, and be usable as the stimulus of a whole calibrated measurement.
 *
 * Round 11: wah::track_bandpass() must follow a pedal that moves (a full heel-to-toe in 1 s)
 * and read a static pedal precisely with a longer window, behind a latency and a tilting chain.
 *
 * Round 12: wah::aggregate_passes() must fold several passes of a capture into one
 * centre-versus-position curve, dropping bad passes and surviving dropouts.
 *
 * Round 13: wah::capture_progress() must report how sure a capture is and when to stop, from
 * the spread of the kept passes, and must not count the passes that were dropped.
 *
 * Round 14: wah::estimate_hold() must read Q, gain and centre of a pedal held still, behind a
 * latency and a tilting chain, and refuse an interval in which the pedal is still moving.
 *
 * Round 15: wah::aggregate_holds() must fold the holds of all passes into one Q, gain and centre
 * per end, dropping the wrong ones, and say how sure it is.
 *
 * Round 16: wah::capture_progress() with the holds must say which quantity (the curve, or the Q
 * or gain at either end) keeps a capture from being sure, and not stop before all are.
 *
 * Round 17: wah::fit_shape() must pick the 2-pole band-pass or the 2-pole with a low shelf for a
 * calibrated transfer function (the simplest that fits within 1.5 dB), recover its parameters, and
 * refuse a flat or random response.
 *
 * Round 18: wah::fit_shape() must also recognise a 4-pole (two band-passes) that neither the 2-pole
 * nor the shelf fits, recover both centres and Qs, keep the centres ordered and at most 2 octaves
 * apart, and still refuse a flat response.
 *
 * Round 19: wah::fit_shape() must fall back to the measurement itself (a 64-point table) when no
 * model fits within 3 dB, interpolate gaps up to an octave, refuse wider ones, and ignore bins
 * under the coherence floor and outside the range.
 *
 * Round 20: wah::fit_hold_shape() must fit the shape of a pedal held still (the 2-pole model, within the
 * tolerances of round 14), behind a latency and a tilting chain, refuse an interval in which the pedal is
 * still moving (5 % rule of D43, on the centres of the window fits), and refuse what it cannot check.
 *
 * Exit codes: 0 all pass, 1 at least one check fails.
 */

#include "wah_measure.h"
#include <algorithm>
#include <cmath>
#include <limits>
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


// One-pole high-pass (bilinear) as a Biquad: the "DI box" of the test chain.
Biquad highpass1(double fc, double fs) {
    double k = std::tan(kPi * fc / fs);
    double b0 = 1.0 / (1.0 + k);
    return { b0, -b0, 0.0, (k - 1.0) / (k + 1.0), 0.0 };
}

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

    // 5. Latency of a chain: a pure delay behind a low-pass "interface" (about 5.5 kHz).
    //    The stimulus is white; the response is the stimulus, filtered, then delayed.
    {
        const std::vector<float> xf = to_float(white);
        std::vector<double> lp(kN);
        double s = 0;
        for (int i = 0; i < kN; i++) { s += 0.5 * (white[i] - s); lp[i] = s; }
        auto delayed = [&](const std::vector<double>& v, int d, double gain) {
            std::vector<float> o(kN, 0.0f);
            for (int i = d; i < kN; i++) o[i] = (float)(gain * v[i - d]);
            return o;
        };
        const int kMax = 4800;  // 100 ms
        const int delays[] = {0, 1, 137, 1500, kMax};
        bool all_found = true, all_valid = true;
        for (int d : delays) {
            std::vector<float> r = delayed(lp, d, 1.0);
            wah::DelayEstimate e = wah::estimate_delay(xf.data(), r.data(), kN, kMax);
            all_valid = all_valid && e.valid;
            all_found = all_found && e.valid && e.samples == d;
        }
        check(all_valid && all_found, "5a delay found exactly for 0, 1, 137, 1500 and 4800 (the limit) samples");

        std::vector<float> flat = delayed(white, 137, 1.0);
        wah::DelayEstimate ef = wah::estimate_delay(xf.data(), flat.data(), kN, kMax);
        check(ef.valid && ef.samples == 137 && ef.peak > 0.99f, "5b pure delay: peak correlation above 0.99");

        std::vector<float> inv = delayed(lp, 137, -1.0);
        wah::DelayEstimate ei = wah::estimate_delay(xf.data(), inv.data(), kN, kMax);
        check(ei.valid && ei.samples == 137, "5c inverted polarity: delay still found");

        std::vector<float> noisy = delayed(lp, 700, 1.0);
        Rng rn(987654321u);
        for (float& v : noisy) v += (float)(1.7 * rn.next());  // noise about as strong as the signal
        wah::DelayEstimate en = wah::estimate_delay(xf.data(), noisy.data(), kN, kMax);
        check(en.valid && en.samples == 700, "5d noise as strong as the signal: delay found");

        std::vector<float> far = delayed(lp, 6000, 1.0);  // beyond max_delay
        check(!wah::estimate_delay(xf.data(), far.data(), kN, kMax).valid,
              "5e delay beyond max_delay: refused, not a wrong lag");

        std::vector<double> other(kN);
        Rng ro(55555u);
        for (double& v : other) v = ro.next();
        std::vector<float> unrelated = to_float(other);
        check(!wah::estimate_delay(xf.data(), unrelated.data(), kN, kMax).valid,
              "5f unrelated signals: refused");

        // 6. Bad input must be refused.
        std::vector<float> zeros(kN, 0.0f);
        check(!wah::estimate_delay(nullptr, xf.data(), kN, kMax).valid, "6a null stimulus");
        check(!wah::estimate_delay(xf.data(), nullptr, kN, kMax).valid, "6b null response");
        check(!wah::estimate_delay(xf.data(), xf.data(), kN, -1).valid, "6c negative max_delay");
        check(!wah::estimate_delay(xf.data(), xf.data(), 1000, 10).valid, "6d fewer than 1024 samples");
        check(!wah::estimate_delay(xf.data(), xf.data(), 8000, 4800).valid, "6e shorter than 4 * max_delay");
        check(!wah::estimate_delay(zeros.data(), xf.data(), kN, kMax).valid, "6f silent stimulus");
        check(!wah::estimate_delay(xf.data(), zeros.data(), kN, kMax).valid, "6g silent response");
    }

    // 7. Calibrated measurement: the pedal (800 Hz, Q 5, +6 dB) behind a chain with a
    //    420-sample latency and a high-pass at 1.5 kHz (a 6 dB/octave tilt right across
    //    the pedal's range). The loopback is the chain alone.
    {
        const int kD = 420, kMax = 4800;
        const Biquad chain = highpass1(1500.0, kFs);
        Biquad f6 = f; f6.b0 *= 2; f6.b1 *= 2; f6.b2 *= 2;
        auto lag = [&](const std::vector<double>& v) {
            std::vector<float> o(kN, 0.0f);
            for (int i = kD; i < kN; i++) o[i] = (float)v[i - kD];
            return o;
        };
        const std::vector<float> xf = to_float(white);
        const std::vector<float> loop = lag(run(chain, white));
        const std::vector<float> dut = lag(run(chain, run(f6, white)));

        wah::TransferFunction tf = wah::measure_calibrated(xf.data(), dut.data(), loop.data(), kN,
                                                           (float)kFs, kMax);
        int n = 0;
        double err = max_error_db(tf, f6, 100, 5000, 0.9, n);
        check(tf.valid && n > 300 && err < 1.0, "7a calibrated magnitude within 1 dB of the pedal alone, 100 Hz to 5 kHz");
        wah::BandpassEstimate e = wah::estimate_bandpass(tf);
        check(e.valid && std::abs(e.center_freq - 800.0) < 16.0 && std::abs(e.q - 5.0) < 0.5 &&
                  std::abs(e.peak_gain_db - 6.02) < 1.0,
              "7b fit recovers centre within 2 %, Q within 10 %, gain within 1 dB");

        // The chain must matter, or the test proves nothing: trim by the latency, no division.
        wah::TransferFunction raw = wah::measure_transfer_function(xf.data(), dut.data() + kD, kN - kD, (float)kFs);
        wah::BandpassEstimate er = wah::estimate_bandpass(raw);
        check(!er.valid || std::abs(er.center_freq - 800.0) > 40.0,
              "7c without calibration the centre is more than 5 % off (the test chain is not trivial)");

        // A noisy loopback must lower the coherence, never raise it.
        std::vector<float> loop_n = loop;
        Rng rl(424242u);
        for (float& v : loop_n) v += (float)(0.3 * rl.next());
        wah::TransferFunction tn = wah::measure_calibrated(xf.data(), dut.data(), loop_n.data(), kN,
                                                           (float)kFs, kMax);
        wah::TransferFunction ln = wah::measure_transfer_function(xf.data(), loop_n.data() + kD, kN - kD, (float)kFs);
        bool never_above = tn.valid && tn.coherence.size() == ln.coherence.size();
        bool lowered = false;
        for (size_t i = 0; never_above && i < tn.coherence.size(); i++) {
            if (tn.coherence[i] > ln.coherence[i] + 1e-6f) never_above = false;
            if (tn.coherence[i] < tf.coherence[i] - 0.05f) lowered = true;
        }
        check(never_above && lowered, "7d noisy loopback: coherence never above the loopback's own, and lower than before");

        // A silent pedal is measured (valid) but carries no coherence anywhere.
        std::vector<float> silent(kN, 0.0f);
        wah::TransferFunction ts = wah::measure_calibrated(xf.data(), silent.data(), loop.data(), kN,
                                                           (float)kFs, kMax);
        double cmax = 0;
        for (float c : ts.coherence) if (c > cmax) cmax = c;
        check(ts.valid && ts.coherence.size() > 0 && cmax == 0.0, "7e silent pedal: valid, coherence 0 in every bin");

        // 8. Refusals.
        std::vector<float> other(kN);
        Rng ro(777u);
        for (float& v : other) v = (float)ro.next();
        check(!wah::measure_calibrated(nullptr, dut.data(), loop.data(), kN, (float)kFs, kMax).valid, "8a null stimulus");
        check(!wah::measure_calibrated(xf.data(), nullptr, loop.data(), kN, (float)kFs, kMax).valid, "8b null pedal response");
        check(!wah::measure_calibrated(xf.data(), dut.data(), nullptr, kN, (float)kFs, kMax).valid, "8c null loopback");
        check(!wah::measure_calibrated(xf.data(), dut.data(), other.data(), kN, (float)kFs, kMax).valid,
              "8d loopback unrelated to the stimulus: refused");
        check(!wah::measure_calibrated(xf.data(), dut.data(), loop.data(), kN, (float)kFs, -1).valid, "8e negative max_delay");
        check(!wah::measure_calibrated(xf.data(), dut.data(), loop.data(), 6000, (float)kFs, kMax).valid,
              "8f too short to measure after trimming");
    }

    // 9. The stimulus generator (D28, D29).
    {
        const float kRms = 0.1f;
        std::vector<float> a = wah::generate_stimulus(kN, 7u, kRms);
        std::vector<float> b = wah::generate_stimulus(kN, 7u, kRms);
        std::vector<float> c2 = wah::generate_stimulus(kN, 8u, kRms);
        check((int)a.size() == kN && a == b, "9a right length, and the same seed gives the identical array");
        double dot = 0, ea = 0, ec = 0;
        for (size_t i = 0; i < a.size() && i < c2.size(); i++) { dot += (double)a[i] * c2[i]; ea += (double)a[i] * a[i]; ec += (double)c2[i] * c2[i]; }
        check(c2.size() == a.size() && ea > 0 && ec > 0 && std::abs(dot) / std::sqrt(ea * ec) < 0.01,
              "9b another seed gives unrelated noise (correlation below 0.01)");
        double sum = 0, sq = 0, peak = 0;
        for (float v : a) { sum += v; sq += (double)v * v; if (std::abs(v) > peak) peak = std::abs(v); }
        const double mean = a.empty() ? 1.0 : sum / a.size();
        const double rms = a.empty() ? 0.0 : std::sqrt(sq / a.size());
        check(std::abs(rms - kRms) < 1e-4, "9c RMS is the one asked for, within 0.1 %");
        check(std::abs(mean) < 1e-6, "9d mean is zero");
        check(peak > 1.2 * kRms && peak <= 1.8 * kRms,
              "9e peak between 1.2 and 1.8 times the RMS (bounded, so it cannot clip)");

        // Flat spectrum: Hann-windowed periodograms of 4096 samples, 50 % overlap, evaluated at
        // eight frequencies from 100 Hz to 10 kHz; each must be within 2 dB of the average.
        const int S = 4096, hop = S / 2;
        const int K = (kN - S) / hop + 1;
        const double fq[8] = {100, 200, 400, 800, 1600, 3200, 6400, 10000};
        double p[8] = {0}, pm = 0;
        for (int j = 0; j < 8 && (int)a.size() == kN; j++) {
            const int bin = (int)std::lround(fq[j] * S / kFs);
            for (int k = 0; k < K; k++) {
                std::complex<double> acc(0, 0);
                for (int i = 0; i < S; i++) {
                    const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * i / S));
                    acc += a[k * hop + i] * w * std::polar(1.0, -2.0 * kPi * bin * i / S);
                }
                p[j] += std::norm(acc) / K;
            }
            pm += p[j] / 8;
        }
        bool flat = pm > 0;
        for (int j = 0; j < 8; j++) if (!(p[j] > 0) || std::abs(10.0 * std::log10(p[j] / pm)) > 2.0) flat = false;
        check(flat, "9f spectrum flat within 2 dB at 100 Hz, 200 Hz ... 10 kHz");

        // Refusals.
        check(wah::generate_stimulus(0, 1u, kRms).empty() && wah::generate_stimulus(-5, 1u, kRms).empty(), "9g zero or negative length refused");
        check(wah::generate_stimulus(1000, 0u, kRms).empty(), "9h seed 0 refused");
        check(wah::generate_stimulus(1000, 1u, 0.0f).empty() && wah::generate_stimulus(1000, 1u, -0.1f).empty() &&
                  wah::generate_stimulus(1000, 1u, 0.6f).empty(), "9i RMS of 0, negative or above 0.5 refused");

        // The generated stimulus must carry a whole calibrated measurement: pedal 800 Hz, Q 5,
        // +6 dB behind a 420-sample latency and a 1.5 kHz high-pass, as in 7.
        const int kD = 420;
        const Biquad chain = highpass1(1500.0, kFs);
        Biquad f6 = f; f6.b0 *= 2; f6.b1 *= 2; f6.b2 *= 2;
        std::vector<double> x(a.begin(), a.end());
        auto lag = [&](const std::vector<double>& v) {
            std::vector<float> o(kN, 0.0f);
            for (int i = kD; i < kN && i - kD < (int)v.size(); i++) o[i] = (float)v[i - kD];
            return o;
        };
        const std::vector<float> loop = lag(run(chain, x));
        const std::vector<float> dut = lag(run(chain, run(f6, x)));
        wah::TransferFunction tf = wah::measure_calibrated(a.data(), dut.data(), loop.data(), kN, (float)kFs, 4800);
        wah::BandpassEstimate e = wah::estimate_bandpass(tf);
        check(e.valid && std::abs(e.center_freq - 800.0) < 16.0 && std::abs(e.q - 5.0) < 0.5 && std::abs(e.peak_gain_db - 6.02) < 1.0,
              "9j generated stimulus through a latency and a chain: centre, Q and gain recovered");
    }

    // 10. Tracking a moving pedal. Q 5, +6 dB; the centre moves on a straight line in log
    //     frequency: 400 Hz up to 1600 Hz in 1 s, back down in 1 s, then holds at 400 Hz.
    //     Chain: 420-sample latency and a 1.5 kHz high-pass, as in 7 and 9.
    {
        const int kD = 420, kMax = 4800;
        const Biquad chain = highpass1(1500.0, kFs);
        const std::vector<float> xf = wah::generate_stimulus(kN, 7u, 0.1f);
        std::vector<double> x(xf.begin(), xf.end());
        auto centre_at = [](double t, bool moving) {
            if (!moving) return 400.0;
            const double r = t < 1.0 ? t : (t < 2.0 ? 2.0 - t : 0.0);
            return 400.0 * std::pow(4.0, r);
        };
        auto pedal = [&](bool moving) {  // time-varying RBJ band-pass, peak gain 2 (+6 dB)
            std::vector<double> y(kN);
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (int i = 0; i < kN; i++) {
                const double w0 = 2.0 * kPi * centre_at(i / kFs, moving) / kFs;
                const double al = std::sin(w0) / 10.0, a0 = 1.0 + al;
                const double v = 2.0 * (al / a0) * (x[i] - x2) - (-2.0 * std::cos(w0) / a0) * y1 - ((1.0 - al) / a0) * y2;
                x2 = x1; x1 = x[i]; y2 = y1; y1 = v; y[i] = v;
            }
            return y;
        };
        auto lag = [&](const std::vector<double>& v) {
            std::vector<float> o(kN, 0.0f);
            for (int i = kD; i < kN; i++) o[i] = (float)v[i - kD];
            return o;
        };
        const std::vector<float> loop = lag(run(chain, x));
        const std::vector<float> dut_m = lag(run(chain, pedal(true)));
        const std::vector<float> dut_s = lag(run(chain, pedal(false)));

        std::vector<wah::TrackPoint> tr = wah::track_bandpass(xf.data(), dut_m.data(), loop.data(), kN, (float)kFs, kMax);
        const int W = 12000, hop = 6000, n = kN - kD;
        const int expect = (n - W) / hop + 1;
        bool times_ok = (int)tr.size() == expect;
        for (size_t i = 0; times_ok && i < tr.size(); i++)
            if (std::abs(tr[i].time_s - (i * hop + W / 2) / kFs) > 1e-4) times_ok = false;
        check(times_ok, "10a one point per half-window, at the centre time of each window (0.125 s apart)");

        int compared = 0, valid = 0;
        double worst = 0;
        for (const wah::TrackPoint& p : tr) {
            const double t0 = p.time_s - 0.125, t1 = p.time_s + 0.125;
            if ((t0 < 1.0 && t1 > 1.0) || (t0 < 2.0 && t1 > 2.0)) continue;  // contains a turnaround
            compared++;
            if (!p.estimate.valid) continue;
            valid++;
            const double ft = centre_at(p.time_s, true);
            worst = std::max(worst, std::abs(p.estimate.center_freq - ft) / ft);
        }
        check(compared >= 15 && valid == compared, "10b every window away from a turnaround gives a valid estimate");
        check(compared >= 15 && valid == compared && worst < 0.05, "10c centre within 5 % of the true centre in those windows, at 1 s heel-to-toe");

        // A static pedal with a longer window: Q and gain become precise.
        std::vector<wah::TrackPoint> ts = wah::track_bandpass(xf.data(), dut_s.data(), loop.data(), kN, (float)kFs, kMax,
                                                              0.5f, 4096);
        bool all_ok = ts.size() >= 8;
        for (const wah::TrackPoint& p : ts)
            if (!(p.estimate.valid && std::abs(p.estimate.center_freq - 400.0) < 8.0 && std::abs(p.estimate.q - 5.0) < 0.5 &&
                  std::abs(p.estimate.peak_gain_db - 6.02) < 1.0))
                all_ok = false;
        check(all_ok, "10d static pedal, window 0.5 s, segment 4096: centre within 2 %, Q within 10 %, gain within 1 dB");

        // A cable instead of the pedal is flat after calibration: not a band-pass.
        std::vector<wah::TrackPoint> tc = wah::track_bandpass(xf.data(), loop.data(), loop.data(), kN, (float)kFs, kMax);
        bool none_valid = !tc.empty();
        for (const wah::TrackPoint& p : tc) if (p.estimate.valid) none_valid = false;
        check(none_valid, "10e no pedal (a cable): every window refused");

        // A latency longer than a segment (3000 samples, 62 ms) must be removed from both recordings.
        {
            auto lag_by = [&](const std::vector<double>& v, int d) {
                std::vector<float> o(kN, 0.0f);
                for (int i = d; i < kN; i++) o[i] = (float)v[i - d];
                return o;
            };
            const std::vector<float> loop_l = lag_by(run(chain, x), 3000);
            const std::vector<float> dut_l = lag_by(run(chain, pedal(true)), 3000);
            std::vector<wah::TrackPoint> tl3 = wah::track_bandpass(xf.data(), dut_l.data(), loop_l.data(), kN, (float)kFs, kMax);
            int cmp = 0, ok = 0;
            for (const wah::TrackPoint& p : tl3) {
                const double t0 = p.time_s - 0.125, t1 = p.time_s + 0.125;
                if ((t0 < 1.0 && t1 > 1.0) || (t0 < 2.0 && t1 > 2.0)) continue;
                cmp++;
                const double ft = centre_at(p.time_s, true);
                if (p.estimate.valid && std::abs(p.estimate.center_freq - ft) / ft < 0.05) ok++;
            }
            check(cmp >= 15 && ok == cmp, "10h latency of 3000 samples (longer than a segment): centre still within 5 %");
        }

        // The frequency range and the coherence floor reach estimate_bandpass().
        auto all_refused = [&](float fmin, float fmax, float coh) {
            std::vector<wah::TrackPoint> v = wah::track_bandpass(xf.data(), dut_m.data(), loop.data(), kN, (float)kFs, kMax,
                                                                 0.25f, 2048, fmin, fmax, coh);
            bool none = !v.empty();
            for (const wah::TrackPoint& p : v) if (p.estimate.valid) none = false;
            return none;
        };
        check(all_refused(20.0f, 5000.0f, 1.01f), "10f coherence floor above 1: every window refused");
        check(all_refused(25.0f, 30.0f, 0.5f), "10g frequency range with fewer than 20 bins: every window refused");

        // Refusals.
        std::vector<float> other(kN);
        Rng ro(31337u);
        for (float& v : other) v = (float)ro.next();
        check(wah::track_bandpass(nullptr, dut_m.data(), loop.data(), kN, (float)kFs, kMax).empty(), "11a null stimulus");
        check(wah::track_bandpass(xf.data(), nullptr, loop.data(), kN, (float)kFs, kMax).empty(), "11b null pedal response");
        check(wah::track_bandpass(xf.data(), dut_m.data(), nullptr, kN, (float)kFs, kMax).empty(), "11c null loopback");
        check(wah::track_bandpass(xf.data(), dut_m.data(), other.data(), kN, (float)kFs, kMax).empty(), "11d loopback unrelated: refused");
        check(wah::track_bandpass(xf.data(), dut_m.data(), loop.data(), kN, (float)kFs, kMax, 0.0f).empty(), "11e window of 0 s");
        check(wah::track_bandpass(xf.data(), dut_m.data(), loop.data(), kN, (float)kFs, kMax, 0.05f).empty(),
              "11f window shorter than 4 segments");
        check(wah::track_bandpass(xf.data(), dut_m.data(), loop.data(), kN, (float)kFs, kMax, 0.25f, 1000).empty(),
              "11g segment size not a power of two");
        check(wah::track_bandpass(xf.data(), dut_m.data(), loop.data(), kN, (float)kFs, kMax, 5.0f).empty(),
              "11h window longer than the recording");
    }

    // 12. Aggregating passes (D17, D33, D34). Truth: centre(p) = 400 * 4^(p^1.5), a curve that is
    //     not a straight line in log frequency. A pass is a breathing movement: 6 windows held at
    //     the heel, 8 rising, 6 held at the toe, 8 falling; each centre carries 2 % of noise.
    {
        auto truth = [](double p) { return 400.0 * std::pow(4.0, std::pow(p, 1.5)); };
        // scale(p): factor applied to the centre at position p; windows with p in [from, to] only.
        auto make_pass = [&](int k, double from, double to, auto scale, float weight, double noise = 0.02) {
            Rng rg(1000u + 17u * k);
            std::vector<wah::PassPoint> v;
            auto add = [&](double p) {
                if (p < from || p > to) return;
                const double c = truth(p) * scale(p) * (1.0 + noise * rg.next());
                wah::PassPoint pp; pp.position = (float)p; pp.center_freq = (float)c; pp.weight = weight;
                v.push_back(pp);
            };
            for (int i = 0; i < 6; i++) add(0.0);
            for (int i = 0; i < 8; i++) add(std::min(0.999, std::max(0.001, (i + 1) / 9.0 + 0.01 * (k - 3))));
            for (int i = 0; i < 6; i++) add(1.0);
            for (int i = 7; i >= 0; i--) add(std::min(0.999, std::max(0.001, (i + 1) / 9.0 + 0.01 * (k - 3))));
            return v;
        };
        auto one = [](double) { return 1.0; };
        auto worst_error = [&](const wah::AggregatedCurve& a) {
            double w = 0;
            if (!a.valid || a.center_freq.size() != 11) return 1e9;
            for (int i = 0; i < 11; i++) {
                if (a.support[i] < 1) return 1e9;
                w = std::max(w, std::abs(a.center_freq[i] - truth(i / 10.0)) / truth(i / 10.0));
            }
            return w;
        };
        auto has = [](const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); };

        std::vector<std::vector<wah::PassPoint>> six;
        for (int k = 0; k < 6; k++) six.push_back(make_pass(k, 0.0, 1.0, one, 0.9f));

        wah::AggregatedCurve a = wah::aggregate_passes(six);
        bool pos_ok = a.position.size() == 11 && std::abs(a.position[3] - 0.3f) < 1e-6f && a.position[10] == 1.0f;
        check(a.valid && pos_ok && a.dropped.empty() && worst_error(a) < 0.03,
              "12a six clean passes: 11 positions, none dropped, curve within 3 % of the truth");
        bool supported = a.valid;
        for (int s : a.support) if (s < 6) supported = false;
        check(supported, "12b every position is supported by all six passes");

        std::vector<std::vector<wah::PassPoint>> wrong = six;
        wrong[2] = make_pass(2, 0.0, 1.0, [](double) { return 1.4; }, 0.9f);
        a = wah::aggregate_passes(wrong);
        check(a.valid && a.dropped.size() == 1 && has(a.dropped, 2) && worst_error(a) < 0.03,
              "12c one pass 40 % off everywhere: dropped, curve still within 3 %");

        std::vector<std::vector<wah::PassPoint>> late = six;
        late[4] = make_pass(4, 0.0, 1.0, [](double p) { return p > 0.5 ? 1.4 : 1.0; }, 0.9f);
        a = wah::aggregate_passes(late);
        check(a.valid && has(a.dropped, 4) && worst_error(a) < 0.03, "12d wrong over the second half (6 positions of 11): dropped");

        std::vector<std::vector<wah::PassPoint>> brief = six;
        brief[1].clear();  // points exactly on the 11 positions, two of them 40 % off (no interpolation involved)
        for (int i = 0; i <= 10; i++) {
            wah::PassPoint pp;
            pp.position = (float)(i / 10.0);
            pp.center_freq = (float)(truth(i / 10.0) * ((i == 4 || i == 5) ? 1.4 : 1.0));
            pp.weight = 0.9f;
            brief[1].push_back(pp);
        }
        a = wah::aggregate_passes(brief);
        check(a.valid && a.dropped.empty() && worst_error(a) < 0.03,
              "12e wrong at only 2 positions of 11: kept, and outvoted by the median");
        brief[1][6].center_freq *= 1.4f;  // a third wrong position: more than a quarter
        a = wah::aggregate_passes(brief);
        check(a.valid && a.dropped.size() == 1 && has(a.dropped, 1) && worst_error(a) < 0.03,
              "12k wrong at 3 positions of 11 (more than a quarter): dropped");

        std::vector<std::vector<wah::PassPoint>> holes = six;
        holes[0] = make_pass(0, 0.0, 0.25, one, 0.9f);    // gives up after a quarter
        holes[3] = make_pass(3, 0.0, 1.0, one, 0.9f);
        holes[3].erase(std::remove_if(holes[3].begin(), holes[3].end(),
                                      [](const wah::PassPoint& p) { return p.position > 0.3f && p.position < 0.7f; }),
                       holes[3].end());  // a gap in the middle, wider than 0.25
        a = wah::aggregate_passes(holes);
        check(a.valid && a.dropped.empty() && worst_error(a) < 0.03 && a.support[5] == 4 && a.support[1] == 6 && a.support[9] == 5,
              "12f dropouts: an early stop and a gap in the middle leave the curve right, with less support");

        std::vector<std::vector<wah::PassPoint>> two;
        two.push_back(make_pass(0, 0.0, 1.0, one, 0.9f));
        two.push_back(make_pass(1, 0.0, 1.0, [](double) { return 0.9; }, 0.1f));
        a = wah::aggregate_passes(two);
        check(a.valid && a.dropped.empty() && worst_error(a) < 0.03,
              "12g two passes, one 10 % too low with a ninth of the weight: the weighted median follows the heavy one");

        std::vector<std::vector<wah::PassPoint>> split;
        split.push_back(make_pass(0, 0.0, 1.0, one, 0.9f));
        split.push_back(make_pass(1, 0.0, 1.0, [](double) { return 1.4; }, 0.9f));
        a = wah::aggregate_passes(split);
        check(a.valid && a.dropped.empty(), "12l two passes that disagree: nothing is dropped (no consensus to measure against)");

        std::vector<std::vector<wah::PassPoint>> glitch = six;
        glitch[5][10].center_freq *= 3.0f;  // one window reads three times too high
        a = wah::aggregate_passes(glitch);
        check(a.valid && worst_error(a) < 0.03, "12h one glitch window in one pass: the curve is not moved");

        std::vector<wah::PassPoint> junk(6);
        junk[0].position = -0.2f; junk[0].center_freq = 800.0f; junk[0].weight = 1.0f;
        junk[1].position = 1.5f;  junk[1].center_freq = 800.0f; junk[1].weight = 1.0f;
        junk[2].position = 0.5f;  junk[2].center_freq = 0.0f;   junk[2].weight = 1.0f;
        junk[3].position = 0.5f;  junk[3].center_freq = 800.0f; junk[3].weight = 0.0f;
        junk[4].position = NAN;   junk[4].center_freq = 800.0f; junk[4].weight = 1.0f;
        junk[5].position = 0.5f;  junk[5].center_freq = NAN;    junk[5].weight = 1.0f;
        std::vector<std::vector<wah::PassPoint>> with_junk = six;
        with_junk.push_back(junk);
        a = wah::aggregate_passes(with_junk);
        check(a.valid && a.dropped.size() == 1 && has(a.dropped, 6) && worst_error(a) < 0.03,
              "12i a pass of unusable points is dropped; invalid points are ignored");

        std::vector<std::vector<wah::PassPoint>> single(1, six[0]);
        check(wah::aggregate_passes(single).valid && worst_error(wah::aggregate_passes(single)) < 0.03,
              "12j a single pass gives its own curve");
        check(!wah::aggregate_passes(std::vector<std::vector<wah::PassPoint>>()).valid, "13a no passes");
        check(!wah::aggregate_passes(six, 1).valid && !wah::aggregate_passes(six, 0).valid, "13b fewer than 2 positions");
        std::vector<std::vector<wah::PassPoint>> empties(3);
        check(!wah::aggregate_passes(empties).valid, "13c only empty passes");
        wah::AggregatedCurve a21 = wah::aggregate_passes(six, 21);
        check(a21.valid && a21.position.size() == 21 && a21.center_freq.size() == 21 && a21.support.size() == 21 &&
                  std::abs(a21.center_freq[10] - truth(0.5)) / truth(0.5) < 0.03,
              "13d another number of positions (21) works");

        // 14. Confidence and stopping (D35 to D38).
        const float kInf = std::numeric_limits<float>::infinity();
        auto grid_pass = [&](double offset) {  // points exactly on the 11 positions, no noise, no interpolation
            std::vector<wah::PassPoint> v;
            for (int i = 0; i <= 10; i++) {
                wah::PassPoint pp;
                pp.position = (float)(i / 10.0); pp.center_freq = (float)(truth(i / 10.0) * (1.0 + offset)); pp.weight = 0.9f;
                v.push_back(pp);
            }
            return v;
        };
        auto noisy_set = [&](int n, double noise) {
            std::vector<std::vector<wah::PassPoint>> v;
            for (int k = 0; k < n; k++) v.push_back(make_pass(k, 0.0, 1.0, one, 0.9f, noise));
            return v;
        };

        // Five passes at -4, -2, 0, +2, +4 %: the median is the middle one, the deviations in log
        // frequency are 0.0408, 0.0202, 0, 0.0198, 0.0392, so MAD = 0.020203 (the third) and
        // the error is 1.2533 * 1.4826 * 0.020203 / sqrt(5) = 0.016788 at every position.
        std::vector<std::vector<wah::PassPoint>> five;
        for (double d : {-0.04, -0.02, 0.0, 0.02, 0.04}) five.push_back(grid_pass(d));
        a = wah::aggregate_passes(five);
        bool exact = a.valid && a.std_error.size() == 11;
        for (size_t i = 0; exact && i < a.std_error.size(); i++) if (std::abs(a.std_error[i] - 0.016788) > 1e-4) exact = false;
        check(exact && a.dropped.empty(), "14a error of five passes at -4..+4 % is 0.01679 at every position (the D35 formula)");

        std::vector<std::vector<wah::PassPoint>> few(2);
        few[0] = grid_pass(0.0); few[1] = grid_pass(0.01);
        few[0].erase(few[0].begin() + 4, few[0].end());  // nobody at all beyond position 0.3
        few[1].erase(few[1].begin() + 4, few[1].end());
        a = wah::aggregate_passes(few);
        bool inf_all = a.valid && a.std_error.size() == 11;
        for (size_t i = 0; inf_all && i < a.std_error.size(); i++) if (a.std_error[i] != kInf) inf_all = false;
        check(inf_all, "14b two passes, and positions nobody measured: the error is infinite (D38)");

        // More passes, less error: the same noise (4 %), 4 then 8 then 16 passes.
        float e4 = wah::capture_progress(noisy_set(4, 0.04), wah::CaptureTarget::Normal).confidence;
        float e8 = wah::capture_progress(noisy_set(8, 0.04), wah::CaptureTarget::Normal).confidence;
        float e16 = wah::capture_progress(noisy_set(16, 0.04), wah::CaptureTarget::Normal).confidence;
        check(e4 < kInf && e4 > e8 && e8 > e16 && e16 > 0.0f, "14c the confidence improves as passes are added (4, 8, 16 passes)");

        // The same passes with more noise: the error follows the noise (about four times as large).
        float lo = wah::capture_progress(noisy_set(8, 0.02), wah::CaptureTarget::Normal).confidence;
        float hi = wah::capture_progress(noisy_set(8, 0.08), wah::CaptureTarget::Normal).confidence;
        check(lo > 0.0f && hi / lo > 2.5f && hi / lo < 6.0f, "14d four times the noise gives about four times the error");

        // Bad passes do not count: six clean passes, then a wrong pass and a pass of junk.
        std::vector<std::vector<wah::PassPoint>> clean = noisy_set(6, 0.04), dirty = clean;
        dirty.push_back(make_pass(6, 0.0, 1.0, [](double) { return 1.4; }, 0.9f, 0.04));
        dirty.push_back(junk);
        wah::CaptureProgress pc = wah::capture_progress(clean, wah::CaptureTarget::Normal);
        wah::CaptureProgress pd = wah::capture_progress(dirty, wah::CaptureTarget::Normal);
        check(pc.valid && pd.valid && pd.passes_recorded == 8 && pd.passes_kept == 6 && pd.passes_dropped == 2 &&
                  pd.confidence == pc.confidence,
              "14e a wrong pass and a pass of junk are recorded and dropped, and do not change the confidence");

        // States.
        wah::CaptureProgress s3 = wah::capture_progress(noisy_set(3, 0.04), wah::CaptureTarget::Normal);
        check(s3.valid && s3.state == wah::CaptureState::NeedMorePasses && !s3.stop && s3.passes_kept == 3,
              "14f three passes: need more passes, do not stop");
        wah::CaptureProgress sd = wah::capture_progress(noisy_set(8, 0.04), wah::CaptureTarget::Normal);
        check(sd.state == wah::CaptureState::Done && sd.stop && sd.confidence <= 0.03f && std::abs(sd.target - 0.03f) < 1e-6f,
              "14g eight passes at 4 % noise reach the normal target (3 %): done");
        wah::CaptureProgress sn = wah::capture_progress(noisy_set(6, 0.15), wah::CaptureTarget::Normal);
        check(sn.state == wah::CaptureState::ConfidenceNotReached && !sn.stop && sn.passes_kept == 6 && sn.confidence > 0.03f,
              "14h six passes at 15 % noise do not reach 3 %: keep going");
        wah::CaptureProgress sf = wah::capture_progress(noisy_set(6, 0.15), wah::CaptureTarget::Fast);
        check(sf.state == wah::CaptureState::Done && std::abs(sf.target - 0.06f) < 1e-6f && sf.confidence == sn.confidence,
              "14i the same six passes reach the fast target (6 %) but not the normal one");
        wah::CaptureProgress sm = wah::capture_progress(noisy_set(20, 0.15), wah::CaptureTarget::Maximum);
        check(sm.state == wah::CaptureState::MaximumReached && sm.stop && sm.passes_recorded == 20 &&
                  std::abs(sm.target - 0.015f) < 1e-6f && sm.confidence > 0.015f,
              "14j twenty passes at 15 % noise do not reach 1.5 %: stop at the maximum");
        std::vector<std::vector<wah::PassPoint>> twenty = noisy_set(18, 0.15);
        twenty.push_back(make_pass(18, 0.0, 1.0, [](double) { return 1.4; }, 0.9f, 0.15));
        twenty.push_back(junk);
        wah::CaptureProgress s20 = wah::capture_progress(twenty, wah::CaptureTarget::Maximum);
        check(s20.state == wah::CaptureState::MaximumReached && s20.stop && s20.passes_recorded == 20 && s20.passes_kept == 18 &&
                  s20.passes_dropped == 2,
              "14l the 20 passes of the maximum are the ones recorded, dropped ones included");
        check(!wah::capture_progress(std::vector<std::vector<wah::PassPoint>>(), wah::CaptureTarget::Normal).valid &&
                  !wah::capture_progress(six, wah::CaptureTarget::Normal, 1).valid,
              "14k no passes, or fewer than 2 positions: refused");

        // 18. The holds in the stopping rule (D48 to D51). Passes: the curve, as in 14. Holds: eight per
        //     end, centre 400 and 1600 Hz, Q 5, +6.02 dB, with a noise that is set per quantity.
        // Six holds per end. The gain offsets are fixed, so the error is known: at scale s the weighted MAD is
        // 0.8 s dB and the error 1.2533 * 1.4826 * 0.8 s / sqrt(6) = 0.607 s dB. The Q carries random noise.
        auto holds_set = [&](double f0, double qn, double gain_scale, uint32_t seed) {
            Rng rg(seed);
            const double off[6] = {-1.2, -0.8, -0.4, 0.4, 0.8, 1.2};
            std::vector<wah::HoldEstimate> v;
            for (int i = 0; i < 6; i++) {
                wah::HoldEstimate h;
                h.center_freq = (float)(f0 * (1.0 + 0.005 * rg.next()));
                h.q = (float)(5.0 * (1.0 + qn * rg.next()));
                h.peak_gain_db = (float)(6.02 + gain_scale * off[i]);
                h.windows_used = 4; h.valid = true;
                v.push_back(h);
            }
            return v;
        };
        using Lim = wah::CaptureLimit;
        const auto N = wah::CaptureTarget::Normal;
        auto prog = [&](int passes_n, double pass_noise, double hq, double hg, double tq, double tg, wah::CaptureTarget tgt) {
            return wah::capture_progress(noisy_set(passes_n, pass_noise), holds_set(400.0, hq, hg, 11u),
                                         holds_set(1600.0, tq, tg, 22u), tgt);
        };

        wah::CaptureProgress g0 = prog(8, 0.04, 0.03, 0.1, 0.03, 0.1, N);
        check(g0.valid && g0.state == wah::CaptureState::Done && g0.stop && g0.worst_ratio <= 1.0f && std::abs(g0.gain_target_db - 0.25f) < 1e-6f,
              "18a good passes and good holds: done, every quantity at or below its target");
        check(g0.heel_q_error > 0.0f && g0.heel_q_error < kInf && g0.heel_gain_error_db > 0.0f && g0.heel_gain_error_db < kInf &&
                  g0.toe_q_error > 0.0f && g0.toe_q_error < kInf && g0.toe_gain_error_db > 0.0f && g0.toe_gain_error_db < kInf,
              "18b the errors of the holds are reported");

        wah::CaptureProgress cv = prog(8, 0.15, 0.03, 0.1, 0.03, 0.1, N);
        wah::CaptureProgress hq = prog(8, 0.04, 0.20, 0.1, 0.03, 0.1, N);
        wah::CaptureProgress hg = prog(8, 0.04, 0.03, 1.0, 0.03, 0.1, N);
        wah::CaptureProgress tq = prog(8, 0.04, 0.03, 0.1, 0.20, 0.1, N);
        wah::CaptureProgress tg = prog(8, 0.04, 0.03, 0.1, 0.03, 1.0, N);
        auto waiting = [](const wah::CaptureProgress& p, Lim l) {
            return p.valid && p.state == wah::CaptureState::ConfidenceNotReached && !p.stop && p.worst_ratio > 1.0f && p.limiting == l;
        };
        check(waiting(cv, Lim::Curve), "18c only the curve is noisy: keep going, limited by the curve");
        check(waiting(hq, Lim::HeelQ), "18d only the heel Q is noisy: limited by the heel Q");
        check(waiting(hg, Lim::HeelGain), "18e only the heel gain is noisy: limited by the heel gain");
        check(waiting(tq, Lim::ToeQ), "18f only the toe Q is noisy: limited by the toe Q");
        check(waiting(tg, Lim::ToeGain), "18g only the toe gain is noisy: limited by the toe gain");

        // The gain target depends on the CaptureTarget: a gain error of 0.364 dB passes the fast target (0.5) and fails the normal one (0.25).
        wah::CaptureProgress hgn = prog(8, 0.04, 0.03, 0.6, 0.03, 0.1, N);
        wah::CaptureProgress hgf = prog(8, 0.04, 0.03, 0.6, 0.03, 0.1, wah::CaptureTarget::Fast);
        check(std::abs(hgn.heel_gain_error_db - 0.364f) < 0.01f && hgn.state == wah::CaptureState::ConfidenceNotReached && hgn.limiting == Lim::HeelGain &&
                  hgf.state == wah::CaptureState::Done && std::abs(hgf.gain_target_db - 0.5f) < 1e-6f,
              "18h a heel gain error of 0.364 dB: done at the fast target, not at the normal one");
        wah::CaptureProgress g1 = prog(8, 0.04, 0.03, 0.1, 0.03, 0.1, wah::CaptureTarget::Maximum);
        check(std::abs(g1.gain_target_db - 0.1f) < 1e-6f && std::abs(g1.target - 0.015f) < 1e-6f, "18i the maximum target is 1.5 % and 0.1 dB");

        // No holds yet: the curve may be sure, the capture is not.
        wah::CaptureProgress nh = wah::capture_progress(noisy_set(8, 0.04), std::vector<wah::HoldEstimate>(), std::vector<wah::HoldEstimate>(), N);
        check(nh.state == wah::CaptureState::ConfidenceNotReached && !nh.stop && nh.limiting == Lim::HeelQ && nh.worst_ratio == kInf &&
                  nh.confidence <= 0.03f && wah::capture_progress(noisy_set(8, 0.04), N).state == wah::CaptureState::Done,
              "18j curve sure but no holds: not done (limited by the heel Q, the first infinite); the form without holds still says done");

        // The minimum of 4 kept passes and the maximum of 20 recorded still apply.
        wah::CaptureProgress few_p = wah::capture_progress(noisy_set(3, 0.04), holds_set(400.0, 0.03, 0.1, 11u), holds_set(1600.0, 0.03, 0.1, 22u), N);
        check(few_p.state == wah::CaptureState::NeedMorePasses && !few_p.stop, "18k three passes, perfect holds: still need more passes (D50)");
        wah::CaptureProgress mx = wah::capture_progress(noisy_set(20, 0.04), holds_set(400.0, 0.20, 0.1, 11u), holds_set(1600.0, 0.03, 0.1, 22u),
                                                        wah::CaptureTarget::Maximum);
        check(mx.state == wah::CaptureState::MaximumReached && mx.stop && mx.passes_recorded == 20 && mx.limiting == Lim::HeelQ,
              "18l twenty passes without reaching the maximum target: stop, and say what was lacking");
        check(!wah::capture_progress(std::vector<std::vector<wah::PassPoint>>(), holds_set(400.0, 0.03, 0.1, 11u), holds_set(1600.0, 0.03, 0.1, 22u), N).valid &&
                  !wah::capture_progress(noisy_set(8, 0.04), holds_set(400.0, 0.03, 0.1, 11u), holds_set(1600.0, 0.03, 0.1, 22u), N, 1).valid,
              "18m no passes, or fewer than 2 positions: refused");
    }

    // 15. Holds (D39 to D43). A 5.46 s recording; Q 5, +6 dB; the centre holds at 400 Hz until 1.5 s,
    //     rises on a straight line in log frequency to 1600 Hz by 2.5 s, holds until 4 s, falls
    //     back by 5 s. Chain: a 3000-sample latency (longer than a segment) and a 1.5 kHz high-pass.
    {
        const int kM = 1 << 18, kD = 3000, kMax = 4800;
        const Biquad chain = highpass1(1500.0, kFs);
        const std::vector<float> xf = wah::generate_stimulus(kM, 7u, 0.1f);
        std::vector<double> x(xf.begin(), xf.end());
        auto centre_at = [](double t) {
            double r = 0.0;
            if (t >= 1.5 && t < 2.5) r = t - 1.5;
            else if (t >= 2.5 && t < 4.0) r = 1.0;
            else if (t >= 4.0 && t < 5.0) r = 5.0 - t;
            return 400.0 * std::pow(4.0, r);
        };
        std::vector<double> y(kM);
        {
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (int i = 0; i < kM; i++) {
                const double w0 = 2.0 * kPi * centre_at(i / kFs) / kFs;
                const double al = std::sin(w0) / 10.0, a0 = 1.0 + al;
                const double v = 2.0 * (al / a0) * (x[i] - x2) - (-2.0 * std::cos(w0) / a0) * y1 - ((1.0 - al) / a0) * y2;
                x2 = x1; x1 = x[i]; y2 = y1; y1 = v; y[i] = v;
            }
        }
        auto lag = [&](const std::vector<double>& v) {
            std::vector<float> o(kM, 0.0f);
            for (int i = kD; i < kM; i++) o[i] = (float)v[i - kD];
            return o;
        };
        const std::vector<float> loop = lag(run(chain, x));
        const std::vector<float> dut = lag(run(chain, y));
        auto hold = [&](const std::vector<float>& d, double t0, double t1, float settle = 0.2f) {
            return wah::estimate_hold(xf.data(), d.data(), loop.data(), kM, (float)kFs, kMax, (float)t0, (float)t1,
                                      0.5f, 4096, settle);
        };
        auto good = [](const wah::HoldEstimate& e, double f0) {
            return e.valid && std::abs(e.center_freq - f0) / f0 < 0.02 && std::abs(e.q - 5.0) < 0.5 &&
                   std::abs(e.peak_gain_db - 6.02) < 1.0;
        };

        wah::HoldEstimate heel = hold(dut, 0.0, 1.5), toe = hold(dut, 2.5, 4.0);
        check(good(heel, 400.0) && heel.windows_used >= 3, "15a heel hold: centre within 2 %, Q within 10 %, gain within 1 dB");
        check(good(toe, 1600.0) && toe.windows_used >= 3, "15b toe hold: the same at 1600 Hz");

        check(!hold(dut, 1.5, 2.5).valid, "15c an interval in which the pedal moves (the rise): refused");
        check(!hold(dut, 0.6, 2.0).valid, "15d a heel hold that overshoots into the movement: refused");
        wah::HoldEstimate one_s = hold(dut, 0.0, 1.0);
        check(good(one_s, 400.0) && one_s.windows_used == 2, "15e a hold of 1 s (the minimum of D32) gives 2 windows and is read right");
        check(!hold(dut, 0.0, 0.9).valid, "15f a hold of 0.9 s gives only 1 window: refused");
        check(good(hold(dut, 2.2, 4.0), 1600.0) && !hold(dut, 2.2, 4.0, 0.0f).valid,
              "15g settling matters: a hold that starts 0.3 s before the pedal stops is read right with 0.2 s of settling and refused with none");

        std::vector<float> glitchy = dut;
        Rng rg(2468u);
        for (int i = (int)(0.3 * kFs); i < (int)(0.4 * kFs); i++) glitchy[i] += (float)(0.5 * rg.next());
        wah::HoldEstimate hg = hold(glitchy, 0.0, 1.5);
        check(good(hg, 400.0) && hg.windows_used == 3, "15h a noise burst spoils one of the 4 windows: it is left out and the result is right");

        wah::HoldEstimate cable = wah::estimate_hold(xf.data(), loop.data(), loop.data(), kM, (float)kFs, kMax, 0.0f, 1.5f);
        check(!cable.valid, "15i no pedal (a cable): refused");

        std::vector<float> other(kM);
        Rng ro(8642u);
        for (float& v : other) v = (float)ro.next();
        auto raw = [&](const float* s, const float* d, const float* l, float t0, float t1, float win = 0.5f, int seg = 4096, float settle = 0.2f) {
            return wah::estimate_hold(s, d, l, kM, (float)kFs, kMax, t0, t1, win, seg, settle);
        };
        check(!raw(nullptr, dut.data(), loop.data(), 0.0f, 1.5f).valid && !raw(xf.data(), nullptr, loop.data(), 0.0f, 1.5f).valid &&
                  !raw(xf.data(), dut.data(), nullptr, 0.0f, 1.5f).valid,
              "16a null stimulus, pedal response or loopback");
        check(!raw(xf.data(), dut.data(), other.data(), 0.0f, 1.5f).valid, "16b loopback unrelated: refused");
        check(!raw(xf.data(), dut.data(), loop.data(), 1.0f, 1.0f).valid && !raw(xf.data(), dut.data(), loop.data(), 1.5f, 0.5f).valid,
              "16c empty or reversed interval");
        check(!raw(xf.data(), dut.data(), loop.data(), -0.5f, 1.0f).valid && !raw(xf.data(), dut.data(), loop.data(), 4.0f, 6.0f).valid,
              "16d interval outside the recording");
        // The recording declared shorter than the buffers: a hold that ends after it is refused, the same hold inside it is read.
        check(good(hold(dut, 2.6, 3.95), 1600.0) &&
                  !wah::estimate_hold(xf.data(), dut.data(), loop.data(), kM - 75000, (float)kFs, kMax, 2.6f, 3.95f).valid,
              "16f a hold that ends after the declared recording (3.84 s) is refused, the same hold inside a longer one is read");
        check(!raw(xf.data(), dut.data(), loop.data(), 0.0f, 1.5f, 0.0f).valid && !raw(xf.data(), dut.data(), loop.data(), 0.0f, 1.5f, 0.5f, 1000).valid &&
                  !raw(xf.data(), dut.data(), loop.data(), 0.0f, 1.5f, 0.05f).valid && !raw(xf.data(), dut.data(), loop.data(), 0.0f, 1.5f, 0.5f, 4096, -0.1f).valid,
              "16e window of 0 s, segment size not a power of two, window shorter than 4 segments, negative settling");
        check(!raw(xf.data(), dut.data(), loop.data(), 0.5f, 1.5f, 0.5f, 4096, -0.4f).valid,
              "16g negative settling that would start the windows before the hold: refused");
    }

    // 17. Aggregating holds (D44 to D47). Truth: heel 400 Hz, toe 1600 Hz, Q 5, +6.02 dB.
    {
        const float kInf2 = std::numeric_limits<float>::infinity();
        auto hold_of = [](double f0, double q, double g, int windows = 4) {
            wah::HoldEstimate h;
            h.center_freq = (float)f0; h.q = (float)q; h.peak_gain_db = (float)g; h.windows_used = windows; h.valid = true;
            return h;
        };
        auto noisy_holds = [&](int n, double f0, uint32_t seed) {
            Rng rg(seed);
            std::vector<wah::HoldEstimate> v;
            for (int i = 0; i < n; i++)
                v.push_back(hold_of(f0 * (1.0 + 0.01 * rg.next()), 5.0 * (1.0 + 0.05 * rg.next()), 6.02 + 0.3 * rg.next()));
            return v;
        };
        auto near_truth = [](const wah::HoldSummary& s, double f0) {
            return s.valid && std::abs(s.center_freq - f0) / f0 < 0.01 && std::abs(s.q - 5.0) < 0.15 && std::abs(s.peak_gain_db - 6.02) < 0.3;
        };
        auto has2 = [](const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); };

        std::vector<wah::HoldEstimate> heel = noisy_holds(8, 400.0, 11u), toe = noisy_holds(8, 1600.0, 22u);
        wah::HoldsAggregate a2 = wah::aggregate_holds(heel, toe);
        check(near_truth(a2.heel, 400.0) && near_truth(a2.toe, 1600.0) && a2.heel.kept == 8 && a2.toe.kept == 8 &&
                  a2.heel.dropped.empty() && a2.toe.dropped.empty(),
              "17a eight clean holds per end: each end near the truth, none dropped, the two ends independent");
        check(a2.heel.center_error < 0.01f && a2.heel.q_error < 0.05f && a2.heel.gain_error_db < 0.3f && a2.heel.center_error > 0.0f,
              "17b the errors are finite and small for eight clean holds");

        // Five holds with centre and Q at -4, -2, 0, +2, +4 % and gain at -0.4 ... +0.4 dB: the errors are
        // 1.2533 * 1.4826 * MAD / sqrt(5), with MAD = 0.020203 (in log) and 0.2 dB.
        std::vector<wah::HoldEstimate> five;
        for (int i = -2; i <= 2; i++) five.push_back(hold_of(400.0 * (1 + 0.02 * i), 5.0 * (1 + 0.02 * i), 6.0 + 0.2 * i));
        wah::HoldsAggregate af = wah::aggregate_holds(five, std::vector<wah::HoldEstimate>());
        check(af.heel.valid && std::abs(af.heel.center_error - 0.016788f) < 1e-4f && std::abs(af.heel.q_error - 0.016788f) < 1e-4f &&
                  std::abs(af.heel.gain_error_db - 0.16620f) < 2e-3f && af.heel.dropped.empty(),
              "17c the errors follow the D35 formula (0.016788 relative, 0.1662 dB for five holds)");
        check(!af.toe.valid && af.toe.kept == 0, "17d an end with no hold is not valid");

        // Wrong holds are dropped, each by its own rule.
        std::vector<wah::HoldEstimate> bad = heel;
        bad.push_back(hold_of(400.0, 8.0, 6.02));        // Q 60 % off
        bad.push_back(hold_of(400.0, 5.0, 9.5));         // gain 3.5 dB off
        bad.push_back(hold_of(540.0, 5.0, 6.02));        // centre 35 % off
        wah::HoldsAggregate ab = wah::aggregate_holds(bad, toe);
        check(near_truth(ab.heel, 400.0) && ab.heel.kept == 8 && ab.heel.dropped.size() == 3 && has2(ab.heel.dropped, 8) &&
                  has2(ab.heel.dropped, 9) && has2(ab.heel.dropped, 10),
              "17e a hold wrong in Q, one in gain and one in centre are each dropped, the medians do not move");
        std::vector<wah::HoldEstimate> near_limit = heel;
        near_limit.push_back(hold_of(400.0 * 1.10, 5.0 * 1.20, 6.02 + 1.5));  // inside all three limits
        wah::HoldsAggregate an = wah::aggregate_holds(near_limit, toe);
        check(an.heel.dropped.empty() && an.heel.kept == 9, "17f a hold 10 % off in centre, 20 % in Q and 1.5 dB in gain is kept (inside the limits)");

        // Invalid and junk holds are ignored and listed, together with a wrong hold, in order.
        std::vector<wah::HoldEstimate> junk2;
        junk2.push_back(hold_of(400.0, 8.0, 6.0));  // index 0: Q 60 % off
        junk2.insert(junk2.end(), heel.begin(), heel.end());  // indices 1 to 8
        wah::HoldEstimate inv = hold_of(400.0, 5.0, 6.0); inv.valid = false; junk2.push_back(inv);  // 9
        junk2.push_back(hold_of(0.0, 5.0, 6.0));            // 10
        junk2.push_back(hold_of(400.0, 0.0, 6.0));          // 11
        junk2.push_back(hold_of(400.0, 5.0, 6.0, 0));       // 12
        junk2.push_back(hold_of(NAN, 5.0, 6.0));            // 13
        junk2.push_back(hold_of(400.0, 5.0, NAN));          // 14
        junk2.push_back(hold_of(INFINITY, 5.0, 6.0));       // 15
        wah::HoldsAggregate aj = wah::aggregate_holds(junk2, toe);
        check(near_truth(aj.heel, 400.0) && aj.heel.kept == 8 && aj.heel.dropped.size() == 8 && has2(aj.heel.dropped, 0) &&
                  has2(aj.heel.dropped, 15) && std::is_sorted(aj.heel.dropped.begin(), aj.heel.dropped.end()),
              "17g invalid holds, zero, NaN or infinite values and a wrong hold are all listed as dropped, in order");

        // Fewer than 3 usable holds: nothing is dropped for disagreement, the error is infinite.
        std::vector<wah::HoldEstimate> two2 = {hold_of(400.0, 5.0, 6.0), hold_of(560.0, 8.0, 10.0)};
        wah::HoldsAggregate a22 = wah::aggregate_holds(two2, toe);
        check(a22.heel.valid && a22.heel.kept == 2 && a22.heel.dropped.empty() && a22.heel.center_error == kInf2 &&
                  a22.heel.q_error == kInf2 && a22.heel.gain_error_db == kInf2,
              "17h two holds that disagree: nothing is dropped, the errors are infinite (D44, D45)");
        wah::HoldsAggregate a1 = wah::aggregate_holds(std::vector<wah::HoldEstimate>(1, hold_of(400.0, 5.0, 6.0)), toe);
        check(a1.heel.valid && a1.heel.kept == 1 && a1.heel.center_freq == 400.0f && a1.heel.q == 5.0f && a1.heel.peak_gain_db == 6.0f,
              "17i a single hold gives its own values");

        // The weight is the number of windows: a hold of 4 windows outweighs a lower one of 1.
        std::vector<wah::HoldEstimate> wtd = {hold_of(400.0, 4.6, 6.0, 1), hold_of(400.0, 5.0, 6.0, 4)};
        check(wah::aggregate_holds(wtd, toe).heel.q == 5.0f, "17j weights: the hold measured from 4 windows outweighs the one from 1");

        // With too few holds to disagree with, a zero centre or Q must still be refused as unusable.
        wah::HoldsAggregate z1 = wah::aggregate_holds({hold_of(0.0, 5.0, 6.0), hold_of(400.0, 5.0, 6.0)}, toe);
        wah::HoldsAggregate z2 = wah::aggregate_holds({hold_of(400.0, 0.0, 6.0), hold_of(400.0, 5.0, 6.0)}, toe);
        check(z1.heel.kept == 1 && z1.heel.dropped.size() == 1 && z1.heel.dropped[0] == 0 && z1.heel.center_freq == 400.0f &&
                  z2.heel.kept == 1 && z2.heel.dropped.size() == 1 && z2.heel.dropped[0] == 0 && z2.heel.q == 5.0f,
              "17l with two holds, one with a centre or Q of zero is still left out as unusable");

        check(!wah::aggregate_holds(std::vector<wah::HoldEstimate>(), std::vector<wah::HoldEstimate>()).heel.valid &&
                  !wah::aggregate_holds(std::vector<wah::HoldEstimate>(), std::vector<wah::HoldEstimate>()).toe.valid,
              "17k no holds at all: neither end is valid");
    }

    // 19. Round 17 (D7): the shape of the pedal, 2-pole or 2-pole with a low shelf.
    {
        // Analytic transfer function (the reference is computed here, not by the code under test).
        auto shape_db = [](double fr, double f0, double q, double g, double fc, double sg_db) {
            const double r = fr / f0, d = q * (r - 1.0 / r);
            double db = g - 10.0 * std::log10(1.0 + d * d);
            if (fc > 0) {
                const double G = std::pow(10.0, sg_db / 20.0), x = (fr / fc) * (fr / fc);
                db += 10.0 * std::log10((G * G + x) / (1.0 + x));
            }
            return db;
        };
        auto make_tf = [&](double f0, double q, double g, double fc, double sg_db, double noise_db, std::uint32_t seed) {
            wah::TransferFunction tf;
            Rng rn(seed);
            for (int k = 1; k < 2048; k++) {  // 11.7 Hz bins, up to 24 kHz
                const double fr = k * kFs / 4096.0;
                tf.freq_hz.push_back((float)fr);
                tf.magnitude_db.push_back((float)(shape_db(fr, f0, q, g, fc, sg_db) + noise_db * rn.next() * 1.7320508));
                tf.coherence.push_back(1.0f);
            }
            tf.num_segments = 50;
            tf.valid = true;
            return tf;
        };
        // A shelf of -9 dB below 300 Hz under a band-pass at 1.2 kHz, Q 4, +3 dB.
        const wah::TransferFunction pure = make_tf(800, 5, 6, 0, 0, 0.0, 11u);
        const wah::TransferFunction shelved = make_tf(1200, 4, 3, 300, -9, 0.0, 12u);

        wah::ShapeFit a = wah::fit_shape(pure);
        check(a.valid && a.model == wah::ShapeModel::BandPass2 && std::abs(a.center_freq - 800.0) < 8.0 &&
                  std::abs(a.q - 5.0) < 0.25 && std::abs(a.peak_gain_db - 6.0) < 0.3 && a.fit_rmse_db < 0.5 &&
                  a.shelf_freq_hz == 0.0f && a.shelf_gain_db == 0.0f,
              "19a a pure band-pass: 2-pole chosen, centre within 1 %, Q within 5 %, gain within 0.3 dB, shelf fields 0");

        // The shelved shape must not be explainable by the 2-pole alone, or 18b proves nothing.
        wah::BandpassEstimate plain = wah::estimate_bandpass(shelved);
        check(!plain.valid || plain.fit_rmse_db > 1.5, "19b the shelved test shape does not fit a plain 2-pole within 1.5 dB");

        wah::ShapeFit b = wah::fit_shape(shelved);
        check(b.valid && b.model == wah::ShapeModel::BandPass2Shelf && std::abs(b.center_freq - 1200.0) < 60.0 &&
                  std::abs(b.q - 4.0) < 0.6 && std::abs(b.peak_gain_db - 3.0) < 1.0 &&
                  std::abs(b.shelf_freq_hz - 300.0) < 60.0 && std::abs(b.shelf_gain_db + 9.0) < 1.5 && b.fit_rmse_db <= 1.5f,
              "19c a shelved band-pass: shelf model chosen, centre within 5 %, Q within 15 %, shelf corner within 20 %, shelf gain within 1.5 dB");

        // With noise (0.4 dB RMS per bin) the same two decisions.
        wah::ShapeFit an = wah::fit_shape(make_tf(800, 5, 6, 0, 0, 0.4, 21u));
        wah::ShapeFit bn = wah::fit_shape(make_tf(1200, 4, 3, 300, -9, 0.4, 22u));
        check(an.valid && an.model == wah::ShapeModel::BandPass2 && std::abs(an.center_freq - 800.0) < 16.0 && std::abs(an.q - 5.0) < 0.5,
              "19d the pure band-pass with noise: still the 2-pole, centre within 2 %, Q within 10 %");
        check(bn.valid && bn.model == wah::ShapeModel::BandPass2Shelf && std::abs(bn.center_freq - 1200.0) < 80.0 &&
                  std::abs(bn.shelf_gain_db + 9.0) < 2.5,
              "19e the shelved band-pass with noise: still the shelf model, centre within 7 %");

        // The simplest model wins: a 0.3 dB shelf is not worth two more parameters.
        wah::ShapeFit s = wah::fit_shape(make_tf(800, 5, 6, 300, -0.3, 0.0, 13u));
        check(s.valid && s.model == wah::ShapeModel::BandPass2 && s.fit_rmse_db <= 1.5f,
              "19f a negligible shelf (0.3 dB): the 2-pole is chosen, simplest first");

        // Refused: flat, random, invalid, too few bins.
        wah::TransferFunction flat = pure;
        for (float& v : flat.magnitude_db) v = 0.0f;
        wah::ShapeFit fl = wah::fit_shape(flat);
        wah::TransferFunction rnd = pure;
        Rng rr(777u);
        for (float& v : rnd.magnitude_db) v = (float)(12.0 * rr.next());
        check(fl.valid && fl.model == wah::ShapeModel::Measured, "19g a flat response is no model: the measurement itself (D58)");
        check(wah::fit_shape(rnd).valid && wah::fit_shape(rnd).model == wah::ShapeModel::Measured, "19h a random response (+-12 dB) is no model: the measurement itself (D58)");
        wah::TransferFunction few = pure;
        few.freq_hz.resize(15); few.magnitude_db.resize(15); few.coherence.resize(15);
        wah::TransferFunction badsize = pure;
        badsize.coherence.pop_back();
        check(!wah::fit_shape(wah::TransferFunction()).valid && !wah::fit_shape(few).valid && !wah::fit_shape(badsize).valid,
              "19i an invalid transfer function, fewer than 20 bins and mismatched sizes are refused");

        // Bins with low coherence must not count: garbage in half the bins, marked at 0.1.
        wah::TransferFunction junk = shelved;
        Rng rj(5150u);
        for (size_t i = 0; i < junk.freq_hz.size(); i += 2) { junk.magnitude_db[i] = (float)(20.0 * rj.next()); junk.coherence[i] = 0.1f; }
        wah::ShapeFit j = wah::fit_shape(junk);
        check(j.valid && j.model == wah::ShapeModel::BandPass2Shelf && std::abs(j.center_freq - 1200.0) < 60.0,
              "19j bins below the coherence floor are ignored");

        // The range is respected: a 2-pole at 800 Hz, with a wild bump outside 20..5000 Hz.
        wah::TransferFunction bump = pure;
        for (size_t i = 0; i < bump.freq_hz.size(); i++) if (bump.freq_hz[i] > 6000.0f) bump.magnitude_db[i] += 30.0f;
        wah::ShapeFit rg = wah::fit_shape(bump);
        check(rg.valid && rg.model == wah::ShapeModel::BandPass2 && std::abs(rg.center_freq - 800.0) < 8.0,
              "19k data outside the frequency range is ignored");

        // Between 1.5 and 3 dB the 2-pole is not good enough: a 5 dB shelf leaves it at about 1.8 dB.
        const wah::TransferFunction mid = make_tf(1200, 4, 3, 300, -5, 0.0, 31u);
        wah::BandpassEstimate midbp = wah::estimate_bandpass(mid);
        wah::ShapeFit m = wah::fit_shape(mid);
        check(midbp.valid && midbp.fit_rmse_db > 1.5f && midbp.fit_rmse_db <= 3.0f,
              "19l the 5 dB shelf leaves the plain 2-pole between 1.5 and 3 dB (the test shape is not trivial)");
        check(m.valid && m.model == wah::ShapeModel::BandPass2Shelf && m.fit_rmse_db < midbp.fit_rmse_db && std::abs(m.shelf_gain_db + 5.0) < 1.5,
              "19m a 2-pole at 1.8 dB is not accepted when the shelf model fits better");

        // Neither within 1.5 dB: the lower residual wins. A wide band-pass (Q 0.5) with 2 dB of noise: the
        // 2-pole fits to the noise floor, the shelf model (Q of at least 1) cannot do better.
        const wah::TransferFunction wide = make_tf(800, 0.5, 6, 0, 0, 2.0, 32u);
        wah::ShapeFit w = wah::fit_shape(wide);
        wah::BandpassEstimate widebp = wah::estimate_bandpass(wide);
        check(widebp.valid && widebp.fit_rmse_db > 1.5f && w.valid && w.model == wah::ShapeModel::BandPass2 && w.fit_rmse_db == widebp.fit_rmse_db,
              "19n neither model within 1.5 dB: the one with the lower residual (the 2-pole) is chosen");

        // The shelf is a low shelf: a corner above the centre is never reported.
        wah::ShapeFit ca = wah::fit_shape(make_tf(300, 4, 3, 1000, -9, 0.0, 33u));
        check(!ca.valid || ca.model == wah::ShapeModel::BandPass2 || ca.shelf_freq_hz <= ca.center_freq,
              "19o the shelf corner is never above the centre");

        // The range also holds for the shelf model.
        wah::TransferFunction sbump = shelved;
        for (size_t i = 0; i < sbump.freq_hz.size(); i++) if (sbump.freq_hz[i] > 6000.0f) sbump.magnitude_db[i] += 30.0f;
        wah::ShapeFit sr = wah::fit_shape(sbump);
        check(sr.valid && sr.model == wah::ShapeModel::BandPass2Shelf && std::abs(sr.center_freq - 1200.0) < 60.0,
              "19p data outside the frequency range is ignored by the shelf model too");

        // Neither within 1.5 dB, the shelf model lower: it wins (with 1.8 dB of noise: 2-pole 2.3 dB, shelf 1.7 dB).
        const wah::TransferFunction mid_n = make_tf(1200, 4, 3, 300, -5, 1.8, 41u);
        wah::BandpassEstimate midn_bp = wah::estimate_bandpass(mid_n);
        wah::ShapeFit mn = wah::fit_shape(mid_n);
        check(midn_bp.valid && midn_bp.fit_rmse_db > 1.5f && mn.valid && mn.fit_rmse_db > 1.5f &&
                  mn.model == wah::ShapeModel::BandPass2Shelf && mn.fit_rmse_db < midn_bp.fit_rmse_db,
              "19q neither within 1.5 dB and the shelf model lower: the shelf model is chosen");
    }

    // 20. Round 18 (D7): the 4-pole, the product of two 2-pole band-passes.
    {
        auto bp_db = [](double fr, double f0, double q) {
            const double r = fr / f0, d = q * (r - 1.0 / r);
            return -10.0 * std::log10(1.0 + d * d);
        };
        auto make4 = [&](double f1, double q1, double f2, double q2, double g, double noise_db, std::uint32_t seed) {
            wah::TransferFunction tf;
            Rng rn(seed);
            for (int k = 1; k < 2048; k++) {
                const double fr = k * kFs / 4096.0;
                tf.freq_hz.push_back((float)fr);
                tf.magnitude_db.push_back((float)(g + bp_db(fr, f1, q1) + bp_db(fr, f2, q2) + noise_db * rn.next() * 1.7320508));
                tf.coherence.push_back(1.0f);
            }
            tf.num_segments = 50;
            tf.valid = true;
            return tf;
        };
        const wah::TransferFunction four = make4(500, 3, 1800, 4, 12, 0.0, 1u);
        wah::BandpassEstimate fbp = wah::estimate_bandpass(four);
        check(!fbp.valid, "20a the 4-pole test shape is not a 2-pole within 3 dB (it is not trivial)");

        wah::ShapeFit a = wah::fit_shape(four);
        check(a.valid && a.model == wah::ShapeModel::BandPass4 && std::abs(a.center_freq - 500.0) < 10.0 && std::abs(a.q - 3.0) < 0.3 &&
                  std::abs(a.center2_freq - 1800.0) < 36.0 && std::abs(a.q2 - 4.0) < 0.4 && std::abs(a.peak_gain_db - 12.0) < 0.6 &&
                  a.shelf_freq_hz == 0.0f && a.shelf_gain_db == 0.0f && a.fit_rmse_db < 0.5f,
              "20b a 4-pole (500 Hz Q 3, 1.8 kHz Q 4, 12 dB): chosen, centres within 2 %, Qs within 10 %, gain sum within 0.6 dB, shelf fields 0");
        check(a.center_freq < a.center2_freq && a.center2_freq >= 1.1f * a.center_freq && a.center2_freq <= 4.0f * a.center_freq,
              "20c the first centre is the lower and the two are between 10 % and 2 octaves apart");

        wah::ShapeFit an = wah::fit_shape(make4(500, 3, 1800, 4, 12, 0.4, 2u));
        check(an.valid && an.model == wah::ShapeModel::BandPass4 && std::abs(an.center_freq - 500.0) < 25.0 &&
                  std::abs(an.center2_freq - 1800.0) < 90.0 && std::abs(an.q - 3.0) < 0.6 && std::abs(an.q2 - 4.0) < 0.8,
              "20d the same with 0.4 dB of noise: centres within 5 %, Qs within 20 %");

        // Two close, narrow band-passes (700 and 1400 Hz are an octave apart; 800 and 880 are 10 % apart).
        wah::ShapeFit c = wah::fit_shape(make4(700, 6, 1400, 6, 10, 0.0, 3u));
        wah::ShapeFit c2 = wah::fit_shape(make4(800, 8, 880, 8, 10, 0.0, 6u));
        check(c.valid && c.model == wah::ShapeModel::BandPass4 && std::abs(c.center_freq - 700.0) < 14.0 && std::abs(c.center2_freq - 1400.0) < 28.0 &&
                  c2.valid && c2.model == wah::ShapeModel::BandPass4 && std::abs(c2.center_freq - 800.0) < 16.0 && std::abs(c2.center2_freq - 880.0) < 18.0,
              "20e centres an octave apart and only 10 % apart are both recovered");

        // Centres 2.6 octaves apart (400 and 2400 Hz) are outside the model: the best the 4-pole can do is a
        // compromise within 2 octaves, which is worse than 1.5 dB but better than 3 dB (the lowest residual wins).
        wah::ShapeFit fa = wah::fit_shape(make4(400, 3, 2400, 3, 12, 0.0, 7u));
        check(fa.valid && fa.model == wah::ShapeModel::BandPass4 && fa.center2_freq <= 4.0f * fa.center_freq * 1.0001f &&
                  fa.fit_rmse_db > 1.5f && fa.fit_rmse_db <= 3.0f,
              "20f centres 2.6 octaves apart: the 4-pole is the best of three within 3 dB, with its centres at most 2 octaves apart");

        // Wide band-passes (Q 0.7) are outside the model's Q floor of 1: the Q is never reported below 1.
        wah::ShapeFit wq = wah::fit_shape(make4(400, 0.7, 1200, 0.7, 8, 0.0, 1u));
        check(wq.valid && wq.model == wah::ShapeModel::BandPass4 && wq.q >= 0.999f && wq.q2 >= 0.999f,
              "20j two wide band-passes (Q 0.7): the 4-pole is reported with Q at least 1");

        // A plateau between two far band-passes must not fake a flat response.
        wah::TransferFunction flat = four;
        for (float& v : flat.magnitude_db) v = 0.0f;
        check(wah::fit_shape(flat).model == wah::ShapeModel::Measured, "20g a flat response is still no model with the 4-pole in the choice (the plateau does not fake one)");

        // Coherence floor and range also hold for the 4-pole.
        wah::TransferFunction junk = four, bump = four;
        Rng rj(5150u);
        for (size_t i = 0; i < junk.freq_hz.size(); i += 2) { junk.magnitude_db[i] = (float)(20.0 * rj.next()); junk.coherence[i] = 0.1f; }
        for (size_t i = 0; i < bump.freq_hz.size(); i++) if (bump.freq_hz[i] > 6000.0f) bump.magnitude_db[i] += 30.0f;
        wah::ShapeFit j = wah::fit_shape(junk), b = wah::fit_shape(bump);
        check(j.valid && j.model == wah::ShapeModel::BandPass4 && std::abs(j.center2_freq - 1800.0) < 36.0 &&
                  b.valid && b.model == wah::ShapeModel::BandPass4 && std::abs(b.center2_freq - 1800.0) < 36.0,
              "20h bins under the coherence floor and outside the range are ignored by the 4-pole");
    }

    // 21. Round 19 (D58): when no model fits, the measurement itself.
    {
        const double kTwoPi = 2.0 * kPi;
        auto comb_db = [&](double fr) { return 8.0 * std::sin(kTwoPi * 1.5 * std::log2(fr / 100.0)); };
        auto make_comb = [&]() {
            wah::TransferFunction tf;
            for (int k = 1; k < 2048; k++) {
                const double fr = k * kFs / 4096.0;
                tf.freq_hz.push_back((float)fr);
                tf.magnitude_db.push_back((float)comb_db(fr));
                tf.coherence.push_back(1.0f);
            }
            tf.num_segments = 50;
            tf.valid = true;
            return tf;
        };
        const wah::TransferFunction comb = make_comb();
        wah::ShapeFit a = wah::fit_shape(comb);
        bool table_ok = (int)a.table_freq_hz.size() == wah::kShapeTablePoints && (int)a.table_db.size() == wah::kShapeTablePoints;
        double worst = 0;
        bool increasing = table_ok;
        for (int i = 0; table_ok && i < wah::kShapeTablePoints; i++) {
            if (a.table_freq_hz[i] >= 200.0f) worst = std::max(worst, std::abs(a.table_db[i] - comb_db(a.table_freq_hz[i])));  // below, bins are wider than the ripple
            if (i > 0 && !(a.table_freq_hz[i] > a.table_freq_hz[i - 1])) increasing = false;
            if (a.table_freq_hz[i] < 20.0f || a.table_freq_hz[i] > 5000.0f) increasing = false;
        }
        check(a.valid && a.model == wah::ShapeModel::Measured && a.fit_rmse_db == 0.0f && a.best_model_rmse_db > 3.0f,
              "21a a comb ripple (+-8 dB) that no model fits: Measured, residual 0, the best model's residual above 3 dB");
        check(table_ok && increasing && worst < 2.0,
              "21b the table has 64 points, in increasing frequency inside the range, within 2 dB of the measurement from 200 Hz up (below, the bins are wider than the ripple)");

        // A flat response gives a flat table.
        wah::TransferFunction flat = comb;
        for (float& v : flat.magnitude_db) v = 0.0f;
        wah::ShapeFit f = wah::fit_shape(flat);
        double fmax_abs = 0;
        for (float v : f.table_db) fmax_abs = std::max(fmax_abs, (double)std::abs(v));
        check(f.valid && f.model == wah::ShapeModel::Measured && (int)f.table_db.size() == wah::kShapeTablePoints && fmax_abs < 0.01,
              "21c a flat response: a table of 64 zeros");

        // A model result has no table and its best residual is its own.
        wah::TransferFunction pure;
        for (int k = 1; k < 2048; k++) {
            const double fr = k * kFs / 4096.0, r = fr / 800.0, d = 5.0 * (r - 1.0 / r);
            pure.freq_hz.push_back((float)fr);
            pure.magnitude_db.push_back((float)(6.0 - 10.0 * std::log10(1.0 + d * d)));
            pure.coherence.push_back(1.0f);
        }
        pure.valid = true;
        wah::ShapeFit p = wah::fit_shape(pure);
        check(p.valid && p.model == wah::ShapeModel::BandPass2 && p.table_db.empty() && p.table_freq_hz.empty() &&
                  p.best_model_rmse_db == p.fit_rmse_db,
              "21d a band-pass: no table, and the best model's residual is the chosen one's");

        // A gap of half an octave (300 to 425 Hz without data) is interpolated in log frequency.
        wah::TransferFunction gap = comb;
        for (size_t i = 0; i < gap.freq_hz.size(); i++) if (gap.freq_hz[i] > 300.0f && gap.freq_hz[i] < 425.0f) gap.coherence[i] = 0.0f;
        wah::ShapeFit g = wah::fit_shape(gap);
        int i0 = -1, i1 = -1;
        for (int i = 0; g.valid && i < (int)g.table_freq_hz.size(); i++) {
            if (g.table_freq_hz[i] <= 300.0f) i0 = i;
            if (g.table_freq_hz[i] >= 425.0f && i1 < 0) i1 = i;
        }
        bool linear = i0 >= 0 && i1 > i0 + 1;
        for (int i = i0 + 1; linear && i < i1; i++) {
            // every empty cell lies on the straight line (in log frequency) between the cells either side of the gap
            const double t_ = std::log(g.table_freq_hz[i] / g.table_freq_hz[i0]) / std::log(g.table_freq_hz[i1] / g.table_freq_hz[i0]);
            const double want = g.table_db[i0] + t_ * (g.table_db[i1] - g.table_db[i0]);
            if (std::abs(g.table_db[i] - want) > 0.5) linear = false;
        }
        check(g.valid && g.model == wah::ShapeModel::Measured && linear, "21e a gap of half an octave is filled by interpolation in log frequency");

        // A gap of more than an octave (300 to 900 Hz) is refused.
        wah::TransferFunction wide = comb;
        for (size_t i = 0; i < wide.freq_hz.size(); i++) if (wide.freq_hz[i] > 300.0f && wide.freq_hz[i] < 900.0f) wide.coherence[i] = 0.0f;
        wah::ShapeFit w = wah::fit_shape(wide);
        check(!w.valid && w.model == wah::ShapeModel::None && w.table_db.empty(), "21f a gap of 1.6 octaves is refused (nothing to measure)");

        // Edges: data from 35 Hz up leaves 0.8 octave at the bottom (held at the nearest value); from 300 Hz up leaves 3.9 (refused).
        wah::TransferFunction low = comb, high = comb;
        for (size_t i = 0; i < low.freq_hz.size(); i++) {
            if (low.freq_hz[i] < 35.0f) low.coherence[i] = 0.0f;
            if (high.freq_hz[i] < 300.0f) high.coherence[i] = 0.0f;
        }
        wah::ShapeFit l = wah::fit_shape(low), hi = wah::fit_shape(high);
        check(l.valid && l.model == wah::ShapeModel::Measured && l.table_db.size() == (size_t)wah::kShapeTablePoints &&
                  std::abs(l.table_db[0] - comb_db(35.2)) < 4.0 && !hi.valid,
              "21g 0.8 octave missing at the bottom: held at the nearest value; 3.9 octaves missing: refused");

        // Bins under the coherence floor are not in the table.
        wah::TransferFunction junk = comb;
        Rng rj(5150u);
        for (size_t i = 0; i < junk.freq_hz.size(); i += 2) { junk.magnitude_db[i] = (float)(40.0 * rj.next()); junk.coherence[i] = 0.1f; }
        wah::ShapeFit j = wah::fit_shape(junk);
        double jw = 0;
        for (size_t i = 0; i < j.table_db.size(); i++) if (j.table_freq_hz[i] >= 200.0f) jw = std::max(jw, std::abs((double)j.table_db[i] - comb_db(j.table_freq_hz[i])));
        check(j.valid && j.model == wah::ShapeModel::Measured && jw < 2.0, "21h bins under the coherence floor are not in the table");

        // The range: bins outside freq_min..freq_max are not in the table either.
        wah::TransferFunction out_of_range = comb;
        for (size_t i = 0; i < out_of_range.freq_hz.size(); i++) if (out_of_range.freq_hz[i] > 6000.0f) out_of_range.magnitude_db[i] += 40.0f;
        wah::ShapeFit o = wah::fit_shape(out_of_range);
        double ow = 0;
        for (size_t i = 0; i < o.table_db.size(); i++) if (o.table_freq_hz[i] >= 200.0f) ow = std::max(ow, std::abs((double)o.table_db[i] - comb_db(o.table_freq_hz[i])));
        check(o.valid && o.model == wah::ShapeModel::Measured && ow < 2.0, "21i bins outside the range are not in the table");

        // The mean is weighted by coherence: junk of +20 dB at coherence 0.55 (above the floor) among good bins at 1.0
        // pulls each cell by 20 * 0.55 / 1.55 = 7.1 dB, not by the 10 dB of a plain mean.
        wah::TransferFunction weighted = comb;
        for (size_t i = 0; i < weighted.freq_hz.size(); i += 2) { weighted.magnitude_db[i] += 20.0f; weighted.coherence[i] = 0.55f; }
        wah::ShapeFit wt = wah::fit_shape(weighted);
        double lo_bias = 1e9, hi_bias = -1e9;
        for (size_t i = 0; i < wt.table_db.size(); i++)
            if (wt.table_freq_hz[i] >= 1000.0f) {  // cells with many bins, so the junk share is one half
                const double b = wt.table_db[i] - comb_db(wt.table_freq_hz[i]);
                lo_bias = std::min(lo_bias, b);
                hi_bias = std::max(hi_bias, b);
            }
        check(wt.valid && lo_bias > 5.6 && hi_bias < 8.6, "21k the mean of a cell is weighted by coherence (bias 7.1 dB, not 10)");

        // Still refused: an invalid transfer function (no data at all is not a measurement).
        check(!wah::fit_shape(wah::TransferFunction()).valid, "21j an invalid transfer function is still refused");
    }

    // 22. The shape of a hold (round 20, M5). The pedal of 15: Q 5, +6 dB, centre 400 Hz until 1.5 s, rising to 1600 Hz
    //     by 2.5 s, holding until 4 s. Same chain (3000-sample latency, 1.5 kHz high-pass).
    {
        const int kM = 1 << 18, kD = 3000, kMax = 4800;
        const Biquad chain = highpass1(1500.0, kFs);
        const std::vector<float> xf = wah::generate_stimulus(kM, 7u, 0.1f);
        std::vector<double> x(xf.begin(), xf.end());
        auto centre_at = [](double t) { return t < 1.5 ? 400.0 : (t < 2.5 ? 400.0 * std::pow(4.0, t - 1.5) : 1600.0); };
        std::vector<double> y(kM);
        {
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (int i = 0; i < kM; i++) {
                const double w0 = 2.0 * kPi * centre_at(i / kFs) / kFs;
                const double al = std::sin(w0) / 10.0, a0 = 1.0 + al;
                const double v = 2.0 * (al / a0) * (x[i] - x2) - (-2.0 * std::cos(w0) / a0) * y1 - ((1.0 - al) / a0) * y2;
                x2 = x1; x1 = x[i]; y2 = y1; y1 = v; y[i] = v;
            }
        }
        auto lag = [&](const std::vector<double>& v) {
            std::vector<float> o(kM, 0.0f);
            for (int i = kD; i < kM; i++) o[i] = (float)v[i - kD];
            return o;
        };
        const std::vector<float> loop = lag(run(chain, x));
        const std::vector<float> dut = lag(run(chain, y));
        auto hs = [&](const std::vector<float>& d, double t0, double t1, float settle = 0.2f) {
            return wah::fit_hold_shape(xf.data(), d.data(), loop.data(), kM, (float)kFs, kMax, (float)t0, (float)t1, 0.5f, 4096, settle);
        };
        auto good = [](const wah::HoldShape& h, double f0) {
            return h.valid && h.shape.valid && h.shape.model == wah::ShapeModel::BandPass2 && std::abs(h.shape.center_freq - f0) / f0 < 0.02 &&
                   std::abs(h.shape.q - 5.0) < 0.5 && std::abs(h.shape.peak_gain_db - 6.02) < 1.0;
        };

        wah::HoldShape heel = hs(dut, 0.0, 1.5), toe = hs(dut, 2.5, 4.0);
        check(good(heel, 400.0) && heel.windows_checked >= 3, "22a heel hold: 2-pole, centre within 2 %, Q within 10 %, gain within 1 dB, 3 or more windows checked");
        check(good(toe, 1600.0) && toe.windows_checked >= 3, "22b toe hold: the same at 1600 Hz");
        check(!hs(dut, 1.5, 2.5).valid, "22c an interval in which the pedal moves (the rise): refused");
        check(!hs(dut, 0.6, 2.0).valid, "22d a heel hold that overshoots into the movement: refused");
        check(good(hs(dut, 0.0, 1.0), 400.0) && !hs(dut, 0.0, 0.9).valid, "22e a hold of 1 s (2 windows) is read right, one of 0.9 s (1 window) is refused");
        check(good(hs(dut, 2.2, 4.0), 1600.0) && !hs(dut, 2.2, 4.0, 0.0f).valid, "22f settling matters: 0.3 s of the pedal still moving is left out with 0.2 s of settling, refused with none");

        // A static pedal that no model fits: two peaks in parallel (300 Hz and 1500 Hz, Q 8). Its windows give no centre, so the
        // movement cannot be checked: refused (D61 proposed), not accepted unchecked.
        {
            const Biquad b1 = bandpass(300.0, 8.0, kFs), b2 = bandpass(1500.0, 8.0, kFs);
            const std::vector<double> y1 = run(b1, x), y2 = run(b2, x);
            std::vector<double> sum(kM);
            for (int i = 0; i < kM; i++) sum[i] = y1[i] + y2[i];
            check(!hs(lag(run(chain, sum)), 0.0, 1.5).valid, "22g a static pedal with two peaks (no model fits): refused, movement cannot be assessed");
        }

        // Only one window of the hold is usable (a noise burst over the other three): the movement cannot be checked, refused.
        {
            std::vector<float> burst = dut;
            Rng rb(1357u);
            for (int i = (int)(0.7 * kFs); i < (int)(1.5 * kFs); i++) burst[i] += (float)(0.5 * rb.next());
            check(!hs(burst, 0.0, 1.5).valid && hs(dut, 0.0, 1.5).valid, "22p three of four windows spoiled by noise: refused (one window cannot show that the pedal stood still)");
        }

        // No pedal (a cable) is flat: no centre either, refused.
        check(!wah::fit_hold_shape(xf.data(), loop.data(), loop.data(), kM, (float)kFs, kMax, 0.0f, 1.5f).valid, "22h no pedal (a cable): refused");

        std::vector<float> other(kM);
        Rng ro(8642u);
        for (float& v : other) v = (float)ro.next();
        auto raw = [&](const float* s, const float* d, const float* l, float t0, float t1, float win = 0.5f, int seg = 4096, float settle = 0.2f) {
            return wah::fit_hold_shape(s, d, l, kM, (float)kFs, kMax, t0, t1, win, seg, settle).valid;
        };
        check(!raw(nullptr, dut.data(), loop.data(), 0.0f, 1.5f) && !raw(xf.data(), nullptr, loop.data(), 0.0f, 1.5f) &&
                  !raw(xf.data(), dut.data(), nullptr, 0.0f, 1.5f), "22i null stimulus, pedal response or loopback");
        check(!raw(xf.data(), dut.data(), other.data(), 0.0f, 1.5f), "22j loopback unrelated: refused");
        check(!raw(xf.data(), dut.data(), loop.data(), 1.0f, 1.0f) && !raw(xf.data(), dut.data(), loop.data(), 1.5f, 0.5f), "22k empty or reversed interval");
        check(!raw(xf.data(), dut.data(), loop.data(), -0.5f, 1.0f) && !raw(xf.data(), dut.data(), loop.data(), 4.0f, 6.0f), "22l interval outside the recording");
        check(good(hs(dut, 2.6, 3.95), 1600.0) &&
                  !wah::fit_hold_shape(xf.data(), dut.data(), loop.data(), kM - 75000, (float)kFs, kMax, 2.6f, 3.95f).valid,
              "22m a hold that ends after the declared recording is refused, the same hold inside a longer one is read");
        check(!raw(xf.data(), dut.data(), loop.data(), 0.0f, 1.5f, 0.0f) && !raw(xf.data(), dut.data(), loop.data(), 0.0f, 1.5f, 0.5f, 1000) &&
                  !raw(xf.data(), dut.data(), loop.data(), 0.0f, 1.5f, 0.05f) && !raw(xf.data(), dut.data(), loop.data(), 0.0f, 1.5f, 0.5f, 4096, -0.1f),
              "22n window of 0 s, segment size not a power of two, window shorter than 4 segments, negative settling");
        // The same answer as estimate_hold on the same hold: the two functions look at the same windows.
        wah::HoldEstimate e = wah::estimate_hold(xf.data(), dut.data(), loop.data(), kM, (float)kFs, kMax, 0.0f, 1.5f);
        check(e.valid && heel.valid && std::abs(e.center_freq - heel.shape.center_freq) / e.center_freq < 0.02 && std::abs(e.q - heel.shape.q) / e.q < 0.10,
              "22o the shape of the heel hold agrees with estimate_hold (centre within 2 %, Q within 10 %)");
    }

    if (failures) {
        std::cout << "\n✗ Test FAILED: " << failures << " check(s)\n";
        return 1;
    }
    std::cout << "\n✓ Test PASSED\n";
    return 0;
}
