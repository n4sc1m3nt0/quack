/**
 * wah_capture.cpp
 * Capture workflow orchestrator implementation.
 */

#include "wah_capture.h"
#include "wah_analyze.h"
#include "wah_fit.h"
#include <cmath>
#include <algorithm>

namespace wah {

// Input levels for the three capture stages (in dBu)
static constexpr float INPUT_LEVEL_LOW = -20.0f;
static constexpr float INPUT_LEVEL_MID = -10.0f;
static constexpr float INPUT_LEVEL_HIGH = 0.0f;

// Capture buffer size (in seconds)
static constexpr float CAPTURE_DURATION_SEC = 3.5f;

// Signal detection threshold (RMS in dB relative to full scale)
static constexpr float SIGNAL_DETECT_THRESHOLD_DB = -60.0f;

Capturer::Capturer(float sample_rate, int buffer_size, const CaptureCallbacks& callbacks)
    : sample_rate_(sample_rate), buffer_size_(buffer_size), callbacks_(callbacks) {
    // Pre-allocate capture buffer
    int capture_buffer_samples = (int)(CAPTURE_DURATION_SEC * sample_rate);
    current_capture_buffer_.resize(capture_buffer_samples);
}

void Capturer::start() {
    state_ = CaptureState::SignalDetecting;
    samples_.clear();
    calibration_level_index_ = 0;
    sweep_stage_index_ = 0;
    if (callbacks_.on_state_changed) callbacks_.on_state_changed(state_);
    if (callbacks_.on_prompt) callbacks_.on_prompt("Detecting input signal...");
}

void Capturer::cancel() {
    state_ = CaptureState::Idle;
    samples_.clear();
    result_ = {};
    if (callbacks_.on_state_changed) callbacks_.on_state_changed(state_);
}

void Capturer::set_state(CaptureState new_state) {
    state_ = new_state;
    if (callbacks_.on_state_changed) callbacks_.on_state_changed(state_);
}

float Capturer::detect_signal_level(const float* buffer, int num_samples) {
    // Compute RMS level in dB
    float sum_sq = 0.0f;
    for (int i = 0; i < num_samples; i++) {
        sum_sq += buffer[i] * buffer[i];
    }
    float rms = std::sqrt(sum_sq / num_samples);

    // Convert to dB (relative to 1.0 = full scale)
    float db = (rms > 1e-10f) ? 20.0f * std::log10(rms) : -120.0f;
    return db;
}

void Capturer::process_audio(const float* buffer, int num_samples) {
    if (state_ == CaptureState::Idle || state_ == CaptureState::Error || state_ == CaptureState::Complete) {
        return;
    }

    // Accumulate audio into the current capture buffer
    int available = (int)current_capture_buffer_.size() - current_capture_index_;
    int to_copy = std::min(num_samples, available);

    if (to_copy > 0) {
        std::copy(buffer, buffer + to_copy, current_capture_buffer_.begin() + current_capture_index_);
        current_capture_index_ += to_copy;
    }

    // Update progress
    float progress = (float)current_capture_index_ / current_capture_buffer_.size();
    if (callbacks_.on_progress) callbacks_.on_progress(progress);

    // Check if buffer is full
    if (current_capture_index_ >= (int)current_capture_buffer_.size()) {
        switch (state_) {
            case CaptureState::SignalDetecting:
                check_signal();
                break;
            case CaptureState::HeelCapturing:
                capture_heel_level();
                break;
            case CaptureState::ToeCapturing:
                capture_toe_level();
                break;
            case CaptureState::SweepStage1Active:
            case CaptureState::SweepStage2Active:
            case CaptureState::SweepStage3Active:
                capture_sweep_data();
                break;
            default:
                break;
        }
    }
}

void Capturer::check_signal() {
    float signal_db = detect_signal_level(current_capture_buffer_.data(), current_capture_buffer_.size());

    if (signal_db > SIGNAL_DETECT_THRESHOLD_DB) {
        // Signal detected
        set_state(CaptureState::HeelCalibrationReady);
        if (callbacks_.on_prompt) {
            callbacks_.on_prompt("Signal detected! Place your pedal in the heel position and click Ready.");
        }
    } else {
        // No signal
        set_state(CaptureState::Error);
        if (callbacks_.on_error) {
            callbacks_.on_error("No signal detected. Check your connections.");
        }
    }
}

void Capturer::user_ready() {
    switch (state_) {
        case CaptureState::HeelCalibrationReady:
            start_heel_capture();
            break;
        case CaptureState::ToeCalibrationReady:
            start_toe_capture();
            break;
        case CaptureState::SweepStage1Ready:
        case CaptureState::SweepStage2Ready:
        case CaptureState::SweepStage3Ready:
            start_sweep_stage();
            break;
        default:
            break;
    }
}

void Capturer::start_heel_capture() {
    calibration_level_index_ = 0;
    current_input_level_dbu_ = INPUT_LEVEL_LOW;
    current_capture_index_ = 0;
    set_state(CaptureState::HeelCapturing);
    if (callbacks_.on_prompt) {
        callbacks_.on_prompt("Capturing heel position at level 1 of 3...");
    }
}

void Capturer::capture_heel_level() {
    // Analyze the captured buffer
    FrequencyResponse response = analyze(current_capture_buffer_.data(),
                                         (int)current_capture_buffer_.size(),
                                         sample_rate_);

    if (!response.valid) {
        set_state(CaptureState::Error);
        if (callbacks_.on_error) {
            callbacks_.on_error("Failed to analyze heel position. Retake the capture.");
        }
        return;
    }

    // Store heel calibration data (use first level as the reference)
    if (calibration_level_index_ == 0) {
        heel_freq_ = response.center_freq;
        heel_q_ = response.q;
        heel_gain_db_ = response.peak_gain_db;
    }

    // Advance to next level
    advance_heel_level();
}

void Capturer::advance_heel_level() {
    calibration_level_index_++;

    if (calibration_level_index_ < 3) {
        // Next level
        static constexpr float levels[] = {INPUT_LEVEL_LOW, INPUT_LEVEL_MID, INPUT_LEVEL_HIGH};
        current_input_level_dbu_ = levels[calibration_level_index_];
        current_capture_index_ = 0;

        int level_num = calibration_level_index_ + 1;
        if (callbacks_.on_prompt) {
            char buf[100];
            snprintf(buf, sizeof(buf), "Capturing heel position at level %d of 3...", level_num);
            callbacks_.on_prompt(buf);
        }
    } else {
        // Heel capture complete
        set_state(CaptureState::ToeCalibrationReady);
        if (callbacks_.on_prompt) {
            callbacks_.on_prompt("Heel calibration complete! Now move to toe position and click Ready.");
        }
    }
}

void Capturer::start_toe_capture() {
    calibration_level_index_ = 0;
    current_input_level_dbu_ = INPUT_LEVEL_LOW;
    current_capture_index_ = 0;
    set_state(CaptureState::ToeCapturing);
    if (callbacks_.on_prompt) {
        callbacks_.on_prompt("Capturing toe position at level 1 of 3...");
    }
}

void Capturer::capture_toe_level() {
    // Analyze the captured buffer
    FrequencyResponse response = analyze(current_capture_buffer_.data(),
                                         (int)current_capture_buffer_.size(),
                                         sample_rate_);

    if (!response.valid) {
        set_state(CaptureState::Error);
        if (callbacks_.on_error) {
            callbacks_.on_error("Failed to analyze toe position. Retake the capture.");
        }
        return;
    }

    // Store toe calibration data (use first level as the reference)
    if (calibration_level_index_ == 0) {
        toe_freq_ = response.center_freq;
        toe_q_ = response.q;
        toe_gain_db_ = response.peak_gain_db;
    }

    // Advance to next level
    advance_toe_level();
}

void Capturer::advance_toe_level() {
    calibration_level_index_++;

    if (calibration_level_index_ < 3) {
        // Next level
        static constexpr float levels[] = {INPUT_LEVEL_LOW, INPUT_LEVEL_MID, INPUT_LEVEL_HIGH};
        current_input_level_dbu_ = levels[calibration_level_index_];
        current_capture_index_ = 0;

        int level_num = calibration_level_index_ + 1;
        if (callbacks_.on_prompt) {
            char buf[100];
            snprintf(buf, sizeof(buf), "Capturing toe position at level %d of 3...", level_num);
            callbacks_.on_prompt(buf);
        }
    } else {
        // Toe capture complete, start sweeps
        sweep_stage_index_ = 0;
        set_state(CaptureState::SweepStage1Ready);
        if (callbacks_.on_prompt) {
            callbacks_.on_prompt("Stage 1 of 3: Rock the pedal slowly following the guide. Click Ready when ready.");
        }
    }
}

void Capturer::start_sweep_stage() {
    current_capture_index_ = 0;

    static constexpr float levels[] = {INPUT_LEVEL_LOW, INPUT_LEVEL_MID, INPUT_LEVEL_HIGH};
    current_input_level_dbu_ = levels[sweep_stage_index_];

    // Switch to active state
    switch (sweep_stage_index_) {
        case 0:
            set_state(CaptureState::SweepStage1Active);
            break;
        case 1:
            set_state(CaptureState::SweepStage2Active);
            break;
        case 2:
            set_state(CaptureState::SweepStage3Active);
            break;
    }

    if (callbacks_.on_prompt) {
        char buf[100];
        snprintf(buf, sizeof(buf), "Stage %d of 3: Recording (%.1f seconds)...", sweep_stage_index_ + 1, CAPTURE_DURATION_SEC);
        callbacks_.on_prompt(buf);
    }
}

void Capturer::capture_sweep_data() {
    // Analyze the entire sweep buffer to extract multiple measurement points
    // We'll divide the buffer into windows and analyze each to track frequency over time
    int window_size = (int)(0.5f * sample_rate_);  // 500 ms windows with overlap
    int window_stride = (int)(0.25f * sample_rate_);  // 25% overlap

    for (int i = 0; i + window_size <= (int)current_capture_buffer_.size(); i += window_stride) {
        FrequencyResponse response = analyze(
            current_capture_buffer_.data() + i,
            window_size,
            sample_rate_
        );

        if (response.valid) {
            // Infer pedal position from the measured center frequency
            float position = infer_position(response.center_freq, heel_freq_, toe_freq_);

            samples_.push_back(MeasurementSample{
                position,
                response.center_freq,
                response.q,
                response.peak_gain_db,
                current_input_level_dbu_
            });
        }
    }

    // Update UI with samples
    if (callbacks_.on_samples_updated) {
        callbacks_.on_samples_updated(samples_);
    }

    // Advance to next sweep stage
    sweep_stage_index_++;

    if (sweep_stage_index_ < 3) {
        switch (sweep_stage_index_) {
            case 1:
                set_state(CaptureState::SweepStage2Ready);
                if (callbacks_.on_prompt) {
                    callbacks_.on_prompt("Stage 2 of 3: Rock the pedal slowly. Click Ready.");
                }
                break;
            case 2:
                set_state(CaptureState::SweepStage3Ready);
                if (callbacks_.on_prompt) {
                    callbacks_.on_prompt("Stage 3 of 3: Rock the pedal slowly. Click Ready.");
                }
                break;
        }
    } else {
        finish_all_sweeps();
    }
}

void Capturer::finish_all_sweeps() {
    set_state(CaptureState::Fitting);
    if (callbacks_.on_prompt) {
        callbacks_.on_prompt("Fitting curve to measurements...");
    }
    run_fitting();
}

void Capturer::run_fitting() {
    if (samples_.empty()) {
        set_state(CaptureState::Error);
        if (callbacks_.on_error) {
            callbacks_.on_error("No measurements collected. Retake the capture.");
        }
        return;
    }

    // Run curve fitting
    auto profile_opt = fit_profile(samples_, heel_freq_, toe_freq_, heel_q_, toe_q_,
                                    heel_gain_db_, toe_gain_db_);

    if (!profile_opt) {
        set_state(CaptureState::Error);
        if (callbacks_.on_error) {
            callbacks_.on_error("Curve fitting failed. Retake the capture.");
        }
        return;
    }

    auto profile = profile_opt.value();

    // Estimate signal quality
    estimate_signal_quality();

    // Store result and go to complete state
    CaptureResult result;
    result.profile = profile;
    result_= result;

    set_state(CaptureState::Complete);
    if (callbacks_.on_prompt) {
        callbacks_.on_prompt("Capture complete! Enter profile details and export.");
    }
}

void Capturer::estimate_signal_quality() {
    // Estimate based on how well the measurements fit the model
    // For now, use a simple heuristic: count valid measurements
    if (samples_.empty()) {
        if (result_) result_.value().signal_quality = 0.0f;
        return;
    }

    float quality = std::min(1.0f, (float)samples_.size() / 100.0f);
    if (result_) result_.value().signal_quality = quality;
}

std::optional<CaptureResult> Capturer::get_result() const {
    if (state_ == CaptureState::Complete) {
        return result_;
    }
    return {};
}

}  // namespace wah
