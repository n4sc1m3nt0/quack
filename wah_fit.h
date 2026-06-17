/**
 * wah_fit.h
 * Levenberg-Marquardt curve fitting for profile extraction.
 *
 * Takes measurement samples from the capture and fits them to a
 * parametric filter model to extract the final Profile.
 */

#pragma once

#include "libwah.h"
#include <vector>
#include <optional>

namespace wah {

/**
 * Options for the fitting algorithm.
 */
struct FitOptions {
    int max_iterations = 100;
    float tolerance = 1e-6f;
    float lambda_initial = 0.01f;
    float lambda_up_factor = 10.0f;
    float lambda_down_factor = 0.1f;
};

/**
 * Fit a Profile from a set of measurement samples using Levenberg-Marquardt.
 *
 * Algorithm:
 * 1. Use heel/toe calibration as anchors
 * 2. Fit center frequency vs position to log, linear, or custom curve
 * 3. Fit Q and peak_gain as functions of position
 * 4. Fit both 2-pole and 4-pole topologies, choose better fit
 * 5. Extract dynamics from 3-level measurements
 * 6. Fit bass shelf (optional, if significant)
 *
 * @param samples       Measurement samples from sweep stages
 * @param heel_freq     Center frequency at heel (from calibration)
 * @param toe_freq      Center frequency at toe (from calibration)
 * @param heel_q        Q at heel
 * @param toe_q         Q at toe
 * @param heel_gain_db  Peak gain at heel
 * @param toe_gain_db   Peak gain at toe
 * @param options       Algorithm options
 *
 * @return Profile if fitting succeeded, nullopt on error
 */
std::optional<Profile> fit_profile(
    const std::vector<MeasurementSample>& samples,
    float heel_freq,
    float toe_freq,
    float heel_q,
    float toe_q,
    float heel_gain_db,
    float toe_gain_db,
    const FitOptions& options = FitOptions{});

} // namespace wah
