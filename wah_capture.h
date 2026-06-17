/**
 * wah_capture.h
 * Capture workflow orchestrator and state machine.
 *
 * Manages the complete capture process:
 * - Signal detection
 * - Heel/toe calibration
 * - Three sweep stages
 * - Profile fitting
 */

#pragma once

#include "libwah.h"
#include <vector>
#include <functional>
#include <optional>

namespace wah {

/**
 * Capture state machine.
 */
enum class CaptureState {
    Idle,                   // Waiting to start
    SignalDetecting,        // Testing for audio input
    HeelCalibrationReady,   // Waiting for user to place pedal at heel
    HeelCapturing,          // Measuring heel position at 3 levels
    ToeCalibrationReady,    // Waiting for user to move to toe
    ToeCapturing,           // Measuring toe position at 3 levels
    SweepStage1Ready,       // Ready to start sweep (quiet level)
    SweepStage1Active,      // User rocking pedal (~15 seconds)
    SweepStage2Ready,       // Ready for medium level sweep
    SweepStage2Active,
    SweepStage3Ready,       // Ready for hot level sweep
    SweepStage3Active,
    Fitting,                // Running curve fitting
    Complete,               // Capture finished, ready to export
    Error                   // Capture failed
};

struct CaptureResult {
    Profile profile;
    float signal_quality;  // 0.0–1.0, how confident we are in the measurement
};

/**
 * Callback interface for UI updates during capture.
 */
struct CaptureCallbacks {
    // Called when state changes
    std::function<void(CaptureState)> on_state_changed;

    // Called with progress (0.0–1.0)
    std::function<void(float)> on_progress;

    // Called with user prompt
    std::function<void(const std::string&)> on_prompt;

    // Called with error message
    std::function<void(const std::string&)> on_error;

    // Called with current measurements
    std::function<void(const std::vector<MeasurementSample>&)> on_samples_updated;
};

/**
 * Main capture orchestrator.
 *
 * Usage:
 *   Capturer capturer(sample_rate, buffer_size, callbacks);
 *   capturer.start();
 *   while (capturer.get_state() != CaptureState::Complete) {
 *       // Feed audio into capturer
 *       capturer.process_audio(buffer, num_samples);
 *       // User interacts via UI (e.g., click "Ready" button)
 *       // capturer.user_ready();
 *   }
 *   auto result = capturer.get_result();
 */
class Capturer {
public:
    /**
     * Initialize the capturer.
     * @param sample_rate  Audio sample rate (44100, 48000, or 96000 Hz)
     * @param buffer_size  Size of audio buffers
     * @param callbacks    UI callback functions
     */
    Capturer(float sample_rate, int buffer_size, const CaptureCallbacks& callbacks = CaptureCallbacks{});

    /**
     * Start the capture process.
     */
    void start();

    /**
     * Process incoming audio.
     * Should be called in real-time from the audio thread.
     */
    void process_audio(const float* buffer, int num_samples);

    /**
     * Signal that the user is ready to proceed to the next stage.
     * Called when user clicks "Ready" button (for calibration and sweep stages).
     */
    void user_ready();

    /**
     * Get the current state.
     */
    CaptureState get_state() const { return state_; }

    /**
     * Get all collected measurement samples so far.
     */
    const std::vector<MeasurementSample>& get_samples() const { return samples_; }

    /**
     * Get the capture result (only valid when state == Complete).
     */
    std::optional<CaptureResult> get_result() const;

    /**
     * Cancel the capture and reset to idle.
     */
    void cancel();

private:
    float sample_rate_;
    int buffer_size_;
    CaptureState state_ = CaptureState::Idle;
    CaptureCallbacks callbacks_;

    // Calibration data
    float heel_freq_ = 0.0f;
    float heel_q_ = 0.0f;
    float heel_gain_db_ = 0.0f;
    float toe_freq_ = 0.0f;
    float toe_q_ = 0.0f;
    float toe_gain_db_ = 0.0f;

    // Measurement buffer (audio for current capture stage)
    std::vector<float> current_capture_buffer_;
    int current_capture_index_ = 0;

    // Collected measurement samples from all stages
    std::vector<MeasurementSample> samples_;

    // Current input level being measured (for dynamics extraction)
    float current_input_level_dbu_ = 0.0f;

    // Calibration stage counter (0, 1, 2 for three input levels)
    int calibration_level_index_ = 0;

    // Sweep stage counter (0, 1, 2)
    int sweep_stage_index_ = 0;

    // Fitted profile (after fitting is complete)
    std::optional<CaptureResult> result_;

    // Helper functions
    void set_state(CaptureState new_state);
    void check_signal();
    void start_heel_capture();
    void capture_heel_level();
    void advance_heel_level();
    void start_toe_capture();
    void capture_toe_level();
    void advance_toe_level();
    void start_sweep_stage();
    void capture_sweep_data();
    void finish_all_sweeps();
    void run_fitting();
    void estimate_signal_quality();

    // Signal detection at a specific level
    float detect_signal_level(const float* buffer, int num_samples);
};

} // namespace wah
