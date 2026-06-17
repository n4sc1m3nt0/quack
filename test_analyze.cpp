/**
 * test_analyze.cpp
 * Simple test of the frequency analyzer.
 */

#include "libwah.h"
#include "wah_analyze.h"
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    // Generate a test signal: 1000 Hz sine wave at 48 kHz
    float sample_rate = 48000.0f;
    int num_samples = 4800;  // 0.1 second at 48 kHz
    float test_freq = 1000.0f;

    std::vector<float> buffer(num_samples);
    for (int i = 0; i < num_samples; i++) {
        float t = (float)i / sample_rate;
        buffer[i] = 0.5f * std::sin(2.0f * M_PI * test_freq * t);
    }

    // Analyze the buffer
    wah::FrequencyResponse response = wah::analyze(buffer.data(), num_samples, sample_rate);

    std::cout << "Frequency Analysis Results:\n";
    std::cout << "  Valid: " << (response.valid ? "yes" : "no") << "\n";
    std::cout << "  Center Frequency: " << response.center_freq << " Hz\n";
    std::cout << "  Q: " << response.q << "\n";
    std::cout << "  Peak Gain: " << response.peak_gain_db << " dB\n";

    if (response.valid && response.center_freq > 900.0f && response.center_freq < 1100.0f) {
        std::cout << "\n✓ Test PASSED: frequency within expected range\n";
        return 0;
    } else {
        std::cout << "\n✗ Test FAILED: frequency not in expected range\n";
        return 1;
    }
}
