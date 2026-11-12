#include "wah_guided.h"
#include "wah_toml.h"

#include <algorithm>
#include <cmath>

namespace wah {

bool GuideSchedule::valid() const {
    return std::isfinite(heel_hold_s) && std::isfinite(rise_s) && std::isfinite(toe_hold_s) && std::isfinite(fall_s) &&
           heel_hold_s >= 1.0f && toe_hold_s >= 1.0f && rise_s > 0.0f && fall_s > 0.0f;
}

float GuideSchedule::period() const {
    return valid() ? heel_hold_s + rise_s + toe_hold_s + fall_s : 0.0f;
}

float GuideSchedule::position_at(float t) const {
    if (!valid() || !(t >= 0.0f) || !(t < period())) return -1.0f;  // NaN fails the first test
    if (t < heel_hold_s) return 0.0f;
    if (t < heel_hold_s + rise_s) return (t - heel_hold_s) / rise_s;
    if (t < heel_hold_s + rise_s + toe_hold_s) return 1.0f;
    return 1.0f - (t - heel_hold_s - rise_s - toe_hold_s) / fall_s;
}

void GuideSchedule::heel_hold(float& start_s, float& end_s) const {
    start_s = 0.0f;
    end_s = valid() ? heel_hold_s : 0.0f;
}

void GuideSchedule::toe_hold(float& start_s, float& end_s) const {
    start_s = end_s = 0.0f;
    if (!valid()) return;
    start_s = heel_hold_s + rise_s;
    end_s = start_s + toe_hold_s;
}

PassHolds measure_pass_holds(const GuideSchedule& schedule, const float* stimulus, const float* dut, const float* loopback,
                             int num_samples, float sample_rate, int max_delay,
                             float window_s, int segment_size, float settle_s,
                             float freq_min, float freq_max, float min_coherence) {
    PassHolds out;  // an invalid schedule has empty intervals, which fit_hold_shape refuses
    float hs, he, ts, te;
    schedule.heel_hold(hs, he);
    schedule.toe_hold(ts, te);
    out.heel = fit_hold_shape(stimulus, dut, loopback, num_samples, sample_rate, max_delay, hs, he,
                              window_s, segment_size, settle_s, freq_min, freq_max, min_coherence);
    out.toe = fit_hold_shape(stimulus, dut, loopback, num_samples, sample_rate, max_delay, ts, te,
                             window_s, segment_size, settle_s, freq_min, freq_max, min_coherence);
    out.valid = out.heel.valid && out.toe.valid;
    return out;
}

HoldEstimate hold_estimate_of(const HoldShape& hold) {
    HoldEstimate e;
    const ShapeFit& s = hold.shape;
    if (!hold.valid || !s.valid || hold.windows_checked < 1) return e;
    if (s.model != ShapeModel::BandPass2 && s.model != ShapeModel::BandPass2Shelf) return e;
    e.center_freq = s.center_freq;
    e.q = s.q;
    e.peak_gain_db = s.peak_gain_db;
    e.windows_used = hold.windows_checked;
    e.valid = true;
    return e;
}

std::vector<PassPoint> pass_points(const GuideSchedule& schedule, const std::vector<TrackPoint>& tracks,
                                   float window_s, float settle_s) {
    std::vector<PassPoint> out;
    // An invalid schedule has period 0 and so no window inside the pass.
    if (!std::isfinite(window_s) || !(window_s > 0.0f) || !std::isfinite(settle_s) || !(settle_s >= 0.0f)) return out;
    const double corner[3] = {schedule.heel_hold_s, (double)schedule.heel_hold_s + schedule.rise_s,
                              (double)schedule.heel_hold_s + schedule.rise_s + schedule.toe_hold_s};
    for (const TrackPoint& p : tracks) {
        const float f = p.estimate.center_freq;
        if (!p.estimate.valid || !std::isfinite(f) || !(f > 0.0f) || !std::isfinite(p.time_s)) continue;
        const double start = (double)p.time_s - 0.5 * window_s, end = (double)p.time_s + 0.5 * window_s;
        if (start < settle_s || end > schedule.period()) continue;                      // D40, and the end of the pass
        bool turnaround = false;
        for (double c : corner) turnaround |= start < c && c < end;                      // D32
        if (turnaround) continue;
        PassPoint q;
        q.position = schedule.position_at(p.time_s);
        q.center_freq = f;
        q.weight = 1.0f;
        out.push_back(q);
    }
    return out;
}

std::vector<PassPoint> shape_pass_points(const GuideSchedule& schedule, const std::vector<ShapeTrackPoint>& tracks,
                                         float window_s, float settle_s) {
    // The centre of the band-pass of each window; a Measured shape and None have none, and become windows that are not valid.
    std::vector<TrackPoint> windows;
    for (const ShapeTrackPoint& s : tracks) {
        TrackPoint w;
        w.time_s = s.time_s;
        const ShapeModel m = s.shape.model;
        if (s.shape.valid && (m == ShapeModel::BandPass2 || m == ShapeModel::BandPass2Shelf || m == ShapeModel::BandPass4)) {
            w.estimate.center_freq = s.shape.center_freq;  // for a 4-pole, the lower centre
            w.estimate.valid = true;
        }
        windows.push_back(w);
    }
    return pass_points(schedule, windows, window_s, settle_s);
}

PassAnalysis analyse_pass(const GuideSchedule& schedule, const float* stimulus, const float* dut, const float* loopback,
                          int num_samples, float sample_rate, int max_delay,
                          float freq_min, float freq_max, float min_coherence) {
    PassAnalysis out;
    if (!schedule.valid()) return out;
    // The tracker: window 0.25 s, segment 2048 (D32). It runs over the whole recording; pass_points() keeps what lies inside the pass.
    const std::vector<ShapeTrackPoint> tracks = track_shape(stimulus, dut, loopback, num_samples, sample_rate, max_delay,
                                                          0.25f, 2048, freq_min, freq_max, min_coherence);
    if (tracks.empty()) return out;  // a null buffer, a loopback without a latency, a recording that is too short
    out.points = shape_pass_points(schedule, tracks, 0.25f, 0.2f);
    out.holds = measure_pass_holds(schedule, stimulus, dut, loopback, num_samples, sample_rate, max_delay,
                                   0.5f, 4096, 0.2f, freq_min, freq_max, min_coherence);
    out.valid = true;
    return out;
}

namespace {

// The points and the heel and toe estimates of every pass, in the order of the passes (the indices stay those of the input).
void split_passes(const std::vector<PassAnalysis>& passes, std::vector<std::vector<PassPoint>>& points,
                  std::vector<HoldEstimate>& heel, std::vector<HoldEstimate>& toe) {
    for (const PassAnalysis& p : passes) {
        points.push_back(p.points);
        heel.push_back(hold_estimate_of(p.holds.heel));
        toe.push_back(hold_estimate_of(p.holds.toe));
    }
}

}  // namespace

CaptureProgress capture_progress_of(const std::vector<PassAnalysis>& passes, CaptureTarget target, int num_points) {
    std::vector<std::vector<PassPoint>> points;
    std::vector<HoldEstimate> heel, toe;
    split_passes(passes, points, heel, toe);
    return capture_progress(points, heel, toe, target, num_points);
}

CaptureSummary capture_summary(const std::vector<PassAnalysis>& passes, CaptureTarget target, int num_points) {
    CaptureSummary out;
    std::vector<std::vector<PassPoint>> points;
    std::vector<HoldEstimate> heel, toe;
    split_passes(passes, points, heel, toe);
    out.curve = aggregate_passes(points, num_points);
    out.holds = aggregate_holds(heel, toe);
    out.progress = capture_progress(points, heel, toe, target, num_points);
    std::vector<HoldShape> heel_shapes, toe_shapes;
    for (const PassAnalysis& p : passes) { heel_shapes.push_back(p.holds.heel); toe_shapes.push_back(p.holds.toe); }
    out.heel_shape = aggregate_shapes(heel_shapes);  // reported; it does not enter the progress (D49, D51)
    out.toe_shape = aggregate_shapes(toe_shapes);
    out.valid = out.curve.valid && out.progress.valid;
    return out;
}

namespace {

bool finite_pos(float v) { return std::isfinite(v) && v > 0.0f; }

// The model a hold can be folded as, or None.
ShapeModel usable_model(const HoldShape& h) {
    const ShapeFit& s = h.shape;
    if (!h.valid || !s.valid) return ShapeModel::None;
    if (s.model == ShapeModel::BandPass2 || s.model == ShapeModel::BandPass2Shelf) {
        if (!finite_pos(s.center_freq) || !finite_pos(s.q) || !std::isfinite(s.peak_gain_db)) return ShapeModel::None;
        if (s.model == ShapeModel::BandPass2Shelf && (!finite_pos(s.shelf_freq_hz) || !std::isfinite(s.shelf_gain_db))) return ShapeModel::None;
        return s.model;
    }
    if (s.model == ShapeModel::Measured) {
        if ((int)s.table_freq_hz.size() != kShapeTablePoints || (int)s.table_db.size() != kShapeTablePoints) return ShapeModel::None;
        for (int i = 0; i < kShapeTablePoints; i++)
            if (!finite_pos(s.table_freq_hz[i]) || !std::isfinite(s.table_db[i])) return ShapeModel::None;
        return ShapeModel::Measured;
    }
    return ShapeModel::None;  // a 4-pole, None
}

float median_of(std::vector<float> v) {  // v is not empty
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
}

}  // namespace

ShapeSummary aggregate_shapes(const std::vector<HoldShape>& holds) {
    ShapeSummary out;
    std::vector<int> model_of(holds.size());
    int count[3] = {0, 0, 0};  // BandPass2, BandPass2Shelf, Measured
    int usable = 0;
    for (size_t i = 0; i < holds.size(); i++) {
        const ShapeModel m = usable_model(holds[i]);
        model_of[i] = m == ShapeModel::BandPass2 ? 0 : m == ShapeModel::BandPass2Shelf ? 1 : m == ShapeModel::Measured ? 2 : -1;
        if (model_of[i] >= 0) { count[model_of[i]]++; usable++; }
    }
    int win = -1;
    for (int m = 0; m < 3; m++) if (2 * count[m] > usable) win = m;  // more than half of the usable holds
    if (win < 0) {
        for (size_t i = 0; i < holds.size(); i++) out.dropped.push_back((int)i);
        return out;
    }

    std::vector<int> kept;
    for (size_t i = 0; i < holds.size(); i++) {
        bool keep = model_of[i] == win;
        if (keep && win == 2 && !kept.empty()) {  // the same grid as the first kept table, within 0.1 %
            const std::vector<float>& g0 = holds[kept[0]].shape.table_freq_hz;
            for (int k = 0; k < kShapeTablePoints && keep; k++)
                keep = std::abs(holds[i].shape.table_freq_hz[k] - g0[k]) <= 0.001f * g0[k];
        }
        if (keep) kept.push_back((int)i); else out.dropped.push_back((int)i);
    }

    auto med = [&](float ShapeFit::*field) {
        std::vector<float> v;
        for (int i : kept) v.push_back(holds[i].shape.*field);
        return median_of(v);
    };
    if (win == 2) {
        out.model = ShapeModel::Measured;
        out.table_freq_hz = holds[kept[0]].shape.table_freq_hz;
        for (int k = 0; k < kShapeTablePoints; k++) {
            std::vector<float> v;
            for (int i : kept) v.push_back(holds[i].shape.table_db[k]);
            out.table_db.push_back(median_of(v));
        }
    } else {
        out.model = win == 0 ? ShapeModel::BandPass2 : ShapeModel::BandPass2Shelf;
        out.center_freq = med(&ShapeFit::center_freq);
        out.q = med(&ShapeFit::q);
        out.peak_gain_db = med(&ShapeFit::peak_gain_db);
        if (win == 1) { out.shelf_freq_hz = med(&ShapeFit::shelf_freq_hz); out.shelf_gain_db = med(&ShapeFit::shelf_gain_db); }
    }
    out.kept = (int)kept.size();
    out.valid = true;
    return out;
}

namespace {

// The measurement chain: a one-pole high-pass (bilinear) and then a delay of latency zeros.
std::vector<float> through_chain(const std::vector<float>& x, float sample_rate, const MeasurementChain& chain) {
    std::vector<float> y(x.size(), 0.0f);
    double k = 0.0, b0 = 1.0, a1 = 0.0, x1 = 0.0, y1 = 0.0;
    const bool hp = chain.highpass_hz > 0.0f;
    if (hp) { k = std::tan(3.14159265358979323846 * chain.highpass_hz / sample_rate); b0 = 1.0 / (1.0 + k); a1 = (k - 1.0) / (k + 1.0); }
    for (size_t i = 0; i < x.size(); i++) {
        double v = x[i];
        if (hp) { v = b0 * x[i] - b0 * x1 - a1 * y1; x1 = x[i]; y1 = v; }
        const size_t j = i + (size_t)chain.latency_samples;
        if (j < y.size()) y[j] = (float)v;
    }
    return y;
}

}  // namespace

SimulatedPass simulate_pass(const GuideSchedule& schedule, const Profile& profile, const std::vector<float>& stimulus,
                            float sample_rate, const MeasurementChain& chain, int block_size) {
    SimulatedPass out;
    if (!schedule.valid() || stimulus.empty() || !(sample_rate > 0.0f) || !std::isfinite(sample_rate) || block_size < 1) return out;
    if (chain.latency_samples < 0 || !(chain.highpass_hz >= 0.0f) || !(chain.highpass_hz < 0.5f * sample_rate)) return out;  // NaN fails the comparisons

    Engine engine;
    engine.init(profile, sample_rate);
    std::vector<float> wet = stimulus;
    for (size_t start = 0; start < wet.size(); start += (size_t)block_size) {
        const int n = (int)std::min((size_t)block_size, wet.size() - start);
        float pos = schedule.position_at((float)(((double)start + 0.5 * n) / sample_rate));
        if (pos < 0.0f) pos = 0.0f;  // the pass is over: the pedal is back at the heel
        engine.set_position(pos);
        engine.process(wet.data() + start, n);
    }
    out.dut = through_chain(wet, sample_rate, chain);
    out.loopback = through_chain(stimulus, sample_rate, chain);
    out.valid = true;
    return out;
}

namespace {
// A hold whose Q and gain can go into a profile: valid, Q above 0, both finite.
bool hold_usable(const HoldSummary& h) {
    return h.valid && std::isfinite(h.q) && h.q > 0.0f && std::isfinite(h.peak_gain_db);
}
}  // namespace

std::optional<Profile> profile_from_summary(const CaptureSummary& summary, const Meta& meta) {
    if (!summary.valid || !summary.curve.valid) return std::nullopt;

    // [curves]: the positions with data; the first and the last (heel and toe) must have it, as the reader requires.
    const std::vector<float>& pos = summary.curve.position;
    const std::vector<float>& freq = summary.curve.center_freq;
    const size_t n = pos.size();
    if (n < 2 || freq.size() != n || pos.front() != 0.0f || pos.back() != 1.0f) return std::nullopt;
    Curves curves;
    for (size_t i = 0; i < n; i++) {
        if (!std::isfinite(pos[i]) || !std::isfinite(freq[i]) || freq[i] < 0.0f) return std::nullopt;
        if (i > 0 && !(pos[i] > pos[i - 1])) return std::nullopt;
        if (freq[i] == 0.0f) {  // no pass had data here: leave it out
            if (i == 0 || i == n - 1) return std::nullopt;
            continue;
        }
        curves.position.push_back(pos[i]);
        curves.freq.push_back(freq[i]);
    }
    if (curves.position.size() > (size_t)MAX_CURVE_POINTS) return std::nullopt;

    Profile profile;
    profile.meta = meta;
    profile.filter.freq_heel = curves.freq.front();
    profile.filter.freq_toe = curves.freq.back();

    // q and gain_db: only the two ends, and only when both holds are usable.
    const HoldSummary& heel = summary.holds.heel;
    const HoldSummary& toe = summary.holds.toe;
    if (hold_usable(heel) && hold_usable(toe)) {
        curves.q = {heel.q, toe.q};
        curves.gain_db = {heel.peak_gain_db, toe.peak_gain_db};
        profile.filter.q_min = std::min(heel.q, toe.q);
        profile.filter.q_max = std::max(heel.q, toe.q);
        profile.filter.peak_gain_db = 0.5f * (heel.peak_gain_db + toe.peak_gain_db);
    }

    // Topology and shelf from the shapes that are valid; two models cannot be one pedal. A Measured shape goes into the [shape_heel] or
    // [shape_toe] block (D71): both ends Measured give both tables, and a Measured end next to any other end gives no profile (both or none).
    bool have_model = false;
    int measured = 0;
    Shape shape;
    ShapeModel model = ShapeModel::None;
    float shelf_db = 0.0f, shelf_freq = 0.0f;
    int shelves = 0;
    for (const ShapeSummary* s : {&summary.heel_shape, &summary.toe_shape}) {
        if (!s->valid) continue;
        if (s->model == ShapeModel::Measured) {
            measured++;
            (s == &summary.heel_shape ? shape.heel : shape.toe) = ShapeTable{s->table_freq_hz, s->table_db};
            continue;
        }
        if (have_model && s->model != model) return std::nullopt;
        have_model = true;
        model = s->model;
        if (s->model == ShapeModel::BandPass2Shelf) {
            if (!std::isfinite(s->shelf_gain_db) || !std::isfinite(s->shelf_freq_hz) || !(s->shelf_freq_hz > 0.0f)) return std::nullopt;
            shelf_db += s->shelf_gain_db;
            shelf_freq += s->shelf_freq_hz;
            shelves++;
        }
    }
    if (measured == 1) return std::nullopt;
    if (measured == 2) {
        if (!check_shape(shape).empty()) return std::nullopt;  // a table the format would refuse is no table
        profile.shape = std::move(shape);
    }
    if (have_model) profile.filter.topology = (model == ShapeModel::BandPass4) ? Topology::Bandpass4Pole : Topology::Bandpass2Pole;
    if (shelves > 0) {
        // D68: the shelf is written only if it is inside the ranges of the spec (-6 to +3 dB, 100 to 400 Hz): the engine's shelf is a stub (M7), so a
        // fit can read a shelf that is not the pedal's. Otherwise the Filter keeps its defaults. The ranges apply to the value that would be written.
        const float db = shelf_db / (float)shelves, hz = shelf_freq / (float)shelves;
        if (db >= -6.0f && db <= 3.0f && hz >= 100.0f && hz <= 400.0f) {
            profile.filter.bass_shelf_db = db;
            profile.filter.bass_shelf_freq = hz;
        }
    }

    profile.curves = std::move(curves);
    return profile;
}

} // namespace wah
