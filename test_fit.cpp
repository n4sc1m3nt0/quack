/**
 * test_fit.cpp
 * Test the Levenberg-Marquardt profile fitting.
 */

#include "libwah.h"
#include "wah_fit.h"
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    // Simulate a wah pedal capture with synthetic measurements.
    // We'll create measurements that follow a log-frequency sweep.

    // Calibration data (from heel and toe measurements)
    float heel_freq = 450.0f;
    float toe_freq = 2100.0f;
    float heel_q = 4.2f;
    float toe_q = 4.8f;
    float heel_gain_db = 17.0f;
    float toe_gain_db = 18.5f;

    // Generate synthetic measurement samples across the sweep
    std::vector<wah::MeasurementSample> samples;

    // Stage 1: quiet level (-20 dBu) - 50 samples
    for (int i = 0; i < 50; i++) {
        float position = (float)i / 50.0f;
        float log_freq = std::log(heel_freq) + position * std::log(toe_freq / heel_freq);
        float measured_freq = std::exp(log_freq) + (rand() % 10 - 5);  // ±5 Hz noise

        float measured_q = heel_q + position * (toe_q - heel_q) * 0.95f;  // slight compression
        float measured_gain = heel_gain_db + position * (toe_gain_db - heel_gain_db);

        samples.push_back(wah::MeasurementSample{
            position,
            measured_freq,
            measured_q,
            measured_gain,
            -20.0f
        });
    }

    // Stage 2: medium level (-10 dBu) - 50 samples
    for (int i = 0; i < 50; i++) {
        float position = (float)i / 50.0f;
        float log_freq = std::log(heel_freq) + position * std::log(toe_freq / heel_freq);
        float measured_freq = std::exp(log_freq) + (rand() % 10 - 5);

        float measured_q = heel_q + position * (toe_q - heel_q) * 0.80f;  // more compression
        float measured_gain = heel_gain_db + position * (toe_gain_db - heel_gain_db);

        samples.push_back(wah::MeasurementSample{
            position,
            measured_freq,
            measured_q,
            measured_gain,
            -10.0f
        });
    }

    // Stage 3: hot level (0 dBu) - 50 samples
    for (int i = 0; i < 50; i++) {
        float position = (float)i / 50.0f;
        float log_freq = std::log(heel_freq) + position * std::log(toe_freq / heel_freq);
        float measured_freq = std::exp(log_freq) + (rand() % 10 - 5);

        float measured_q = heel_q + position * (toe_q - heel_q) * 0.60f;  // strong compression
        float measured_gain = heel_gain_db + position * (toe_gain_db - heel_gain_db);

        samples.push_back(wah::MeasurementSample{
            position,
            measured_freq,
            measured_q,
            measured_gain,
            0.0f
        });
    }

    // Run fitting
    auto result = wah::fit_profile(samples, heel_freq, toe_freq, heel_q, toe_q,
                                    heel_gain_db, toe_gain_db);

    if (!result) {
        std::cout << "✗ Fitting failed\n";
        return 1;
    }

    const auto& profile = result.value();
    std::cout << "Profile Fitting Results:\n";
    std::cout << "  Topology: " << (profile.filter.topology == wah::Topology::Bandpass2Pole ? "2-pole" : "4-pole") << "\n";
    std::cout << "  Freq Range: " << profile.filter.freq_heel << " Hz to " << profile.filter.freq_toe << " Hz\n";
    std::cout << "  Q Range: " << profile.filter.q_min << " to " << profile.filter.q_max << "\n";
    std::cout << "  Peak Gain: " << profile.filter.peak_gain_db << " dB\n";
    std::cout << "  Sweep Curve: ";
    switch (profile.sweep.curve) {
        case wah::SweepCurve::Linear: std::cout << "Linear\n"; break;
        case wah::SweepCurve::Log: std::cout << "Logarithmic\n"; break;
        case wah::SweepCurve::Custom: std::cout << "Custom LUT\n"; break;
    }
    std::cout << "  Dynamics (resonance compression):\n";
    std::cout << "    Low Level:  " << profile.dynamics.resonance_low << "\n";
    std::cout << "    Mid Level:  " << profile.dynamics.resonance_mid << "\n";
    std::cout << "    High Level: " << profile.dynamics.resonance_high << "\n";

    // Verify results
    bool success = true;

    if (profile.filter.freq_heel != heel_freq || profile.filter.freq_toe != toe_freq) {
        std::cout << "✗ Frequency range not preserved\n";
        success = false;
    }

    if (profile.sweep.curve != wah::SweepCurve::Log) {
        std::cout << "✗ Expected Log sweep curve\n";
        success = false;
    }

    // Dynamics should increase with level (low < mid < high)
    if (profile.dynamics.resonance_low >= profile.dynamics.resonance_mid ||
        profile.dynamics.resonance_mid >= profile.dynamics.resonance_high) {
        std::cout << "✗ Dynamics not increasing with level\n";
        success = false;
    }

    if (success) {
        std::cout << "\n✓ Test PASSED\n";
        return 0;
    } else {
        std::cout << "\n✗ Test FAILED\n";
        return 1;
    }
}
