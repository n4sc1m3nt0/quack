/**
 * test_capture.cpp
 * Test the capture workflow state machine.
 */

#include "libwah.h"
#include "wah_capture.h"
#include <cmath>
#include <iostream>
#include <vector>

/**
 * Generate synthetic wah audio at a specific pedal position.
 * Creates a sine wave at the center frequency with noise.
 */
std::vector<float> generate_wah_audio(float center_freq, float /*q*/, float sample_rate, float duration_sec) {
    int num_samples = (int)(duration_sec * sample_rate);
    std::vector<float> buffer(num_samples);

    // Create a sine wave at the center frequency mixed with noise
    for (int i = 0; i < num_samples; i++) {
        float t = (float)i / sample_rate;

        // Main component: sine wave at center frequency
        float sine = std::sin(2.0f * M_PI * center_freq * t);

        // Add some harmonics for realistic spectral content
        float harmonic2 = 0.3f * std::sin(2.0f * M_PI * center_freq * 2 * t);
        float harmonic3 = 0.15f * std::sin(2.0f * M_PI * center_freq * 0.5f * t);

        // Add a little noise
        float noise = 0.1f * (2.0f * (rand() / (float)RAND_MAX) - 1.0f);

        buffer[i] = (sine + harmonic2 + harmonic3 + noise) * 0.3f;
    }

    return buffer;
}

int main() {
    std::cout << "Testing capture workflow...\n\n";

    float sample_rate = 48000.0f;
    int buffer_size = 4800;

    // Track state changes
    wah::CaptureState last_state = wah::CaptureState::Idle;
    auto on_state_changed = [&](wah::CaptureState state) {
        if (state != last_state) {
            std::cout << "State: ";
            switch (state) {
                case wah::CaptureState::Idle: std::cout << "Idle"; break;
                case wah::CaptureState::SignalDetecting: std::cout << "SignalDetecting"; break;
                case wah::CaptureState::HeelCalibrationReady: std::cout << "HeelCalibrationReady"; break;
                case wah::CaptureState::HeelCapturing: std::cout << "HeelCapturing"; break;
                case wah::CaptureState::ToeCalibrationReady: std::cout << "ToeCalibrationReady"; break;
                case wah::CaptureState::ToeCapturing: std::cout << "ToeCapturing"; break;
                case wah::CaptureState::SweepStage1Ready: std::cout << "SweepStage1Ready"; break;
                case wah::CaptureState::SweepStage1Active: std::cout << "SweepStage1Active"; break;
                case wah::CaptureState::SweepStage2Ready: std::cout << "SweepStage2Ready"; break;
                case wah::CaptureState::SweepStage2Active: std::cout << "SweepStage2Active"; break;
                case wah::CaptureState::SweepStage3Ready: std::cout << "SweepStage3Ready"; break;
                case wah::CaptureState::SweepStage3Active: std::cout << "SweepStage3Active"; break;
                case wah::CaptureState::Fitting: std::cout << "Fitting"; break;
                case wah::CaptureState::Complete: std::cout << "Complete"; break;
                case wah::CaptureState::Error: std::cout << "Error"; break;
            }
            std::cout << "\n";
            last_state = state;
        }
    };

    wah::CaptureCallbacks callbacks;
    callbacks.on_state_changed = on_state_changed;
    callbacks.on_prompt = [](const std::string& msg) {
        std::cout << "  Prompt: " << msg << "\n";
    };
    callbacks.on_error = [](const std::string& msg) {
        std::cout << "  Error: " << msg << "\n";
    };
    callbacks.on_progress = [](float /*progress*/) {
        // Progress updates
    };

    wah::Capturer capturer(sample_rate, buffer_size, callbacks);

    // Simulate the capture process
    auto simulate_signal_input = [&](float center_freq, float q) {
        // Generate audio for this frequency
        auto audio = generate_wah_audio(center_freq, q, sample_rate, 3.5f);

        // Feed it to the capturer in chunks
        for (int i = 0; i < (int)audio.size(); i += buffer_size) {
            int chunk_size = std::min(buffer_size, (int)audio.size() - i);
            capturer.process_audio(audio.data() + i, chunk_size);
        }
    };

    // Start capture
    capturer.start();

    // Simulate signal detection
    std::cout << "\n--- Signal Detection ---\n";
    simulate_signal_input(1000.0f, 5.0f);  // Test tone

    // Wait for signal detected state
    if (capturer.get_state() != wah::CaptureState::HeelCalibrationReady) {
        std::cout << "✗ Signal detection failed\n";
        return 1;
    }

    // Heel calibration
    std::cout << "\n--- Heel Calibration ---\n";
    capturer.user_ready();

    // Feed signal three times (once per level)
    for (int i = 0; i < 3; i++) {
        simulate_signal_input(450.0f, 4.2f);  // Heel frequency
        if (capturer.get_state() == wah::CaptureState::Error) {
            std::cout << "✗ Heel calibration failed at level " << i << "\n";
            return 1;
        }
        if (capturer.get_state() == wah::CaptureState::ToeCalibrationReady) {
            break;
        }
    }

    // Toe calibration
    if (capturer.get_state() != wah::CaptureState::ToeCalibrationReady) {
        std::cout << "✗ Heel calibration didn't complete\n";
        return 1;
    }
    std::cout << "\n--- Toe Calibration ---\n";
    capturer.user_ready();

    // Feed signal three times (once per level)
    for (int i = 0; i < 3; i++) {
        simulate_signal_input(2100.0f, 4.8f);  // Toe frequency
        if (capturer.get_state() == wah::CaptureState::Error) {
            std::cout << "✗ Toe calibration failed at level " << i << "\n";
            return 1;
        }
        if (capturer.get_state() == wah::CaptureState::SweepStage1Ready) {
            break;
        }
    }

    // Sweep stages
    std::cout << "\n--- Sweep Stages ---\n";
    for (int stage = 0; stage < 3; stage++) {
        auto state = capturer.get_state();
        if (state == wah::CaptureState::SweepStage1Ready ||
            state == wah::CaptureState::SweepStage2Ready ||
            state == wah::CaptureState::SweepStage3Ready) {
            capturer.user_ready();

            // Simulate a sweep from heel to toe
            const int num_positions = 10;
            for (int pos = 0; pos < num_positions; pos++) {
                float position = (float)pos / (num_positions - 1);
                float log_heel = std::log(450.0f);
                float log_toe = std::log(2100.0f);
                float log_freq = log_heel + position * (log_toe - log_heel);
                float freq = std::exp(log_freq);
                float q = 4.2f + position * (4.8f - 4.2f);

                simulate_signal_input(freq, q);
            }
        }
    }

    // Check if capture completed
    if (capturer.get_state() != wah::CaptureState::Complete) {
        std::cout << "\n✗ Capture did not complete (state: ";
        switch (capturer.get_state()) {
            case wah::CaptureState::Error: std::cout << "Error"; break;
            default: std::cout << "Other"; break;
        }
        std::cout << ")\n";
        return 1;
    }

    // Get result
    auto result = capturer.get_result();
    if (!result) {
        std::cout << "\n✗ No result from capture\n";
        return 1;
    }

    const auto& profile = result.value().profile;

    std::cout << "\n--- Capture Result ---\n";
    std::cout << "Topology: " << (profile.filter.topology == wah::Topology::Bandpass2Pole ? "2-pole" : "4-pole") << "\n";
    std::cout << "Freq Range: " << profile.filter.freq_heel << " Hz to " << profile.filter.freq_toe << " Hz\n";
    std::cout << "Q Range: " << profile.filter.q_min << " to " << profile.filter.q_max << "\n";
    std::cout << "Peak Gain: " << profile.filter.peak_gain_db << " dB\n";
    std::cout << "Samples Collected: " << capturer.get_samples().size() << "\n";
    std::cout << "Signal Quality: " << result.value().signal_quality << "\n";

    std::cout << "\n✓ Capture workflow test PASSED\n";
    return 0;
}
