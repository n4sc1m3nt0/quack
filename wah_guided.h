/**
 * wah_guided.h
 * The guided capture (D13), built on the measurement functions of wah_measure.h (M5, round 21).
 * It replaces the old heel/toe three-stage Capturer of wah_capture.h, which stays until this
 * module can do its work; the two are not included together by any file. No I/O.
 *
 * A pass of the guide: a hold at the heel, a rise, a hold at the toe, a fall (back to the heel,
 * where the next pass starts). Times are seconds from the start of the pass.
 */

#pragma once

#include "libwah.h"
#include "wah_measure.h"

namespace wah {

struct GuideSchedule {
    float heel_hold_s = 1.5f;  // D32: holds of at least 1 s
    float rise_s = 1.0f;
    float toe_hold_s = 1.5f;
    float fall_s = 1.0f;

    /** All four are finite, the holds at least 1 s (D32), the rise and the fall above 0. */
    bool  valid() const;
    /** Length of a pass; 0 if the schedule is not valid. */
    float period() const;
    /** Guide position at time t of a pass: 0 at the heel, 1 at the toe, linear in time during the rise and the fall (D33).
     *  -1 (outside 0..1, so aggregate_passes ignores it) if t is outside [0, period) or the schedule is not valid. */
    float position_at(float t) const;
    /** The hold at the heel, [0, heel_hold_s), and the hold at the toe, after the rise; set start = end = 0 if not valid. */
    void  heel_hold(float& start_s, float& end_s) const;
    void  toe_hold(float& start_s, float& end_s) const;
};

/** The two holds of one pass, each with the shape fit_hold_shape() found. */
struct PassHolds {
    HoldShape heel, toe;
    bool valid = false;  // the schedule is valid and both holds are
};

/**
 * Measure the heel and the toe hold of one pass (one recording that starts with the pass): the
 * intervals come from the schedule, the rest is fit_hold_shape() (loopback latency and division, 0.5 s
 * windows of segment 4096, 0.2 s of settling: D39, D40; the 5 % movement rule: D43). Each hold is
 * refused on its own account, so a pass can have a good heel and a refused toe (valid = false).
 * valid is false for a schedule that is not valid, a null buffer, or a hold the measurement refuses.
 */
PassHolds measure_pass_holds(const GuideSchedule& schedule, const float* stimulus, const float* dut, const float* loopback,
                             int num_samples, float sample_rate, int max_delay,
                             float window_s = 0.5f, int segment_size = 4096, float settle_s = 0.2f,
                             float freq_min = 20.0f, float freq_max = 5000.0f, float min_coherence = 0.5f);

/**
 * The hold as the HoldEstimate that aggregate_holds() takes. Only a 2-pole or 2-pole-with-shelf shape has
 * the centre, Q and gain of one band-pass; a 4-pole or a Measured shape gives an invalid estimate (and so a
 * hold that aggregate_holds() lists as dropped). windows_used is windows_checked. Invalid if the hold is not.
 */
HoldEstimate hold_estimate_of(const HoldShape& hold);

/**
 * The points of one pass for aggregate_passes() (M5, D32, D33, D40): each TrackPoint of track_bandpass()
 * becomes a PassPoint whose position is the guide's position (schedule.position_at) at the centre of its
 * window and whose weight is 1 (a TrackPoint holds no coherence). Dropped: a window whose estimate is not
 * valid or whose centre is not positive and finite; a window that starts before settle_s (D40) or ends after
 * the pass; a window that straddles a corner of the schedule (the start or the end of the rise or of the
 * fall: a turnaround window, D32), that is, whose start is before the corner and whose end is after it.
 * window_s is the window of the tracker (the default of track_bandpass, 0.25 s). The order is kept.
 * Empty if the schedule is not valid, window_s <= 0 or settle_s < 0 (or not finite).
 */
std::vector<PassPoint> pass_points(const GuideSchedule& schedule, const std::vector<TrackPoint>& tracks,
                                   float window_s = 0.25f, float settle_s = 0.2f);

/**
 * The same as pass_points(), for the windows of track_shape() (round 28, D65): the centre of a window is that of its
 * band-pass: the centre of a 2-pole, of a 2-pole with shelf, the lower centre of a 4-pole. A window that is not valid,
 * or whose shape is Measured or None (no centre), is dropped; the other rules (the settling, the end of the pass, the
 * turnarounds, the arguments) are those of pass_points().
 */
std::vector<PassPoint> shape_pass_points(const GuideSchedule& schedule, const std::vector<ShapeTrackPoint>& tracks,
                                         float window_s = 0.25f, float settle_s = 0.2f);

/** Everything one pass of the guide gives: the points of its curve and its two holds. */
struct PassAnalysis {
    std::vector<PassPoint> points;  // pass_points() of the tracker's windows; may be empty
    PassHolds holds;                // measure_pass_holds(); a refused hold does not invalidate the points
    bool valid = false;             // the schedule is valid and the tracker ran (the loopback latency was found, the buffers are usable)
};

/**
 * Analyse one pass (M5, D62, D63): track_shape() (window 0.25 s, segment 2048: D32; round 28, D65) turned into points by
 * shape_pass_points(), and measure_pass_holds() (window 0.5 s, segment 4096, settling 0.2 s: D39, D40), on the same
 * recordings. One recording holds one pass. A pass whose holds are refused still gives its points (valid
 * stays true, holds.valid is false); the aggregation drops what disagrees. valid is false, with no points
 * and no holds, for an invalid schedule, a null buffer, or a loopback whose latency is not found.
 */
PassAnalysis analyse_pass(const GuideSchedule& schedule, const float* stimulus, const float* dut, const float* loopback,
                          int num_samples, float sample_rate, int max_delay,
                          float freq_min = 20.0f, float freq_max = 5000.0f, float min_coherence = 0.5f);

/**
 * Where a capture stands after the passes analysed so far: the points of every pass and the heel and toe holds
 * (hold_estimate_of; a refused hold is an invalid estimate, which aggregate_holds lists as dropped) go into
 * capture_progress(). Every analysis counts as a pass recorded, a not valid one with no points (the
 * aggregation drops it).
 */
CaptureProgress capture_progress_of(const std::vector<PassAnalysis>& passes, CaptureTarget target, int num_points = 11);

/** The shape of one end of the sweep (heel or toe), from the holds of all the passes (M5, round 25). */
struct ShapeSummary {
    ShapeModel model = ShapeModel::None;  // BandPass2, BandPass2Shelf or Measured: the model most of the usable holds agree on
    float center_freq = 0, q = 0, peak_gain_db = 0;  // median over the kept holds (BandPass2 and BandPass2Shelf)
    float shelf_freq_hz = 0, shelf_gain_db = 0;      // median over the kept holds (BandPass2Shelf)
    std::vector<float> table_freq_hz;                // Measured: the grid of the kept tables (kShapeTablePoints cells)
    std::vector<float> table_db;                     // Measured: the median in dB of each cell over the kept tables
    int kept = 0;                                    // holds the medians come from
    std::vector<int> dropped;                        // indices in the input of the holds left out
    bool valid = false;
};

/**
 * Fold the shapes of the holds of one end over the passes (M5, round 25). A hold is usable if it is valid, its
 * shape is valid and is a 2-pole, a 2-pole with shelf, or a Measured table of kShapeTablePoints cells, and all
 * its numbers are finite (centre and Q above 0). A 4-pole, a refused hold, a shape of None and a malformed one
 * are not usable and are listed in dropped (a 4-pole has no rule yet for ordering two centres across passes).
 * The model is the one that more than half of the usable holds have; the holds of another model are dropped too.
 * With no such majority (two models of equal count, three models) nothing is valid and every hold is listed as dropped. The result is the median
 * (the mean of the two middle values for an even number) of each parameter over the kept holds; for Measured, the
 * median of each cell, over the tables whose grid is that of the first kept table within 0.1 % (the others are dropped).
 * A single usable hold is a valid summary with kept 1; no error is given (the holds of the same model can be compared
 * by aggregate_holds for the band-pass).
 */
ShapeSummary aggregate_shapes(const std::vector<HoldShape>& holds);

/** The end of a capture as one value (M5, round 24): what the passes say, with no profile yet (the format v1.1 is M6, D6). */
struct CaptureSummary {
    AggregatedCurve curve;   // aggregate_passes() of the points of all the passes (dropped lists the passes left out)
    HoldsAggregate  holds;   // aggregate_holds() of the heel holds and of the toe holds (dropped lists the holds left out; indices are those of the passes)
    CaptureProgress progress;  // capture_progress_of() of the same passes
    ShapeSummary heel_shape, toe_shape;  // aggregate_shapes() of the heel holds and of the toe holds (round 26); they do not count for "done" (D49, D51): a shape of another model has no error to compare with a target
    bool valid = false;      // the progress and the curve are valid: passes is not empty, num_points >= 2 and some pass has data
};

/**
 * Fold the analysed passes of a capture into one summary: the curve, the holds at the heel and the toe, and where the
 * capture stands, all from the same vector (the indices in curve.dropped and in holds.heel.dropped / holds.toe.dropped
 * are the positions in passes). A pass that was not analysed is dropped from the curve and its holds are listed as dropped.
 * The holds may be invalid (holds.heel.valid false) while the curve is valid: a capture whose holds were all refused has
 * a curve but no Q or gain.
 */
CaptureSummary capture_summary(const std::vector<PassAnalysis>& passes, CaptureTarget target, int num_points = 11);

/**
 * A Profile with its [curves] from a capture summary (M6, round 30, D66). No I/O and no clock: the caller gives the meta
 * (name, created ...), which is copied as it is.
 *
 * [curves]: one entry per position of curve.center_freq that has data (centre above 0). A position with no data (centre
 * exactly 0) is left out, so the file says only what was measured; the heel (first) and the toe (last) positions must have
 * data. q and gain_db are the two values (heel, toe) of the holds when both holds are usable (valid, q above 0, finite
 * numbers), and are left out otherwise; they are never copied to every position (D32).
 * [shape_heel] and [shape_toe]: when the shapes of both ends are valid and Measured, their tables (table_freq_hz and table_db) as they are
 * (round 34, D71); topology and shelf stay at the defaults, as there is no band-pass to read them from.
 * [filter]: freq_heel and freq_toe are the first and the last freq of the curves. With both holds usable, q_min and q_max
 * are the lower and the higher of the two Q and peak_gain_db is the mean of the two gains; otherwise the defaults of
 * Filter stay. Topology from the shapes that are valid: 2-pole and 2-pole with shelf give Bandpass2Pole (the shelf also
 * gives bass_shelf_db and bass_shelf_freq, the mean of the ends that have it, written only if it is inside the ranges of the spec,
 * -6 to +3 dB and 100 to 400 Hz: round 31, D68; otherwise the defaults of Filter stay), 4-pole gives Bandpass4Pole; no valid shape
 * leaves the default topology.
 *
 * nullopt (no profile): the summary or its curve is not valid; the curve is malformed (sizes differ, the first position is
 * not 0 or the last is not 1, positions that do not rise, a centre that is negative or not finite); the heel or the toe
 * has no centre; more than MAX_CURVE_POINTS positions have data; one valid shape is Measured and the other end is not (both or
 * none: D71); two valid shapes of different models; two Measured shapes with a table that check_shape refuses; a shelf with numbers that are not finite or a corner not above 0.
 */
std::optional<Profile> profile_from_summary(const CaptureSummary& summary, const Meta& meta);

/** The measurement chain between the pedal and the interface: a latency and a one-pole high-pass (the "DI box" of the tests). */
struct MeasurementChain {
    int   latency_samples = 3000;   // >= 0
    float highpass_hz = 1500.0f;    // 0 = none; otherwise above 0 and below the Nyquist frequency
};

/** The two recordings of one pass of a virtual pedal. */
struct SimulatedPass {
    std::vector<float> dut;       // the stimulus through the engine, then through the chain
    std::vector<float> loopback;  // the stimulus through the chain alone
    bool valid = false;
};

/**
 * The virtual pedal (D10, M5 round 27): the Engine of libwah.h as the device under test, for tests and demos without
 * hardware. One pass of the guide drives the engine: at every block of block_size samples set_position() gets the
 * guide's position at the middle of the block (the heel, 0, once the pass is over). The stimulus goes through the
 * engine, and through the chain, to give `dut`; through the chain alone to give `loopback`. Both have the length of the
 * stimulus, so they are what analyse_pass() takes. No I/O. The engine recomputes its coefficients only when the position
 * has moved by more than POSITION_THRESHOLD (0.002) from the last one it used.
 *
 * valid is false (and the buffers are empty) if the schedule is not valid, the stimulus is empty, sample_rate is not
 * above 0, block_size < 1, or the chain has a negative latency or a high-pass that is negative, NaN or at or above the
 * Nyquist frequency.
 */
SimulatedPass simulate_pass(const GuideSchedule& schedule, const Profile& profile, const std::vector<float>& stimulus,
                            float sample_rate, const MeasurementChain& chain = MeasurementChain(), int block_size = 64);

} // namespace wah
