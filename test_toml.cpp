/**
 * test_toml.cpp
 * Test TOML serialization and parsing.
 */

#include "libwah.h"
#include "wah_toml.h"
#include <iostream>

int main() {
    // Create a test profile
    wah::Profile original;
    original.meta.name = "Test Wah";
    original.meta.author = "claude";
    original.meta.make = "Vox";
    original.meta.model = "V847";
    original.meta.description = "A test profile for TOML serialization";
    original.meta.created = "2026-06-17T14:00:00Z";

    original.filter.topology = wah::Topology::Bandpass2Pole;
    original.filter.freq_heel = 450.0f;
    original.filter.freq_toe = 2100.0f;
    original.filter.q_min = 4.2f;
    original.filter.q_max = 4.8f;
    original.filter.peak_gain_db = 17.5f;
    original.filter.bass_shelf_db = -1.5f;
    original.filter.bass_shelf_freq = 200.0f;

    original.sweep.curve = wah::SweepCurve::Log;

    original.levels.input_gain_db = 0.0f;
    original.levels.output_gain_db = 0.0f;
    original.levels.mix = 1.0f;

    original.dynamics.resonance_low = 0.1f;
    original.dynamics.resonance_mid = 0.3f;
    original.dynamics.resonance_high = 0.5f;

    // Serialize to TOML
    std::string toml_text = wah::profile_to_toml(original);
    std::cout << "Serialized TOML:\n" << toml_text << "\n";

    // Parse back from TOML
    auto parsed = wah::toml_to_profile(toml_text);
    if (!parsed) {
        std::cout << "✗ Parsing failed\n";
        return 1;
    }

    const auto& restored = parsed.value();

    // Verify all fields
    bool success = true;

    if (restored.meta.name != original.meta.name) {
        std::cout << "✗ Name mismatch\n";
        success = false;
    }

    if (restored.filter.freq_heel != original.filter.freq_heel ||
        restored.filter.freq_toe != original.filter.freq_toe) {
        std::cout << "✗ Frequency range mismatch\n";
        success = false;
    }

    if (restored.filter.q_min != original.filter.q_min ||
        restored.filter.q_max != original.filter.q_max) {
        std::cout << "✗ Q range mismatch\n";
        success = false;
    }

    if (restored.filter.peak_gain_db != original.filter.peak_gain_db) {
        std::cout << "✗ Peak gain mismatch\n";
        success = false;
    }

    if (restored.sweep.curve != original.sweep.curve) {
        std::cout << "✗ Sweep curve mismatch\n";
        success = false;
    }

    if (restored.dynamics.resonance_low != original.dynamics.resonance_low ||
        restored.dynamics.resonance_mid != original.dynamics.resonance_mid ||
        restored.dynamics.resonance_high != original.dynamics.resonance_high) {
        std::cout << "✗ Dynamics mismatch\n";
        success = false;
    }

    if (success) {
        std::cout << "\n✓ Test PASSED: Round-trip serialization successful\n";
        return 0;
    } else {
        std::cout << "\n✗ Test FAILED\n";
        return 1;
    }
}
