/**
 * test_guided.cpp
 * Round 21: the guided capture (wah_guided.h). The schedule of a pass (position and hold intervals),
 * the measurement of the heel and toe holds of a pass behind a latency and a tilting chain, and the
 * conversion of a hold into the estimate that aggregate_holds() takes.
 *
 * The pedal is the one of test_measure.cpp, group 15 and 22: Q 5, +6 dB, centre 400 Hz until 1.5 s, rising on a
 * straight line in log frequency to 1600 Hz by 2.5 s, holding until 4 s, falling back by 5 s. The
 * reference is the pedal's own parameters, not the code under test.
 *
 * Round 22: pass_points() turns the windows of track_bandpass() into the points of a pass: the guide's position
 * at the window time, without the windows of the settling, of the turnarounds, outside the pass or not valid.
 *
 * Round 23: analyse_pass() gives the points and the holds of one pass in one call, keeps the points of a pass whose
 * holds are refused, and refuses what it cannot measure; capture_progress_of() feeds them to capture_progress().
 *
 * Round 24: capture_summary() folds the analysed passes into the curve, the holds at the heel and the toe, and the
 * progress, from one vector, with the indices of what was dropped.
 *
 * Round 25: aggregate_shapes() folds the shapes of the holds of one end over the passes: the model most of them agree on,
 * the median of its parameters (or of each cell of a Measured table), and the indices of what was left out.
 *
 * Round 26: capture_summary() also gives the shape of the heel and of the toe (aggregate_shapes), with the same indices,
 * and the shapes do not decide when the capture is done.
 *
 * Round 27: simulate_pass() is the virtual pedal (D10): the engine as the device under test behind a latency and
 * a high-pass, with a loopback of the chain alone; its buffers are what analyse_pass() takes. The reference of
 * the chain is the test's own high-pass; the dry profile (mix 0) must give the loopback exactly.
 *
 * Round 30: profile_from_summary() turns a CaptureSummary into a Profile with its [curves] (M6, D66): the positions with data, the two
 * hold values of q and gain_db, the ends of the v1 [filter], the topology and the shelf from the shapes; the cases that give no profile;
 * and the reader of wah_toml.cpp reads back everything it gives.
 *
 * Round 31 (D68): a shelf outside the ranges of the spec is not written (the Filter keeps its defaults); 37t is changed for it.
 *
 * Exit codes: 0 all pass, 1 at least one check fails.
 */

#include "wah_guided.h"
#include "wah_toml.h"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <limits>
#include <vector>

namespace {

const double kPi = 3.14159265358979323846;
const double kFs = 48000.0;
int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "ok   " : "FAIL ") << what << "\n";
    if (!ok) failures++;
}

// One-pole high-pass (bilinear): the "DI box" of the test chain.
std::vector<double> highpass1(double fc, const std::vector<double>& x) {
    const double k = std::tan(kPi * fc / kFs), b0 = 1.0 / (1.0 + k), a1 = (k - 1.0) / (k + 1.0);
    std::vector<double> y(x.size());
    double x1 = 0, y1 = 0;
    for (size_t i = 0; i < x.size(); i++) { y[i] = b0 * x[i] - b0 * x1 - a1 * y1; x1 = x[i]; y1 = y[i]; }
    return y;
}

}  // namespace

int main() {
    const wah::GuideSchedule def;

    // 1. The schedule.
    {
        check(def.valid() && std::abs(def.period() - 5.0f) < 1e-6f, "23a the default schedule is valid and a pass lasts 5 s");
        check(def.position_at(0.0f) == 0.0f && def.position_at(1.49f) == 0.0f && std::abs(def.position_at(1.75f) - 0.25f) < 1e-5f && std::abs(def.position_at(2.0f) - 0.5f) < 1e-5f && std::abs(def.position_at(2.25f) - 0.75f) < 1e-5f &&
                  std::abs(def.position_at(4.25f) - 0.75f) < 1e-5f &&
                  def.position_at(2.5f) == 1.0f && def.position_at(3.99f) == 1.0f && std::abs(def.position_at(4.5f) - 0.5f) < 1e-5f,
              "23b position: 0 at the heel hold, 0.5 half way up (linear in time), 1 at the toe hold, 0.5 half way down");
        check(def.position_at(-0.1f) == -1.0f && def.position_at(5.0f) == -1.0f && def.position_at(9.0f) == -1.0f &&
                  def.position_at(std::numeric_limits<float>::quiet_NaN()) == -1.0f,
              "23c position outside [0, period), or NaN: -1 (outside 0..1)");
        float s = -1, e = -1;
        def.heel_hold(s, e);
        const bool heel_ok = s == 0.0f && std::abs(e - 1.5f) < 1e-6f;
        def.toe_hold(s, e);
        check(heel_ok && std::abs(s - 2.5f) < 1e-6f && std::abs(e - 4.0f) < 1e-6f, "23d the heel hold is [0, 1.5), the toe hold [2.5, 4.0)");

        wah::GuideSchedule bad = def;
        bad.heel_hold_s = 0.9f;
        wah::GuideSchedule zero_rise = def; zero_rise.rise_s = 0.0f;
        wah::GuideSchedule nan_fall = def; nan_fall.fall_s = std::numeric_limits<float>::quiet_NaN();
        wah::GuideSchedule short_toe = def; short_toe.toe_hold_s = 0.99f;
        wah::GuideSchedule inf_fall = def; inf_fall.fall_s = std::numeric_limits<float>::infinity();
        check(!bad.valid() && !zero_rise.valid() && !nan_fall.valid() && !short_toe.valid() && !inf_fall.valid(), "23e invalid: a hold under 1 s (D32), no rise, a NaN, an infinite fall");
        float bs = -1, be = -1;
        bad.heel_hold(bs, be);
        check(bad.period() == 0.0f && bad.position_at(0.5f) == -1.0f && bs == 0.0f && be == 0.0f, "23f an invalid schedule has no period, no position and empty intervals");
    }

    // 2. A pass measured behind a latency (3000 samples) and a 1.5 kHz high-pass.
    const int kM = 1 << 18, kD = 3000, kMax = 4800;
    const std::vector<float> xf = wah::generate_stimulus(kM, 7u, 0.1f);
    std::vector<double> x(xf.begin(), xf.end()), y(kM);
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (int i = 0; i < kM; i++) {
            const double t = i / kFs;
            const double c = 400.0 * std::pow(4.0, t < 1.5 ? 0.0 : (t < 2.5 ? t - 1.5 : (t < 4.0 ? 1.0 : (t < 5.0 ? 5.0 - t : 0.0))));
            const double w0 = 2.0 * kPi * c / kFs, al = std::sin(w0) / 10.0, a0 = 1.0 + al;
            const double v = 2.0 * (al / a0) * (x[i] - x2) - (-2.0 * std::cos(w0) / a0) * y1 - ((1.0 - al) / a0) * y2;
            x2 = x1; x1 = x[i]; y2 = y1; y1 = v; y[i] = v;
        }
    }
    auto lag = [&](const std::vector<double>& v) {
        std::vector<float> o(kM, 0.0f);
        for (int i = kD; i < kM; i++) o[i] = (float)v[i - kD];
        return o;
    };
    const std::vector<float> loop = lag(highpass1(1500.0, x)), dut = lag(highpass1(1500.0, y));
    auto pass = [&](const wah::GuideSchedule& g, int n = -1) {
        return wah::measure_pass_holds(g, xf.data(), dut.data(), loop.data(), n < 0 ? kM : n, (float)kFs, kMax);
    };
    auto good = [](const wah::HoldShape& h, double f0) {
        return h.valid && h.shape.model == wah::ShapeModel::BandPass2 && std::abs(h.shape.center_freq - f0) / f0 < 0.02 &&
               std::abs(h.shape.q - 5.0) < 0.5 && std::abs(h.shape.peak_gain_db - 6.02) < 1.0;
    };

    wah::PassHolds p = pass(def);
    check(p.valid && good(p.heel, 400.0) && good(p.toe, 1600.0), "24a a pass: heel 400 Hz and toe 1600 Hz, Q within 10 %, gain within 1 dB, valid");

    // The recording declared shorter than the pass (3.84 s): the heel hold is inside it, the toe hold is not.
    wah::PassHolds cut = pass(def, kM - 75000);
    check(!cut.valid && good(cut.heel, 400.0) && !cut.toe.valid, "24b the recording ends before the toe hold: the heel is read, the toe refused, the pass is not valid");

    // The guide is not what the pedal does: a schedule whose toe hold starts at 2.0 s (the pedal is still rising).
    wah::GuideSchedule early = def;
    early.heel_hold_s = 1.0f;
    wah::PassHolds wrong = pass(early);
    check(!wrong.valid && !wrong.toe.valid, "24c a schedule that puts the toe hold in the rise: refused (the 5 % rule sees the movement)");

    wah::GuideSchedule bad = def;
    bad.heel_hold_s = 0.9f;
    check(!pass(bad).valid, "24d an invalid schedule: refused");
    check(!wah::measure_pass_holds(def, nullptr, dut.data(), loop.data(), kM, (float)kFs, kMax).valid &&
              !wah::measure_pass_holds(def, xf.data(), nullptr, loop.data(), kM, (float)kFs, kMax).valid &&
              !wah::measure_pass_holds(def, xf.data(), dut.data(), nullptr, kM, (float)kFs, kMax).valid,
          "24e null stimulus, pedal response or loopback");
    // A different schedule that is also right: a longer heel hold and a shorter toe hold (the pedal's real times are the 5 s above, so
    // only the shapes that stay inside the pedal's holds are valid): heel 1.2 s alone is read, as the 1 s minimum.
    wah::GuideSchedule shorter = def;
    shorter.heel_hold_s = 1.2f;
    shorter.rise_s = 1.3f;
    wah::PassHolds sh = pass(shorter);
    check(sh.heel.valid && good(sh.heel, 400.0), "24f a schedule with a 1.2 s heel hold: the heel is still read right");

    // 3. The hold as the estimate of aggregate_holds().
    {
        const wah::HoldEstimate he = wah::hold_estimate_of(p.heel), te = wah::hold_estimate_of(p.toe);
        check(he.valid && he.center_freq == p.heel.shape.center_freq && he.q == p.heel.shape.q &&
                  he.peak_gain_db == p.heel.shape.peak_gain_db && he.windows_used == p.heel.windows_checked && he.windows_used >= 3,
              "25a a 2-pole hold gives the estimate with its centre, Q, gain and the windows checked");
        wah::HoldShape shelf = p.heel;
        shelf.shape.model = wah::ShapeModel::BandPass2Shelf;
        wah::HoldShape four = p.heel;
        four.shape.model = wah::ShapeModel::BandPass4;
        wah::HoldShape measured = p.heel;
        measured.shape.model = wah::ShapeModel::Measured;
        wah::HoldShape refused = p.heel;
        refused.valid = false;
        wah::HoldShape no_windows = p.heel;
        no_windows.windows_checked = 0;
        check(wah::hold_estimate_of(shelf).valid && !wah::hold_estimate_of(four).valid && !wah::hold_estimate_of(measured).valid &&
                  !wah::hold_estimate_of(refused).valid && !wah::hold_estimate_of(no_windows).valid,
              "25b a shelf shape is a band-pass estimate; a 4-pole, a Measured shape, a refused hold and a hold with no window are not");
        wah::HoldsAggregate ag = wah::aggregate_holds({he, he, he}, {te, te, te});
        check(ag.heel.valid && ag.heel.kept == 3 && std::abs(ag.heel.center_freq - 400.0f) < 8.0f && ag.toe.valid &&
                  ag.toe.kept == 3 && std::abs(ag.toe.center_freq - 1600.0f) < 32.0f,
              "25c aggregate_holds takes the estimates of three passes and gives 400 Hz and 1600 Hz");
    }

    // 4. The points of a pass (round 22).
    {
        auto tp = [](float t, float f, bool valid = true) {
            wah::TrackPoint p;
            p.time_s = t; p.estimate.center_freq = f; p.estimate.valid = valid;
            return p;
        };
        // window 0.25 s: [t - 0.125, t + 0.125]. Corners of the default schedule: 1.5, 2.5, 4.0 (and the ends 0 and 5.0).
        const std::vector<wah::TrackPoint> tr = {
            tp(0.1f, 400), tp(0.2f, 401), tp(0.5f, 402), tp(1.45f, 403), tp(1.7f, 500), tp(2.45f, 900), tp(3.0f, 1600),
            tp(3.95f, 1601), tp(4.5f, 800), tp(4.95f, 404), tp(5.5f, 405)};
        std::vector<wah::PassPoint> pp = wah::pass_points(def, tr);
        check(pp.size() == 4 && pp[0].center_freq == 402 && pp[1].center_freq == 500 && pp[2].center_freq == 1600 && pp[3].center_freq == 800,
              "26a kept: heel hold, rise, toe hold, fall; dropped: the settling (0.1 s, 0.2 s), the four turnarounds, the end of the pass and beyond");
        check(pp.size() == 4 && pp[0].position == 0.0f && std::abs(pp[1].position - 0.2f) < 1e-5f && pp[2].position == 1.0f && std::abs(pp[3].position - 0.5f) < 1e-5f &&
                  pp[0].weight == 1.0f && pp[3].weight == 1.0f,
              "26b the position is the guide's at the window time (0, 0.2, 1, 0.5) and the weight is 1");
        check(wah::pass_points(def, tr, 0.25f, 0.0f).size() == 5, "26c with no settling the window at 0.2 s is kept (the one at 0.1 s starts before the pass)");
        check(wah::pass_points(def, {tp(0.5f, 400, false), tp(0.6f, 0.0f), tp(0.7f, -3.0f), tp(0.8f, std::numeric_limits<float>::quiet_NaN()),
                                     tp(std::numeric_limits<float>::quiet_NaN(), 400), tp(0.9f, std::numeric_limits<float>::infinity())}).empty(),
              "26d a window that is not valid, or has a centre that is zero, negative, NaN or infinite, or a time that is NaN: dropped");
        wah::GuideSchedule bad = def;
        bad.rise_s = 0.0f;
        check(wah::pass_points(bad, tr).empty() && wah::pass_points(def, tr, 0.0f).empty() && wah::pass_points(def, tr, -0.25f).empty() &&
                  wah::pass_points(def, tr, 0.25f, -0.1f).empty() && wah::pass_points(def, std::vector<wah::TrackPoint>()).empty() &&
                  wah::pass_points(def, tr, std::numeric_limits<float>::quiet_NaN()).empty() && wah::pass_points(def, tr, 0.25f, std::numeric_limits<float>::quiet_NaN()).empty(),
              "26e an invalid schedule, a window of 0 or less, a negative settling, no tracks, NaN arguments: no points");
        const std::vector<wah::PassPoint> wide = wah::pass_points(def, tr, 1.0f, 0.2f);
        check(wide.size() == 2 && wide[0].center_freq == 1600 && wide[1].center_freq == 800,
              "26f a window of 1 s (the tracker's window is a parameter): only [2.5, 3.5] and [4, 5] fit between the corners (touching one is not straddling it)");

        // From audio: the tracker on the pedal of 24, one pass; the centre must follow 400 * 4^position.
        const std::vector<wah::TrackPoint> track = wah::track_bandpass(xf.data(), dut.data(), loop.data(), kM, (float)kFs, kMax);
        const std::vector<wah::PassPoint> real = wah::pass_points(def, track);
        bool follows = real.size() >= 8;
        bool has_heel = false, has_toe = false, has_mid = false;
        for (const wah::PassPoint& q : real) {
            const double truth = 400.0 * std::pow(4.0, q.position);
            if (std::abs(q.center_freq - truth) / truth > 0.06) follows = false;
            has_heel |= q.position == 0.0f; has_toe |= q.position == 1.0f; has_mid |= q.position > 0.3f && q.position < 0.7f;
        }
        check(follows && has_heel && has_toe && has_mid, "27a from audio: the tracker's windows give points at the heel, in the rise and at the toe, each within 6 % of 400 * 4^position");
        std::vector<std::vector<wah::PassPoint>> three = {real, real, real};
        wah::AggregatedCurve cur = wah::aggregate_passes(three);
        check(cur.valid && cur.dropped.empty() && cur.center_freq.size() == 11 && std::abs(cur.center_freq[0] - 400.0f) < 24.0f &&
                  std::abs(cur.center_freq[10] - 1600.0f) < 96.0f,
              "27b three such passes go into aggregate_passes: a curve from about 400 Hz to about 1600 Hz, nothing dropped");
    }

    // 5. One pass in one call, and the progress of a capture (round 23).
    {
        auto follows = [](const std::vector<wah::PassPoint>& pts) {
            bool ok = pts.size() >= 8;
            for (const wah::PassPoint& q : pts) { const double tr = 400.0 * std::pow(4.0, q.position); if (std::abs(q.center_freq - tr) / tr > 0.06) ok = false; }
            return ok;
        };
        auto analyse = [&](const wah::GuideSchedule& g, int n = -1) {
            return wah::analyse_pass(g, xf.data(), dut.data(), loop.data(), n < 0 ? kM : n, (float)kFs, kMax);
        };
        const wah::PassAnalysis a = analyse(def);
        check(a.valid && follows(a.points) && a.points.size() >= 28 && a.holds.valid && good(a.holds.heel, 400.0) && good(a.holds.toe, 1600.0),
              "28a a pass: 28 or more points (the 0.25 s windows of the tracker), within 6 % of 400 * 4^position, heel 400 Hz and toe 1600 Hz, valid");

        const wah::PassAnalysis cut = analyse(def, kM - 75000);
        check(cut.valid && !cut.holds.valid && cut.holds.heel.valid && !cut.holds.toe.valid && !cut.points.empty() && follows(cut.points),
              "28b the recording ends before the toe hold: the toe is refused, the heel and the points are kept, the pass stays valid");

        wah::GuideSchedule early = def;
        early.heel_hold_s = 1.0f;
        const wah::PassAnalysis wrong = analyse(early);
        check(wrong.valid && !wrong.holds.valid && !wrong.holds.toe.valid && !wrong.points.empty(), "28c a schedule that puts the toe in the rise: toe refused, points still given");

        wah::GuideSchedule bad = def;
        bad.fall_s = 0.0f;
        std::vector<float> other(kM);
        std::uint32_t s = 8642u;
        for (float& v : other) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; v = (float)((double)s / 2147483648.0 - 1.0); }
        const wah::PassAnalysis b1 = analyse(bad), b2 = wah::analyse_pass(def, nullptr, dut.data(), loop.data(), kM, (float)kFs, kMax),
                                b3 = wah::analyse_pass(def, xf.data(), nullptr, loop.data(), kM, (float)kFs, kMax),
                                b4 = wah::analyse_pass(def, xf.data(), dut.data(), nullptr, kM, (float)kFs, kMax),
                                b5 = wah::analyse_pass(def, xf.data(), dut.data(), other.data(), kM, (float)kFs, kMax);
        auto empty = [](const wah::PassAnalysis& x) { return !x.valid && x.points.empty() && !x.holds.valid && !x.holds.heel.valid && !x.holds.toe.valid; };
        check(empty(b1) && empty(b2) && empty(b3) && empty(b4) && empty(b5), "28d an invalid schedule, a null buffer, a loopback that is not the chain: nothing, not valid");

        // The progress of a capture. Four passes of the same pedal reach the target; a pass is one analysis.
        using wah::CaptureState;
        const wah::CaptureTarget N = wah::CaptureTarget::Normal;
        wah::CaptureProgress p3 = wah::capture_progress_of({a, a, a}, N), p4 = wah::capture_progress_of({a, a, a, a}, N);
        check(p3.valid && p3.state == CaptureState::NeedMorePasses && !p3.stop && p3.passes_recorded == 3 &&
                  p4.valid && p4.state == CaptureState::Done && p4.stop && p4.passes_kept == 4 && p4.heel_q_error == 0.0f && p4.toe_gain_error_db == 0.0f,
              "29a three passes: need more; four identical passes: done, with the errors of the holds measured (zero for identical passes)");

        wah::PassAnalysis no_toe = a;  // the toe of this pass was refused: the points stay, the toe hold is an invalid estimate
        no_toe.holds.toe = wah::HoldShape();
        no_toe.holds.valid = false;
        wah::CaptureProgress one = wah::capture_progress_of({a, a, a, no_toe}, N);
        check(one.state == CaptureState::Done && one.passes_kept == 4, "29b one refused toe hold among four passes: the three others are enough, still done");
        wah::CaptureProgress two = wah::capture_progress_of({a, a, no_toe, no_toe}, N);
        check(!two.stop && two.state == CaptureState::ConfidenceNotReached && two.limiting == wah::CaptureLimit::ToeQ && two.toe_q_error == std::numeric_limits<float>::infinity(),
              "29c two refused toe holds among four: the toe has only 2 holds, no error can be given: not done, and the toe Q is what is lacking");

        wah::PassAnalysis dead = analyse(bad);  // not valid, no points
        wah::CaptureProgress with_dead = wah::capture_progress_of({a, a, a, a, dead}, N);
        check(with_dead.passes_recorded == 5 && with_dead.passes_kept == 4 && with_dead.passes_dropped == 1 && with_dead.state == CaptureState::Done,
              "29d a pass that could not be analysed counts as recorded and is dropped, and does not spoil the others");
        // The target decides: five passes whose centres are 12 %, 0, 12 %, 6 % and 6 % off (the holds identical) give an error of about 5 %:
        // done at the fast target (6 %), not at the normal one (3 %).
        std::vector<wah::PassAnalysis> loose(5, a);
        const float off[5] = {0.88f, 1.0f, 1.12f, 0.94f, 1.06f};
        for (int i = 0; i < 5; i++) for (wah::PassPoint& q : loose[i].points) q.center_freq *= off[i];
        wah::CaptureProgress fast = wah::capture_progress_of(loose, wah::CaptureTarget::Fast), norm = wah::capture_progress_of(loose, N);
        check(fast.state == CaptureState::Done && norm.state == CaptureState::ConfidenceNotReached && norm.limiting == wah::CaptureLimit::Curve &&
                  norm.confidence > 0.03f && fast.confidence < 0.06f,
              "29f the target is the caller's: passes 5 % apart are done at the fast target and not at the normal one, and it is the curve that lacks");
        check(!wah::capture_progress_of({}, N).valid && !wah::capture_progress_of({a, a, a, a}, N, 1).valid, "29e no passes, or fewer than 2 positions: refused");
    }

    // 6. The summary of a capture (round 24).
    {
        const wah::CaptureTarget N = wah::CaptureTarget::Normal;
        const wah::PassAnalysis a = wah::analyse_pass(def, xf.data(), dut.data(), loop.data(), kM, (float)kFs, kMax);
        wah::PassAnalysis no_toe = a;
        no_toe.holds.toe = wah::HoldShape();
        no_toe.holds.valid = false;
        wah::PassAnalysis dead;  // not analysed
        wah::PassAnalysis stray = a;  // a pass whose curve is 40 % off everywhere: dropped from the curve
        for (wah::PassPoint& q : stray.points) q.center_freq *= 1.4f;

        const wah::CaptureSummary s = wah::capture_summary({a, a, a, a}, N);
        bool curve_ok = s.curve.valid && s.curve.center_freq.size() == 11 && s.curve.dropped.empty();
        for (size_t i = 0; i < s.curve.position.size() && curve_ok; i++) {
            const double truth = 400.0 * std::pow(4.0, s.curve.position[i]);
            if (std::abs(s.curve.center_freq[i] - truth) / truth > 0.06) curve_ok = false;
        }
        check(s.valid && curve_ok && s.holds.heel.valid && s.holds.heel.kept == 4 && std::abs(s.holds.heel.center_freq - 400.0f) < 8.0f &&
                  s.holds.toe.valid && s.holds.toe.kept == 4 && std::abs(s.holds.toe.center_freq - 1600.0f) < 32.0f &&
                  std::abs(s.holds.heel.q - 5.0f) < 0.5f && s.progress.state == wah::CaptureState::Done,
              "30a four passes: a curve within 6 % of 400 * 4^position at 11 positions, heel 400 Hz and toe 1600 Hz kept from 4 holds, Q 5, done");

        const wah::CaptureProgress direct = wah::capture_progress_of({a, a, a, a}, N);
        check(s.progress.state == direct.state && s.progress.passes_kept == direct.passes_kept && s.progress.confidence == direct.confidence &&
                  s.progress.worst_ratio == direct.worst_ratio && s.progress.limiting == direct.limiting,
              "30b the progress is the one of capture_progress_of on the same passes");

        const wah::CaptureSummary t = wah::capture_summary({a, a, no_toe, a}, N);
        check(t.valid && t.holds.heel.kept == 4 && t.holds.toe.kept == 3 && t.holds.toe.dropped == std::vector<int>({2}) && t.curve.dropped.empty(),
              "30c a refused toe hold in pass 2: the toe is read from 3 holds and lists index 2 as dropped, the curve keeps all 4 passes");

        const wah::CaptureSummary u = wah::capture_summary({a, a, a, dead, a}, N);
        check(u.valid && u.curve.dropped == std::vector<int>({3}) && u.holds.heel.dropped == std::vector<int>({3}) && u.holds.toe.dropped == std::vector<int>({3}) &&
                  u.progress.passes_recorded == 5 && u.progress.passes_dropped == 1,
              "30d a pass that was not analysed (index 3) is dropped from the curve and from both holds, and counted as recorded");

        const wah::CaptureSummary v = wah::capture_summary({a, a, a, stray, a}, N);
        check(v.valid && v.curve.dropped == std::vector<int>({3}) && v.holds.heel.kept == 5,
              "30e a pass whose curve is 40 % off is dropped from the curve (index 3) but its holds, which are right, are kept");

        // All the holds refused: the curve is there, the holds are not.
        const wah::CaptureSummary w = wah::capture_summary({no_toe, no_toe, no_toe, no_toe}, N);
        wah::PassAnalysis no_holds = a;
        no_holds.holds = wah::PassHolds();
        const wah::CaptureSummary x = wah::capture_summary({no_holds, no_holds, no_holds, no_holds}, N);
        check(w.valid && !w.holds.toe.valid && w.holds.heel.valid && x.valid && !x.holds.heel.valid && !x.holds.toe.valid && x.curve.valid && !x.progress.stop,
              "30f every toe hold refused: no toe, the heel is read; every hold refused: the curve is there, no heel and no toe, and the capture does not stop");

        check(!wah::capture_summary({}, N).valid && !wah::capture_summary({a, a, a, a}, N, 1).valid && !wah::capture_summary({dead, dead, dead, dead}, N).valid,
              "30g no passes, fewer than 2 positions, or no pass with data: not valid");
        // The shapes (round 26).
        check(s.heel_shape.valid && s.heel_shape.model == wah::ShapeModel::BandPass2 && s.heel_shape.kept == 4 && std::abs(s.heel_shape.center_freq - 400.0f) < 8.0f &&
                  s.toe_shape.valid && s.toe_shape.model == wah::ShapeModel::BandPass2 && s.toe_shape.kept == 4 && std::abs(s.toe_shape.center_freq - 1600.0f) < 32.0f,
              "33a four passes: the heel shape (2-pole, 400 Hz) and the toe shape (2-pole, 1600 Hz), each from 4 holds");
        check(t.heel_shape.kept == 4 && t.toe_shape.kept == 3 && t.toe_shape.dropped == std::vector<int>({2}) &&
                  u.heel_shape.dropped == std::vector<int>({3}) && u.toe_shape.dropped == std::vector<int>({3}) && u.heel_shape.kept == 4,
              "33b a refused toe in pass 2 and a pass not analysed (3): the same indices as in the holds");
        check(w.heel_shape.valid && !w.toe_shape.valid && !x.heel_shape.valid && !x.toe_shape.valid && !wah::capture_summary({}, N).heel_shape.valid,
              "33c every toe refused: a heel shape and no toe shape; every hold refused or no passes: no shapes");

        // A hold of another model has a shape and no band-pass estimate: the shape is reported, and the capture is not done for it.
        wah::PassAnalysis tab = a;
        for (wah::HoldShape* hs : {&tab.holds.heel, &tab.holds.toe}) {
            hs->shape.model = wah::ShapeModel::Measured;
            hs->shape.table_freq_hz.clear(); hs->shape.table_db.clear();
            for (int i = 0; i < wah::kShapeTablePoints; i++) { hs->shape.table_freq_hz.push_back(20.0f * std::pow(250.0f, (float)i / (wah::kShapeTablePoints - 1))); hs->shape.table_db.push_back(0.1f * i); }
        }
        const wah::CaptureSummary z = wah::capture_summary({tab, tab, tab, tab}, N);
        check(z.valid && z.heel_shape.valid && z.heel_shape.model == wah::ShapeModel::Measured && z.toe_shape.model == wah::ShapeModel::Measured &&
                  !z.holds.heel.valid && !z.progress.stop && z.progress.state == wah::CaptureState::ConfidenceNotReached,
              "33d four passes whose holds are Measured tables: both shapes are given (Measured), there is no Q or gain, and the capture is not done for it (D49, D51)");

        std::vector<wah::PassAnalysis> loose(5, a);  // centres 12 %, 0, 12 %, 6 % and 6 % off: an error of about 5 %
        const float off[5] = {0.88f, 1.0f, 1.12f, 0.94f, 1.06f};
        for (int i = 0; i < 5; i++) for (wah::PassPoint& q : loose[i].points) q.center_freq *= off[i];
        check(wah::capture_summary(loose, wah::CaptureTarget::Fast).progress.state == wah::CaptureState::Done &&
                  wah::capture_summary(loose, N).progress.state == wah::CaptureState::ConfidenceNotReached,
              "30i the target is the caller's: passes 5 % apart are done at the fast target and not at the normal one");
        const wah::CaptureSummary y = wah::capture_summary({a, a, a, a}, N, 21);
        check(y.valid && y.curve.center_freq.size() == 21 && y.curve.position.size() == 21, "30h the number of positions is the caller's");
    }

    // 7. The shapes of the holds over the passes (round 25). Built by hand: no audio.
    {
        using wah::ShapeModel;
        auto mk = [](ShapeModel m, float f, float q, float g) {
            wah::HoldShape h;
            h.shape.model = m; h.shape.center_freq = f; h.shape.q = q; h.shape.peak_gain_db = g; h.shape.valid = true;
            h.windows_checked = 4; h.valid = true;
            return h;
        };
        auto bp2 = [&](float f, float q = 5.0f, float g = 6.0f) { return mk(ShapeModel::BandPass2, f, q, g); };
        auto shelf = [&](float f, float q, float g, float sf, float sg) {
            wah::HoldShape h = mk(ShapeModel::BandPass2Shelf, f, q, g);
            h.shape.shelf_freq_hz = sf; h.shape.shelf_gain_db = sg;
            return h;
        };
        auto table = [&](float base_db, float scale = 1.0f) {
            wah::HoldShape h = mk(ShapeModel::Measured, 0, 0, 0);
            for (int i = 0; i < wah::kShapeTablePoints; i++) {
                h.shape.table_freq_hz.push_back(20.0f * std::pow(250.0f, (float)i / (wah::kShapeTablePoints - 1)) * scale);
                h.shape.table_db.push_back(base_db + 0.1f * i);
            }
            return h;
        };
        const float nan = std::numeric_limits<float>::quiet_NaN();

        wah::ShapeSummary a = wah::aggregate_shapes({bp2(390), bp2(410), bp2(400), bp2(395), bp2(405)});
        check(a.valid && a.model == ShapeModel::BandPass2 && a.kept == 5 && a.dropped.empty() && a.center_freq == 400.0f && a.q == 5.0f && a.peak_gain_db == 6.0f,
              "31a five 2-pole holds: the median of the centre (400 Hz), Q and gain, none dropped");
        check(wah::aggregate_shapes({bp2(380), bp2(420), bp2(410), bp2(390)}).center_freq == 400.0f, "31b an even number: the mean of the two middle values");

        wah::ShapeSummary c = wah::aggregate_shapes({bp2(400), shelf(300, 2, 5, 150, -6), bp2(401), bp2(399)});
        check(c.valid && c.model == ShapeModel::BandPass2 && c.kept == 3 && c.dropped == std::vector<int>({1}) && c.center_freq == 400.0f,
              "31c three 2-pole holds and a shelf one: the 2-pole wins, the shelf hold (index 1) is dropped");
        wah::ShapeSummary d = wah::aggregate_shapes({shelf(300, 2, 5, 150, -6), shelf(310, 3, 6, 160, -8), bp2(400), shelf(320, 4, 7, 170, -10)});
        check(d.valid && d.model == ShapeModel::BandPass2Shelf && d.kept == 3 && d.dropped == std::vector<int>({2}) && d.center_freq == 310.0f && d.q == 3.0f &&
                  d.peak_gain_db == 6.0f && d.shelf_freq_hz == 160.0f && d.shelf_gain_db == -8.0f,
              "31d three shelf holds and a 2-pole one: the shelf wins and its medians (centre, Q, gain, shelf corner, shelf gain) are given");
        check(!wah::aggregate_shapes({bp2(400), bp2(401), shelf(300, 2, 5, 150, -6), shelf(310, 3, 6, 160, -8)}).valid &&
                  !wah::aggregate_shapes({bp2(400), shelf(300, 2, 5, 150, -6)}).valid &&
                  !wah::aggregate_shapes({bp2(400), shelf(300, 2, 5, 150, -6), table(0)}).valid,
              "31e no majority (2 against 2, 1 against 1, three models): not valid");

        wah::HoldShape four = mk(ShapeModel::BandPass4, 300, 4, 6), none = mk(ShapeModel::None, 0, 0, 0), refused = bp2(400), nan_c = bp2(nan), q0 = bp2(400, 0.0f),
                       inf_g = bp2(400, 5.0f, std::numeric_limits<float>::infinity()), neg_f = bp2(-400), shape_invalid = bp2(400);
        refused.valid = false;
        shape_invalid.shape.valid = false;
        wah::ShapeSummary e = wah::aggregate_shapes({bp2(400), four, bp2(402), refused, nan_c, none, q0, inf_g, neg_f, shape_invalid});
        check(e.valid && e.kept == 2 && e.dropped == std::vector<int>({1, 3, 4, 5, 6, 7, 8, 9}) && e.center_freq == 401.0f,
              "31f a 4-pole, a refused hold, a NaN centre, a shape of None, Q 0, an infinite gain, a negative centre and an invalid shape: dropped; the two others are read");

        wah::ShapeSummary m = wah::aggregate_shapes({table(0.0f), table(1.0f), table(5.0f), bp2(400)});
        bool cells = m.valid && m.model == ShapeModel::Measured && m.kept == 3 && m.dropped == std::vector<int>({3}) &&
                     (int)m.table_db.size() == wah::kShapeTablePoints && (int)m.table_freq_hz.size() == wah::kShapeTablePoints;
        for (int i = 0; cells && i < wah::kShapeTablePoints; i++) cells = std::abs(m.table_db[i] - (1.0f + 0.1f * i)) < 1e-4f;
        check(cells, "31g three Measured tables (+0, +1, +5 dB) and a 2-pole hold: the median of each cell (+1 dB), the 2-pole hold dropped");
        wah::HoldShape wrong_size = table(1.0f), other_grid = table(1.0f, 1.2f), nan_cell = table(1.0f);
        wrong_size.shape.table_db.pop_back();
        nan_cell.shape.table_db[10] = nan;
        wah::ShapeSummary g = wah::aggregate_shapes({table(0.0f), table(2.0f), wrong_size, other_grid, nan_cell});
        check(g.valid && g.kept == 2 && g.dropped == std::vector<int>({2, 3, 4}) && std::abs(g.table_db[0] - 1.0f) < 1e-4f,
              "31h a table of the wrong size, one on another grid (20 % off) and one with a NaN cell are dropped; the two others give the mean of the middle two");

        // A shelf hold with a NaN corner, a NaN shelf gain or a corner of 0 is not usable; a table whose grid has fewer cells is not usable either.
        wah::HoldShape sh_nan_f = shelf(300, 2, 5, nan, -6), sh_nan_g = shelf(300, 2, 5, 150, nan), sh_zero_f = shelf(300, 2, 5, 0.0f, -6);
        wah::ShapeSummary k = wah::aggregate_shapes({shelf(300, 2, 5, 150, -6), shelf(320, 2, 5, 150, -6), sh_nan_f, sh_nan_g, sh_zero_f});
        wah::HoldShape short_grid = table(1.0f);
        short_grid.shape.table_freq_hz.pop_back();
        wah::ShapeSummary kt = wah::aggregate_shapes({table(0.0f), table(2.0f), short_grid});
        check(k.valid && k.kept == 2 && k.dropped == std::vector<int>({2, 3, 4}) && k.center_freq == 310.0f &&
                  kt.valid && kt.kept == 2 && kt.dropped == std::vector<int>({2}),
              "31k a shelf hold with a NaN corner, a NaN shelf gain or a corner of 0, and a table with 63 frequencies, are not usable and are dropped");
        check(wah::aggregate_shapes({bp2(400), bp2(401), shelf(300, 2, 5, 150, -6), shelf(310, 3, 6, 160, -8)}).dropped == std::vector<int>({0, 1, 2, 3}),
              "31l with no majority every hold is listed as dropped");

        wah::ShapeSummary one = wah::aggregate_shapes({four, bp2(400), refused});
        check(one.valid && one.kept == 1 && one.center_freq == 400.0f && one.dropped == std::vector<int>({0, 2}), "31i a single usable hold is a valid summary with kept 1");
        check(!wah::aggregate_shapes({}).valid && !wah::aggregate_shapes({four, none, refused}).valid, "31j no holds, or none usable: not valid");

        // From audio: the holds of four passes of the synthetic pedal.
        const wah::PassAnalysis pa = wah::analyse_pass(def, xf.data(), dut.data(), loop.data(), kM, (float)kFs, kMax);
        wah::ShapeSummary heel = wah::aggregate_shapes({pa.holds.heel, pa.holds.heel, pa.holds.heel, pa.holds.heel}),
                          toe = wah::aggregate_shapes({pa.holds.toe, pa.holds.toe, pa.holds.toe, pa.holds.toe});
        check(heel.valid && heel.model == ShapeModel::BandPass2 && std::abs(heel.center_freq - 400.0f) < 8.0f && std::abs(heel.q - 5.0f) < 0.5f &&
                  toe.valid && toe.model == ShapeModel::BandPass2 && std::abs(toe.center_freq - 1600.0f) < 32.0f,
              "32a the holds of a pass from audio: heel 400 Hz and toe 1600 Hz, Q 5, 2-pole");
    }

    // 8. The virtual pedal (round 27): the buffers.
    {
        wah::Profile prof;  // defaults: 450 to 2100 Hz, Q 4, +18 dB
        const std::vector<float> st = wah::generate_stimulus(1 << 16, 3u, 0.1f);
        const wah::SimulatedPass sp = wah::simulate_pass(def, prof, st, (float)kFs);
        std::vector<double> sd(st.begin(), st.end());
        const std::vector<double> hp = highpass1(1500.0, sd);
        double worst = 0, early = 0;
        for (size_t i = 0; i < st.size(); i++) {
            const double ref = i >= 3000 ? hp[i - 3000] : 0.0;
            if (sp.valid && i < sp.loopback.size()) worst = std::max(worst, std::abs(sp.loopback[i] - ref));
            if (sp.valid && i < 3000) early = std::max(early, std::max(std::abs((double)sp.loopback[i]), std::abs((double)sp.dut[i])));
        }
        check(sp.valid && sp.dut.size() == st.size() && sp.loopback.size() == st.size() && worst < 1e-5 && early == 0.0,
              "34a the loopback is the stimulus through a 1.5 kHz high-pass delayed by 3000 samples (the test's own reference), and both start with 3000 zeros");

        wah::Profile dry = prof;
        dry.levels.mix = 0.0f;
        const wah::SimulatedPass dp = wah::simulate_pass(def, dry, st, (float)kFs);
        double dw = 0;
        for (size_t i = 0; dp.valid && i < st.size(); i++) dw = std::max(dw, (double)std::abs(dp.dut[i] - dp.loopback[i]));
        check(dp.valid && dw < 1e-6 && !sp.dut.empty() && sp.dut != sp.loopback, "34b a dry profile (mix 0) gives the loopback, and the wet one does not: the pedal and the loopback go through the same chain");

        wah::MeasurementChain none;
        none.latency_samples = 0; none.highpass_hz = 0.0f;
        const wah::SimulatedPass np = wah::simulate_pass(def, dry, st, (float)kFs, none);
        check(np.valid && np.loopback == st && np.dut == st, "34c no latency and no high-pass: the loopback is the stimulus, and so is a dry pedal");

        // After the pass the pedal is back at the heel: a stimulus of 10.9 s, the hold read from 5.2 s to 7 s is at 450 Hz (not 2100).
        const std::vector<float> longst = wah::generate_stimulus(1 << 19, 5u, 0.1f);
        const wah::SimulatedPass lp = wah::simulate_pass(def, prof, longst, (float)kFs);
        wah::HoldEstimate tail;
        if (lp.valid) tail = wah::estimate_hold(longst.data(), lp.dut.data(), lp.loopback.data(), 1 << 19, (float)kFs, 4800, 5.0f, 7.0f);
        check(tail.valid && std::abs(tail.center_freq - 450.0f) / 450.0f < 0.02, "34e after the pass the pedal is back at the heel: a hold read from 5 s to 7 s is at 450 Hz within 2 %");

        // A length that is not a multiple of the block (65536 + 37 samples): the last, short block is processed as it is, and nothing past the end is touched.
        const std::vector<float> odd = wah::generate_stimulus((1 << 16) + 37, 9u, 0.1f);
        const wah::SimulatedPass op = wah::simulate_pass(def, dry, odd, (float)kFs);
        double ow = 0;
        for (size_t i = 0; op.valid && i < odd.size(); i++) ow = std::max(ow, (double)std::abs(op.dut[i] - op.loopback[i]));
        check(op.valid && op.dut.size() == odd.size() && op.loopback.size() == odd.size() && ow < 1e-6 && wah::simulate_pass(def, dry, odd, (float)kFs, wah::MeasurementChain(), 1000).valid,
              "34f a stimulus of 65573 samples (not a multiple of the block of 64, or of 1000): same length out, the short last block is right");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        wah::GuideSchedule badg = def;
        badg.rise_s = 0.0f;
        wah::MeasurementChain neg_lat, neg_hp, nyq_hp, nan_hp;
        neg_lat.latency_samples = -1; neg_hp.highpass_hz = -1.0f; nyq_hp.highpass_hz = 24000.0f; nan_hp.highpass_hz = nan;
        auto empty = [](const wah::SimulatedPass& x) { return !x.valid && x.dut.empty() && x.loopback.empty(); };
        check(empty(wah::simulate_pass(badg, prof, st, (float)kFs)) && empty(wah::simulate_pass(def, prof, std::vector<float>(), (float)kFs)) &&
                  empty(wah::simulate_pass(def, prof, st, 0.0f)) && empty(wah::simulate_pass(def, prof, st, nan)) && empty(wah::simulate_pass(def, prof, st, std::numeric_limits<float>::infinity())) && empty(wah::simulate_pass(def, prof, st, (float)kFs, wah::MeasurementChain(), 0)) &&
                  empty(wah::simulate_pass(def, prof, st, (float)kFs, neg_lat)) && empty(wah::simulate_pass(def, prof, st, (float)kFs, neg_hp)) &&
                  empty(wah::simulate_pass(def, prof, st, (float)kFs, nyq_hp)) && empty(wah::simulate_pass(def, prof, st, (float)kFs, nan_hp)),
              "34d an invalid schedule, no stimulus, a sample rate of 0, NaN or infinite, a block of 0, a negative latency, a negative, Nyquist or NaN high-pass: refused with empty buffers");
    }

    // 10. The tracker of shapes (round 28, D65).
    {
        const std::vector<wah::ShapeTrackPoint> ts = wah::track_shape(xf.data(), dut.data(), loop.data(), kM, (float)kFs, kMax);
        const std::vector<wah::TrackPoint> tb = wah::track_bandpass(xf.data(), dut.data(), loop.data(), kM, (float)kFs, kMax);
        bool same = ts.size() == tb.size() && !ts.empty();
        for (size_t i = 0; same && i < ts.size(); i++) same = ts[i].time_s == tb[i].time_s;
        int valid_windows = 0;
        for (const wah::ShapeTrackPoint& p : ts) valid_windows += p.shape.valid;
        check(same && valid_windows >= 40, "36a the same windows as track_bandpass (same number, same times), and most of them (40 or more) give a valid shape");

        std::vector<float> other(kM);
        std::uint32_t sd = 8642u;
        for (float& v : other) { sd ^= sd << 13; sd ^= sd >> 17; sd ^= sd << 5; v = (float)((double)sd / 2147483648.0 - 1.0); }
        auto trk = [&](const float* s, const float* d, const float* l, float win = 0.25f, int seg = 2048, int n = -1) {
            return wah::track_shape(s, d, l, n < 0 ? kM : n, (float)kFs, kMax, win, seg).size();
        };
        check(trk(nullptr, dut.data(), loop.data()) == 0 && trk(xf.data(), nullptr, loop.data()) == 0 && trk(xf.data(), dut.data(), nullptr) == 0 &&
                  trk(xf.data(), dut.data(), other.data()) == 0 && trk(xf.data(), dut.data(), loop.data(), 0.0f) == 0 && trk(xf.data(), dut.data(), loop.data(), 0.25f, 1000) == 0 &&
                  trk(xf.data(), dut.data(), loop.data(), 0.05f) == 0 && trk(xf.data(), dut.data(), loop.data(), 10.0f) == 0,
              "36b null buffers, an unrelated loopback, a window of 0, a segment that is not a power of two, a window under 4 segments, a window longer than the recording: no windows");

        auto stp = [](float t, wah::ShapeModel m, float f, bool valid = true) {
            wah::ShapeTrackPoint p;
            p.time_s = t; p.shape.model = m; p.shape.center_freq = f; p.shape.valid = valid;
            return p;
        };
        using M = wah::ShapeModel;
        auto stp4 = [&](float t, float lower, float upper) { wah::ShapeTrackPoint p = stp(t, M::BandPass4, lower); p.shape.center2_freq = upper; return p; };
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const std::vector<wah::ShapeTrackPoint> sp = {
            stp(0.5f, M::BandPass2, 402), stp(1.7f, M::BandPass2Shelf, 500), stp4(3.0f, 1600, 3200), stp(4.5f, M::Measured, 800), stp(0.6f, M::None, 0),
            stp(0.7f, M::BandPass2, 400, false), stp(0.8f, M::BandPass2, 0), stp(0.9f, M::BandPass2, nan), stp(1.45f, M::BandPass2, 403), stp(0.1f, M::BandPass2, 400),
            stp(5.5f, M::BandPass2, 405), stp(nan, M::BandPass2, 406), stp(0.2f, M::BandPass2, 401)};
        const std::vector<wah::PassPoint> pp = wah::shape_pass_points(def, sp);
        check(pp.size() == 3 && pp[0].center_freq == 402 && pp[1].center_freq == 500 && pp[2].center_freq == 1600 && pp[0].position == 0.0f &&
                  std::abs(pp[1].position - 0.2f) < 1e-5f && pp[2].position == 1.0f && pp[0].weight == 1.0f,
              "36c the centre of a 2-pole, of a 2-pole with shelf and the lower centre of a 4-pole are kept, with the guide's position; a Measured shape, None, an invalid window, a centre of 0 or NaN, a turnaround, the settling (0.1 s and 0.2 s), beyond the pass and a NaN time are dropped");
        check(wah::shape_pass_points(def, sp, 0.25f, 0.0f).size() == 4 && wah::shape_pass_points(def, sp, 0.0f).empty() && wah::shape_pass_points(def, sp, 0.25f, -0.1f).empty() &&
                  wah::shape_pass_points(def, std::vector<wah::ShapeTrackPoint>()).empty() && wah::shape_pass_points(def, sp, nan).empty() && wah::shape_pass_points(def, sp, 0.25f, nan).empty(),
              "36d with no settling the window at 0.2 s is kept too (4 points, the one at 0.1 s still starts before the pass); a window of 0, a negative settling, no windows and NaN arguments: no points");
        wah::GuideSchedule badg = def;
        badg.rise_s = 0.0f;
        check(wah::shape_pass_points(badg, sp).empty(), "36e an invalid schedule: no points");

        const std::vector<wah::PassPoint> real = wah::shape_pass_points(def, ts);
        bool follows = real.size() >= 28;
        for (const wah::PassPoint& q : real) { const double tr = 400.0 * std::pow(4.0, q.position); if (std::abs(q.center_freq - tr) / tr > 0.06) follows = false; }
        check(follows, "36f from audio: the points of the tracker of shapes are 28 or more and each within 6 % of 400 * 4^position");
    }

    // 9. Closed loop (round 27): the sample profiles are captured through the virtual pedal. Measured in the round (4 passes of seeds 7 to 10,
    //    blocks of 64 samples): Cry Baby (450 to 2100 Hz, Q 4.8, +18 dB): heel 448.5 Hz, toe 2093.8 Hz (0.3 % low), Q 4.68 and 4.73 (2.5 % and 1.5 % low),
    //    gain 8.90 and 9.12 dB, curve at most 3.03 % off (at position 0.1) with the band-pass tracker of round 27, 1.07 % with the tracker of shapes of
    //    round 28. Vox (420 to 2180 Hz, Q 4.1 to 4.6, bass shelf -2.1 dB): no points in round 27, a curve at most 1.79 % off in round 28, heel Q 3.82
    //    (6.8 % low). The tolerances of the holds are the budget of D32 (2 % centre, 10 % Q), which the measurement meets; those of the curve are
    //    set from the round-28 measurement (2 % for the Cry Baby, 3 % for the Vox), tighter than the 5 % of D32, so that the gain of the better tracker is kept.
    //    The gain is NOT the profile's: see 35c.
    {
        const std::string dir = WAH_EXAMPLES_DIR;
        const std::optional<wah::Profile> cry = wah::load_toml(dir + "/CryBaby_GCB95_Modern.wah"), vox = wah::load_toml(dir + "/Vox_V846_Vintage.wah");
        check(cry.has_value() && vox.has_value(), "35  the two sample profiles load");
        if (cry && vox) {
            const int n = 1 << 18;
            auto capture = [&](const wah::Profile& p, unsigned seed) {
                const std::vector<float> st = wah::generate_stimulus(n, seed, 0.1f);
                const wah::SimulatedPass sp = wah::simulate_pass(def, p, st, (float)kFs);
                return wah::analyse_pass(def, st.data(), sp.dut.data(), sp.loopback.data(), n, (float)kFs, kMax);
            };
            std::vector<wah::PassAnalysis> passes;
            for (unsigned seed = 7; seed < 11; seed++) passes.push_back(capture(*cry, seed));
            const wah::CaptureSummary cs = wah::capture_summary(passes, wah::CaptureTarget::Normal);
            const wah::Filter& f = cry->filter;
            auto rel = [](double v, double truth) { return std::abs(v - truth) / truth; };
            check(cs.valid && cs.progress.state == wah::CaptureState::Done && cs.holds.heel.valid && cs.holds.toe.valid &&
                      rel(cs.holds.heel.center_freq, f.freq_heel) < 0.02 && rel(cs.holds.toe.center_freq, f.freq_toe) < 0.02 &&
                      rel(cs.holds.heel.q, f.q_min) < 0.10 && rel(cs.holds.toe.q, f.q_max) < 0.10 &&
                      cs.heel_shape.model == wah::ShapeModel::BandPass2 && cs.toe_shape.model == wah::ShapeModel::BandPass2,
                  "35a Cry Baby through the virtual pedal: done, heel within 2 % of 450 Hz, toe within 2 % of 2100 Hz, Q within 10 % of 4.8, both shapes 2-pole");
            bool curve_ok = cs.curve.valid && cs.curve.center_freq.size() == 11;
            for (size_t i = 0; curve_ok && i < 11; i++)
                curve_ok = rel(cs.curve.center_freq[i], f.freq_heel * std::pow((double)f.freq_toe / f.freq_heel, cs.curve.position[i])) < 0.02;
            check(curve_ok, "35b the curve at the 11 positions is within 2 % of 450 * (2100 / 450)^position (the profile's own sweep; measured at most 1.07 %)");
            check(std::abs(cs.holds.heel.peak_gain_db - 0.5 * f.peak_gain_db) < 1.0 && std::abs(cs.holds.toe.peak_gain_db - 0.5 * f.peak_gain_db) < 1.0,
                  "35c KNOWN DEFECT of the engine (M7): the gain read is half the profile's dB (9 dB within 1 dB for +18 dB), because the engine scales the amplitude by 10^(dB/40)");

            // The Vox profile has a bass shelf of -2.1 dB. The engine's shelf is a stub (b0 scaled alone), which breaks the symmetry of the band-pass:
            // no window is a 2-pole band-pass (before round 28, with estimate_bandpass, the pass had no points). The tracker of shapes reads the
            // window as a shape with a shelf and gives the points; the same profile without the shelf gives them too.
            wah::Profile no_shelf = *vox;
            no_shelf.filter.bass_shelf_db = 0.0f;
            const wah::PassAnalysis without = capture(no_shelf, 7u);
            std::vector<wah::PassAnalysis> vp;
            for (unsigned seed = 7; seed < 10; seed++) vp.push_back(capture(*vox, seed));
            check(vp[0].valid && vp[0].points.size() >= 28 && without.valid && without.points.size() >= 28,
                  "35d the Vox profile (bass shelf -2.1 dB) gives 28 or more points through the tracker of shapes, as it does without the shelf (D65: before round 28 it gave none)");
            const wah::CaptureSummary vs = wah::capture_summary(vp, wah::CaptureTarget::Normal);
            const wah::Filter& vf = vox->filter;
            bool vcurve = vs.valid && vs.curve.valid && vs.curve.center_freq.size() == 11;
            for (size_t i = 0; vcurve && i < 11; i++)
                vcurve = rel(vs.curve.center_freq[i], vf.freq_heel * std::pow((double)vf.freq_toe / vf.freq_heel, vs.curve.position[i])) < 0.03;
            check(vcurve && vs.holds.heel.valid && vs.holds.toe.valid && rel(vs.holds.heel.center_freq, vf.freq_heel) < 0.02 && rel(vs.holds.toe.center_freq, vf.freq_toe) < 0.02 &&
                      rel(vs.holds.heel.q, vf.q_min) < 0.10 && rel(vs.holds.toe.q, vf.q_max) < 0.10 && vs.heel_shape.model == wah::ShapeModel::BandPass2Shelf &&
                      vs.toe_shape.model == wah::ShapeModel::BandPass2Shelf,
                  "35e the Vox through the virtual pedal (3 passes): the curve within 3 % of its own sweep (measured at most 1.79 %), heel and toe within 2 %, Q within 10 %, both shapes read as 2-pole with shelf");
        }
    }

    // ── Round 30: profile_from_summary() (M6, D66). The summaries of 37a to 37g are built by hand, so each number the profile must
    //    carry is known. 37r to 37t are the end to end checks, and every profile that comes out is also read back by the reader of wah_toml.cpp
    //    (37q): the guarantee "what this function gives, the reader accepts" is checked by something other than this function.
    {
        auto hold = [](float f, float q, float g) {
            wah::HoldSummary h;
            h.center_freq = f; h.q = q; h.peak_gain_db = g; h.kept = 4; h.valid = true;
            return h;
        };
        auto shape = [](wah::ShapeModel m) {
            wah::ShapeSummary s;
            s.model = m; s.center_freq = 500.0f; s.q = 4.0f; s.peak_gain_db = 12.0f; s.kept = 4; s.valid = true;
            return s;
        };
        // 11 positions, the centre on 450 * (2100/450)^position: every position has data, both holds and both shapes are good.
        auto make = [&]() {
            wah::CaptureSummary s;
            s.valid = true;
            s.curve.valid = true;
            for (int i = 0; i < 11; i++) {
                const float p = (float)i / 10.0f;
                s.curve.position.push_back(p);
                s.curve.center_freq.push_back((float)(450.0 * std::pow(2100.0 / 450.0, (double)p)));
            }
            s.holds.heel = hold(450.0f, 4.0f, 10.0f);
            s.holds.toe = hold(2100.0f, 6.0f, 14.0f);
            s.heel_shape = shape(wah::ShapeModel::BandPass2);
            s.toe_shape = shape(wah::ShapeModel::BandPass2);
            return s;
        };
        wah::Meta meta;
        meta.name = "Test pedal"; meta.author = "me"; meta.make = "Acme"; meta.model = "W1"; meta.description = "d"; meta.created = "2026-11-11";

        const wah::CaptureSummary good = make();
        const std::optional<wah::Profile> p = wah::profile_from_summary(good, meta);
        bool a = p.has_value() && p->curves.has_value();
        if (a) {
            const wah::Curves& c = *p->curves;
            a = c.position == good.curve.position && c.freq == good.curve.center_freq && c.q.size() == 2 && c.q[0] == 4.0f && c.q[1] == 6.0f &&
                c.gain_db.size() == 2 && c.gain_db[0] == 10.0f && c.gain_db[1] == 14.0f;
        }
        check(a, "37a a full summary: [curves] has the 11 positions and centres as measured, and q and gain_db as two values (heel, toe) from the holds, not copied to every position");
        check(p && p->filter.freq_heel == good.curve.center_freq.front() && p->filter.freq_toe == good.curve.center_freq.back() && p->filter.q_min == 4.0f &&
                  p->filter.q_max == 6.0f && p->filter.peak_gain_db == 12.0f && p->filter.topology == wah::Topology::Bandpass2Pole &&
                  p->filter.bass_shelf_db == 0.0f && p->filter.bass_shelf_freq == 200.0f,
              "37b the v1 [filter]: freq_heel and freq_toe are the ends of freq, q_min and q_max the lower and higher hold Q, peak_gain_db the mean of the two gains, 2-pole, the shelf at its defaults");
        {   // ends that are NOT the defaults of Filter (450, 2100), so the check tells "from the curves" from "left at the default"
            wah::CaptureSummary s = make();
            for (float& f : s.curve.center_freq) f *= 1.2f;
            const std::optional<wah::Profile> r = wah::profile_from_summary(s, meta);
            check(r && r->filter.freq_heel == s.curve.center_freq.front() && r->filter.freq_toe == s.curve.center_freq.back() &&
                      r->filter.freq_heel != wah::Filter().freq_heel && r->filter.freq_toe != wah::Filter().freq_toe,
                  "37b2 with ends that are not the defaults of Filter, freq_heel and freq_toe are still the first and the last freq of the curves");
        }
        check(p && p->meta.name == meta.name && p->meta.author == meta.author && p->meta.make == meta.make && p->meta.model == meta.model &&
                  p->meta.description == meta.description && p->meta.created == meta.created,
              "37c the meta is copied as the caller gave it (no clock in the function)");
        {   // a heel hold with the higher Q: q_min is still the lower of the two
            wah::CaptureSummary s = make();
            s.holds.heel.q = 7.0f;
            const std::optional<wah::Profile> r = wah::profile_from_summary(s, meta);
            check(r && r->filter.q_min == 6.0f && r->filter.q_max == 7.0f && r->curves && r->curves->q[0] == 7.0f && r->curves->q[1] == 6.0f,
                  "37d a heel Q above the toe Q: q_min and q_max are the lower and the higher (so q_min <= q_max), the curves keep (heel, toe) order");
        }

        // Positions with no data.
        {
            wah::CaptureSummary s = make();
            s.curve.center_freq[3] = 0.0f;
            s.curve.center_freq[4] = 0.0f;
            const std::optional<wah::Profile> r = wah::profile_from_summary(s, meta);
            bool ok = r && r->curves && r->curves->position.size() == 9 && r->curves->freq.size() == 9;
            if (ok) {
                const wah::Curves& c = *r->curves;
                ok = c.position[2] == good.curve.position[2] && c.position[3] == good.curve.position[5] && c.freq[3] == good.curve.center_freq[5] &&
                     c.position.front() == 0.0f && c.position.back() == 1.0f;
            }
            check(ok, "37e two positions with no data (centre 0) are left out of [curves], position and freq together; the others keep their values and order");
            wah::CaptureSummary h = make(); h.curve.center_freq.front() = 0.0f;
            wah::CaptureSummary t = make(); t.curve.center_freq.back() = 0.0f;
            check(!wah::profile_from_summary(h, meta) && !wah::profile_from_summary(t, meta), "37f no centre at the heel, or none at the toe: no profile");
        }

        // The holds.
        {
            wah::CaptureSummary s = make();
            s.holds.toe.valid = false;
            const std::optional<wah::Profile> one = wah::profile_from_summary(s, meta);
            s.holds.heel.valid = false;
            const std::optional<wah::Profile> none = wah::profile_from_summary(s, meta);
            wah::CaptureSummary bad = make();
            bad.holds.heel.q = std::numeric_limits<float>::quiet_NaN();
            const std::optional<wah::Profile> nan = wah::profile_from_summary(bad, meta);
            wah::CaptureSummary inf = make();
            inf.holds.toe.q = std::numeric_limits<float>::infinity();
            wah::CaptureSummary gnan = make();
            gnan.holds.toe.peak_gain_db = std::numeric_limits<float>::quiet_NaN();
            const std::optional<wah::Profile> rinf = wah::profile_from_summary(inf, meta), rgnan = wah::profile_from_summary(gnan, meta);
            bool ok = true;
            for (const std::optional<wah::Profile>* r : {&one, &none, &nan, &rinf, &rgnan}) {
                ok = ok && r->has_value() && (*r)->curves && (*r)->curves->q.empty() && (*r)->curves->gain_db.empty() && (*r)->filter.q_min == 4.0f &&
                     (*r)->filter.q_max == 4.0f && (*r)->filter.peak_gain_db == 18.0f && (*r)->curves->freq.size() == 11;
            }
            check(ok, "37g only one hold usable, none, or a hold with a Q that is NaN or infinite or a gain that is NaN: the curve is kept, q and gain_db are left out, and the Filter keeps its defaults (4, 4, 18 dB)");
        }

        // The shapes.
        {
            wah::CaptureSummary s4 = make();
            s4.heel_shape = shape(wah::ShapeModel::BandPass4); s4.toe_shape = shape(wah::ShapeModel::BandPass4);
            const std::optional<wah::Profile> r4 = wah::profile_from_summary(s4, meta);
            check(r4 && r4->filter.topology == wah::Topology::Bandpass4Pole && r4->curves, "37h two 4-pole shapes: bandpass_4pole, with the curves");

            wah::CaptureSummary ss = make();
            ss.heel_shape = shape(wah::ShapeModel::BandPass2Shelf); ss.toe_shape = shape(wah::ShapeModel::BandPass2Shelf);
            ss.heel_shape.shelf_gain_db = -2.0f; ss.heel_shape.shelf_freq_hz = 180.0f;
            ss.toe_shape.shelf_gain_db = -2.4f; ss.toe_shape.shelf_freq_hz = 220.0f;
            const std::optional<wah::Profile> rs = wah::profile_from_summary(ss, meta);
            check(rs && rs->filter.topology == wah::Topology::Bandpass2Pole && std::abs(rs->filter.bass_shelf_db - -2.2f) < 1e-5f &&
                      std::abs(rs->filter.bass_shelf_freq - 200.0f) < 1e-3f,
                  "37i two 2-pole shapes with shelf: bandpass_2pole, and the shelf is the mean of the two ends (-2.2 dB at 200 Hz)");
            ss.toe_shape.valid = false;
            const std::optional<wah::Profile> r1 = wah::profile_from_summary(ss, meta);
            check(r1 && r1->filter.bass_shelf_db == -2.0f && r1->filter.bass_shelf_freq == 180.0f, "37j only the heel shape valid: the shelf is the heel's");

            wah::CaptureSummary sn = make();
            sn.heel_shape.valid = false; sn.toe_shape.valid = false;
            const std::optional<wah::Profile> rn = wah::profile_from_summary(sn, meta);
            check(rn && rn->filter.topology == wah::Topology::Bandpass2Pole && rn->filter.bass_shelf_db == 0.0f && rn->curves, "37k no valid shape: the default topology and shelf, and the curves");

            wah::CaptureSummary sm = make();
            sm.toe_shape = shape(wah::ShapeModel::Measured);
            wah::CaptureSummary sm2 = make();
            sm2.heel_shape = shape(wah::ShapeModel::Measured);
            wah::CaptureSummary sd = make();
            sd.toe_shape = shape(wah::ShapeModel::BandPass4);
            wah::CaptureSummary se = make();
            se.heel_shape = shape(wah::ShapeModel::BandPass2Shelf);  // 2-pole at the toe: the models differ
            check(!wah::profile_from_summary(sm, meta) && !wah::profile_from_summary(sm2, meta) && !wah::profile_from_summary(sd, meta) && !wah::profile_from_summary(se, meta),
                  "37l a Measured shape at one end only (the other is modelled), or two valid shapes of different models: no profile (round 34: both Measured is 41a)");
            wah::CaptureSummary si = make();
            si.heel_shape = shape(wah::ShapeModel::Measured); si.heel_shape.valid = false;
            check(wah::profile_from_summary(si, meta).has_value(), "37m a shape that is not valid does not count, whatever its model");
            wah::CaptureSummary sb = make();
            sb.heel_shape = shape(wah::ShapeModel::BandPass2Shelf); sb.toe_shape = shape(wah::ShapeModel::BandPass2Shelf);
            sb.heel_shape.shelf_gain_db = std::numeric_limits<float>::infinity(); sb.heel_shape.shelf_freq_hz = 200.0f;
            sb.toe_shape.shelf_gain_db = -2.0f; sb.toe_shape.shelf_freq_hz = 200.0f;
            wah::CaptureSummary sz = sb;
            sz.heel_shape.shelf_gain_db = -2.0f; sz.heel_shape.shelf_freq_hz = 0.0f;
            check(!wah::profile_from_summary(sb, meta) && !wah::profile_from_summary(sz, meta), "37n a shelf with a gain that is not finite, or a corner not above 0: no profile");

            // ── Round 34 (D71): two Measured ends give [shape_heel] and [shape_toe] ──────────────────────────────────
            auto measured = [&](float db0, int cells = wah::kShapeTablePoints) {
                wah::ShapeSummary m = shape(wah::ShapeModel::Measured);
                m.center_freq = 0.0f; m.q = 0.0f; m.peak_gain_db = 0.0f;
                for (int i = 0; i < cells; i++) { m.table_freq_hz.push_back(20.0f * std::pow(250.0f, (float)i / (float)(cells - 1))); m.table_db.push_back(db0 + 0.1f * (float)i - 3.0f); }
                return m;
            };
            wah::CaptureSummary mm = make();
            mm.heel_shape = measured(0.0f); mm.toe_shape = measured(5.0f);
            const std::optional<wah::Profile> rm = wah::profile_from_summary(mm, meta);
            check(rm && rm->shape && rm->curves && rm->shape->heel.freq == mm.heel_shape.table_freq_hz && rm->shape->heel.db == mm.heel_shape.table_db &&
                      rm->shape->toe.freq == mm.toe_shape.table_freq_hz && rm->shape->toe.db == mm.toe_shape.table_db && (int)rm->shape->heel.freq.size() == wah::kShapeTablePoints &&
                      rm->filter.topology == wah::Topology::Bandpass2Pole && rm->filter.bass_shelf_db == 0.0f,
                  "41a two Measured shapes: a profile with the two tables as they are (64 cells each), the curves, and the default topology and shelf");
            wah::CaptureSummary h1 = make(), t1 = make(), hn = make(), tm = make();
            h1.heel_shape = measured(0.0f);                                     // toe is a 2-pole
            t1.toe_shape = measured(0.0f);                                      // heel is a 2-pole
            hn.heel_shape = measured(0.0f); hn.toe_shape.valid = false;         // the toe has no valid shape
            tm.heel_shape = measured(0.0f); tm.toe_shape = shape(wah::ShapeModel::BandPass2Shelf);
            check(!wah::profile_from_summary(h1, meta) && !wah::profile_from_summary(t1, meta) && !wah::profile_from_summary(hn, meta) && !wah::profile_from_summary(tm, meta),
                  "41b a Measured end next to a modelled end, or next to one with no valid shape: no profile (both or none)");
            std::vector<wah::CaptureSummary> broken(6, mm);
            broken[0].heel_shape.table_freq_hz.clear(); broken[0].heel_shape.table_db.clear();
            broken[1].toe_shape.table_db.pop_back();
            broken[2].heel_shape.table_freq_hz[10] = broken[2].heel_shape.table_freq_hz[9];
            broken[3].toe_shape.table_db[5] = std::numeric_limits<float>::quiet_NaN();
            broken[4].heel_shape = measured(0.0f, 129);
            broken[5].toe_shape.table_freq_hz[0] = 0.0f;
            bool none = true;
            for (const auto& b : broken) none = none && !wah::profile_from_summary(b, meta);
            check(none, "41c two Measured shapes with a table that the format refuses (empty, sizes differ, freq not rising, a nan, 129 cells, a freq of 0): no profile");
            wah::CaptureSummary small = make();
            small.heel_shape = measured(0.0f, 2); small.toe_shape = measured(1.0f, 128);
            const std::optional<wah::Profile> rsmall = wah::profile_from_summary(small, meta);
            check(rsmall && rsmall->shape && rsmall->shape->heel.freq.size() == 2 && rsmall->shape->toe.freq.size() == 128, "41d tables of 2 and of 128 cells are the limits of the format and give a profile");
            // The whole way: summary -> profile -> text -> profile.
            const std::optional<wah::Profile> again = wah::toml_to_profile(wah::profile_to_toml(*rm));
            check(again && again->shape && again->shape->heel.db == rm->shape->heel.db && again->shape->toe.freq == rm->shape->toe.freq && again->curves && again->curves->freq == rm->curves->freq,
                  "41e a Measured profile written and read back keeps both tables and the curves bit for bit");
        }

        // The refusals.
        {
            wah::CaptureSummary a1 = make(); a1.valid = false;
            wah::CaptureSummary a2 = make(); a2.curve.valid = false;
            wah::CaptureSummary a3 = make(); a3.curve.center_freq.pop_back();
            wah::CaptureSummary a4 = make(); a4.curve.position.front() = 0.01f;
            wah::CaptureSummary a5 = make(); a5.curve.position.back() = 0.99f;
            wah::CaptureSummary a6 = make(); a6.curve.position[5] = a6.curve.position[4];
            wah::CaptureSummary a7 = make(); a7.curve.center_freq[5] = -1.0f;
            wah::CaptureSummary a8 = make(); a8.curve.center_freq[5] = std::numeric_limits<float>::infinity();
            wah::CaptureSummary a9 = make(); a9.curve.center_freq[5] = std::numeric_limits<float>::quiet_NaN();
            wah::CaptureSummary a10; a10.valid = true; a10.curve.valid = true;
            check(!wah::profile_from_summary(a1, meta) && !wah::profile_from_summary(a2, meta) && !wah::profile_from_summary(a3, meta) &&
                      !wah::profile_from_summary(a4, meta) && !wah::profile_from_summary(a5, meta) && !wah::profile_from_summary(a6, meta) &&
                      !wah::profile_from_summary(a7, meta) && !wah::profile_from_summary(a8, meta) && !wah::profile_from_summary(a9, meta) &&
                      !wah::profile_from_summary(a10, meta),
                  "37o no profile for: a summary or a curve that is not valid, sizes that differ, a first position that is not 0, a last that is not 1, positions that do not rise, a centre that is negative, infinite or NaN, an empty curve");

            // 128 positions with data is the most the format has; 129 is refused (the reader would refuse it).
            auto many = [&](int n) {
                wah::CaptureSummary s = make();
                s.curve.position.clear(); s.curve.center_freq.clear();
                for (int i = 0; i < n; i++) {
                    const float p = (float)i / (float)(n - 1);
                    s.curve.position.push_back(p);
                    s.curve.center_freq.push_back(450.0f + 1500.0f * p);
                }
                return s;
            };
            const std::optional<wah::Profile> m128 = wah::profile_from_summary(many(128), meta);
            check(m128 && m128->curves && m128->curves->position.size() == 128 && !wah::profile_from_summary(many(129), meta),
                  "37p 128 positions with data give a profile, 129 do not (MAX_CURVE_POINTS)");
        }

        // The reader accepts what the function gives: the profile goes through TOML text and comes back the same.
        {
            wah::CaptureSummary gaps = make();
            gaps.curve.center_freq[2] = 0.0f;
            wah::CaptureSummary shelf = make();
            shelf.heel_shape = shape(wah::ShapeModel::BandPass2Shelf); shelf.toe_shape = shape(wah::ShapeModel::BandPass2Shelf);
            shelf.heel_shape.shelf_gain_db = -2.0f; shelf.heel_shape.shelf_freq_hz = 180.0f;
            shelf.toe_shape.shelf_gain_db = -2.0f; shelf.toe_shape.shelf_freq_hz = 180.0f;
            wah::CaptureSummary noq = make();
            noq.holds.heel.valid = false;
            bool ok = true;
            for (const wah::CaptureSummary& s : std::vector<wah::CaptureSummary>{good, gaps, shelf, noq}) {
                const std::optional<wah::Profile> w = wah::profile_from_summary(s, meta);
                const std::optional<wah::Profile> back = w ? wah::toml_to_profile(wah::profile_to_toml(*w)) : std::nullopt;
                ok = ok && w && back && back->curves && back->curves->position == w->curves->position && back->curves->freq == w->curves->freq &&
                     back->curves->q == w->curves->q && back->curves->gain_db == w->curves->gain_db && back->filter.topology == w->filter.topology &&
                     back->filter.freq_heel == w->filter.freq_heel && back->filter.freq_toe == w->filter.freq_toe && back->meta.name == w->meta.name;
            }
            check(ok, "37q the profile of a full summary, one with a gap, one with a shelf and one without Q is written as TOML and read back by the reader, with the same curves and ends");
        }
    }

    // 37r End to end (round 30): the pedals of 35 through the virtual pedal, summarised, turned into a profile, written as TOML and read back.
    //     The references are the sample profiles' own parameters; the tolerances are those of 35 (the holds within 2 % and 10 %, the curve 2 % for
    //     the Cry Baby and 3 % for the Vox), because the profile only carries what the summary measured. The gain is NOT checked against the
    //     profile's: that is the known defect of the engine of 35c (M7).
    {
        const std::string dir = WAH_EXAMPLES_DIR;
        const std::optional<wah::Profile> cry = wah::load_toml(dir + "/CryBaby_GCB95_Modern.wah"), vox = wah::load_toml(dir + "/Vox_V846_Vintage.wah");
        if (cry && vox) {
            const int n = 1 << 18;
            auto capture = [&](const wah::Profile& p, unsigned seed) {
                const std::vector<float> st = wah::generate_stimulus(n, seed, 0.1f);
                const wah::SimulatedPass sp = wah::simulate_pass(def, p, st, (float)kFs);
                return wah::analyse_pass(def, st.data(), sp.dut.data(), sp.loopback.data(), n, (float)kFs, kMax);
            };
            auto rel = [](double v, double truth) { return std::abs(v - truth) / truth; };
            wah::Meta meta;
            meta.name = "Captured";
            meta.created = "2026-11-11";
            // The profile through the reader: what a user would load.
            auto through_toml = [](const wah::Profile& p) { return wah::toml_to_profile(wah::profile_to_toml(p)); };

            std::vector<wah::PassAnalysis> cp;
            for (unsigned seed = 7; seed < 11; seed++) cp.push_back(capture(*cry, seed));
            const wah::CaptureSummary cs = wah::capture_summary(cp, wah::CaptureTarget::Normal);
            const std::optional<wah::Profile> cprof = wah::profile_from_summary(cs, meta);
            const std::optional<wah::Profile> cback = cprof ? through_toml(*cprof) : std::nullopt;
            const wah::Filter& cf = cry->filter;
            bool ok = cback && cback->curves && cback->curves->position.size() == 11 && cback->curves->q.size() == 2 && cback->curves->gain_db.size() == 2 &&
                      cback->filter.topology == wah::Topology::Bandpass2Pole && rel(cback->filter.freq_heel, cf.freq_heel) < 0.02 &&
                      rel(cback->filter.freq_toe, cf.freq_toe) < 0.02 && rel(cback->curves->q[0], cf.q_min) < 0.10 && rel(cback->curves->q[1], cf.q_max) < 0.10 &&
                      cback->filter.freq_heel == cback->curves->freq.front() && cback->filter.freq_toe == cback->curves->freq.back();
            for (size_t i = 0; ok && i < cback->curves->position.size(); i++)
                ok = rel(cback->curves->freq[i], cf.freq_heel * std::pow((double)cf.freq_toe / cf.freq_heel, cback->curves->position[i])) < 0.02;
            check(ok, "37r Cry Baby, 4 passes: the profile read back from TOML has 11 positions within 2 % of the sweep, the ends within 2 %, Q (heel, toe) within 10 %, 2-pole");

            std::vector<wah::PassAnalysis> vp;
            for (unsigned seed = 7; seed < 10; seed++) vp.push_back(capture(*vox, seed));
            const wah::CaptureSummary vs = wah::capture_summary(vp, wah::CaptureTarget::Normal);
            const std::optional<wah::Profile> vprof = wah::profile_from_summary(vs, meta);
            const std::optional<wah::Profile> vback = vprof ? through_toml(*vprof) : std::nullopt;
            const wah::Filter& vf = vox->filter;
            ok = vback && vback->curves && vback->curves->position.size() == 11 && vback->filter.topology == wah::Topology::Bandpass2Pole &&
                 rel(vback->filter.freq_heel, vf.freq_heel) < 0.02 && rel(vback->filter.freq_toe, vf.freq_toe) < 0.02 ;
            for (size_t i = 0; ok && i < vback->curves->position.size(); i++)
                ok = rel(vback->curves->freq[i], vf.freq_heel * std::pow((double)vf.freq_toe / vf.freq_heel, vback->curves->position[i])) < 0.03;
            check(ok, "37s Vox (shelf -2.1 dB), 3 passes: the profile read back has 11 positions within 3 % of the sweep, the ends within 2 %, 2-pole (the shelf: see 37t)");
            // The engine's shelf is a stub (see 35d), so the shelf the fit reads through the virtual pedal is not the profile's (-2.1 dB at 200 Hz).
            // Measured in round 30: +20 dB at 22.8 Hz, outside the ranges of the spec (-6 to +3 dB, 100 to 400 Hz). Since round 31 (D68)
            // profile_from_summary leaves the Filter's defaults (0 dB, 200 Hz) for a shelf outside the ranges (see 38). This check turns red when
            // M7 fixes the shelf and the Vox's comes back inside the ranges: then the shelf is read and this check goes.
            check(vback && vback->filter.bass_shelf_db == 0.0f && vback->filter.bass_shelf_freq == 200.0f && vs.heel_shape.model == wah::ShapeModel::BandPass2Shelf &&
                      (vs.heel_shape.shelf_gain_db > 3.0f || vs.heel_shape.shelf_freq_hz < 100.0f),
                  "37t KNOWN DEFECT of the engine (M7): the shelf read for the Vox is outside the spec's ranges (it was +20 dB at 22.8 Hz; the profile's is -2.1 dB at 200 Hz), so the profile keeps the defaults 0 dB at 200 Hz (D68)");
        } else {
            check(false, "37r the two sample profiles load");
        }
    }

    // ── Round 31 (D68): a shelf outside the ranges of the spec (-6 to +3 dB, 100 to 400 Hz) is not written: the Filter keeps its defaults (0 dB, 200 Hz),
    //    the profile still has its topology and its curves. The range is applied to the value that would be written (the mean of the ends).
    {
        auto summary = [](float heel_db, float heel_hz, float toe_db, float toe_hz) {
            wah::CaptureSummary s;
            s.valid = true;
            s.curve.valid = true;
            for (int i = 0; i < 11; i++) {
                const float p = (float)i / 10.0f;
                s.curve.position.push_back(p);
                s.curve.center_freq.push_back(450.0f + 1650.0f * p);
            }
            for (wah::ShapeSummary* e : {&s.heel_shape, &s.toe_shape}) {
                e->model = wah::ShapeModel::BandPass2Shelf; e->center_freq = 500.0f; e->q = 4.0f; e->peak_gain_db = 12.0f; e->kept = 4; e->valid = true;
            }
            s.heel_shape.shelf_gain_db = heel_db; s.heel_shape.shelf_freq_hz = heel_hz;
            s.toe_shape.shelf_gain_db = toe_db; s.toe_shape.shelf_freq_hz = toe_hz;
            return s;
        };
        wah::Meta m;
        m.name = "shelf";
        struct Case { const char* what; float hdb, hhz, tdb, thz; bool applied; float want_db, want_hz; };
        const Case cases[] = {
            {"-6 dB, 100 Hz (both edges)", -6.0f, 100.0f, -6.0f, 100.0f, true, -6.0f, 100.0f},
            {"+3 dB, 400 Hz (both edges)", 3.0f, 400.0f, 3.0f, 400.0f, true, 3.0f, 400.0f},
            {"-6.5 dB", -6.5f, 200.0f, -6.5f, 200.0f, false, 0.0f, 200.0f},
            {"+3.5 dB", 3.5f, 200.0f, 3.5f, 200.0f, false, 0.0f, 200.0f},
            {"99 Hz", -2.0f, 99.0f, -2.0f, 99.0f, false, 0.0f, 200.0f},
            {"401 Hz", -2.0f, 401.0f, -2.0f, 401.0f, false, 0.0f, 200.0f},
            {"the mean inside the ranges, one end outside (-8 and -2 dB: -5)", -8.0f, 200.0f, -2.0f, 200.0f, true, -5.0f, 200.0f},
            {"the mean outside the ranges, one end inside (-2 and +20 dB: +9)", -2.0f, 200.0f, 20.0f, 200.0f, false, 0.0f, 200.0f},
            {"the corner mean outside (50 and 100 Hz: 75)", -2.0f, 50.0f, -2.0f, 100.0f, false, 0.0f, 200.0f},
        };
        for (const Case& k : cases) {
            const std::optional<wah::Profile> r = wah::profile_from_summary(summary(k.hdb, k.hhz, k.tdb, k.thz), m);
            const bool ok = r && r->curves && r->curves->position.size() == 11 && r->filter.topology == wah::Topology::Bandpass2Pole &&
                            std::abs(r->filter.bass_shelf_db - k.want_db) < 1e-4f && std::abs(r->filter.bass_shelf_freq - k.want_hz) < 1e-3f;
            const std::string label = std::string("38 shelf ") + k.what + (k.applied ? ": written as it is" : ": not written, the Filter keeps 0 dB at 200 Hz");
            check(ok, label.c_str());
        }
        // The shelf written is read back as it was.
        const std::optional<wah::Profile> r = wah::profile_from_summary(summary(-2.0f, 180.0f, -2.4f, 220.0f), m);
        const std::optional<wah::Profile> back = r ? wah::toml_to_profile(wah::profile_to_toml(*r)) : std::nullopt;
        check(back && std::abs(back->filter.bass_shelf_db - -2.2f) < 1e-5f && std::abs(back->filter.bass_shelf_freq - 200.0f) < 1e-3f, "38b a shelf inside the ranges goes through TOML and comes back (-2.2 dB at 200 Hz)");
    }

    if (failures) {
        std::cout << "\n✗ Test FAILED: " << failures << " check(s)\n";
        return 1;
    }
    std::cout << "\n✓ Test PASSED\n";
    return 0;
}
