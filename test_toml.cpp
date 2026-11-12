/**
 * test_toml.cpp
 * Test TOML serialization and parsing.
 *
 * Round 29 (format v1.1, D6): the optional [curves] block and `format_version`. The good file is written by hand here, not by the
 * writer; every refused file is the good one with ONE change (replace_once insists the text to change is there exactly once), so
 * the refusal has one possible reason. A v1 profile must be written exactly as before (the golden text) and read as before.
 *
 * Round 31 (D68): check_curves() is the one check of a Curves, used by the reader and the writer; the writer gives nothing (an empty
 * string, a false) for a Curves the reader would refuse; the reader says why it refuses (an optional error string).
 *
 * Round 32 (the points noticed in round 29): a comment after a value, a string with a line break or a quote, a section header
 * written twice, and a float written as the shortest text that reads back exact (group 32).
 *
 * Round 33 (D70, the silent losses): a value that is not a finite number, an unknown topology or curve, a custom lut that is not
 * 128 numbers, a key written twice, and a denormal are refused with a reason (group 33), each from the good file with ONE change.
 *
 * Round 34 (D71, M6 ends): the blocks [shape_heel] and [shape_toe] of format v1.1 (a Measured shape at the heel and at the toe):
 * the reader, the writer that refuses, check_shape (one check for both) and the engine that ignores them (group 34).
 *
 * Exit codes: 0 all pass, 1 at least one check fails.
 */

#include "libwah.h"
#include "wah_toml.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "ok   " : "FAIL ") << what << "\n";
    if (!ok) failures++;
}

// What the v1 writer produced for the profile of main() before round 29.
const std::string kGoldenV1 = R"(
[meta]
name = "Test Wah"
author = "claude"
make = "Vox"
model = "V847"
description = "A test profile for TOML serialization"
created = "2026-06-17T14:00:00Z"

[filter]
topology = "bandpass_2pole"
freq_heel = 450
freq_toe = 2100
q_min = 4.2
q_max = 4.8
peak_gain_db = 17.5
bass_shelf_db = -1.5
bass_shelf_freq = 200

[sweep]
curve = "log"
lut = []

[levels]
input_gain_db = 0
output_gain_db = 0
mix = 1

[dynamics]
resonance_low = 0.1
resonance_mid = 0.3
resonance_high = 0.5
)";

const std::string kVer = "format_version = \"1.1\"\n";
const std::string kCurves =
    "[curves]\n"
    "position = [0.0, 0.25, 0.5, 0.75, 1.0]\n"
    "freq = [420, 600, 900, 1400, 2180]\n"
    "q = [4.1, 4.2, 4.3, 4.4, 4.6]\n"
    "gain_db = [17.0, 17.1, 17.2, 17.3, 17.4]\n";

std::string replace_once(std::string s, const std::string& from, const std::string& to) {
    const size_t at = s.find(from);
    if (at == std::string::npos || s.find(from, at + 1) != std::string::npos) {
        std::cout << "FAIL test setup: \"" << from << "\" must occur exactly once\n";
        failures++;
        return s;
    }
    return s.replace(at, from.size(), to);
}

// The v1 text of kGoldenV1 (it starts with a newline) with a version line at the end of [meta] and a block at the end.
std::string v11(const std::string& version_line, const std::string& curves_block) {
    std::string t = kGoldenV1.substr(1);
    t = replace_once(t, "created = \"2026-06-17T14:00:00Z\"\n", "created = \"2026-06-17T14:00:00Z\"\n" + version_line);
    return curves_block.empty() ? t : t + "\n" + curves_block;
}
std::string with(const std::string& from, const std::string& to) { return v11(kVer, replace_once(kCurves, from, to)); }

bool parses(const std::string& text) { return wah::toml_to_profile(text).has_value(); }
void refused(const std::string& text, const char* what) { check(!parses(text), what); }
void accepted(const std::string& text, const char* what) { check(parses(text), what); }

// A [curves] block with n evenly spaced positions and a rising frequency (n >= 2).
std::string points_curves(int n) {
    std::string pos, fr;
    char b[48];
    for (int i = 0; i < n; i++) {
        std::snprintf(b, sizeof b, "%s%.9g", i ? ", " : "", (double)i / (n - 1));
        pos += b;
        std::snprintf(b, sizeof b, "%s%d", i ? ", " : "", 400 + 10 * i);
        fr += b;
    }
    return "[curves]\nposition = [" + pos + "]\nfreq = [" + fr + "]\n";
}

}  // namespace

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

    if (!success) failures++;

    // ── Round 29: format v1.1 ────────────────────────────────────────────────────────────────────────────────────
    const std::vector<float> kPos = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    // 29a: the good file, written by hand.
    const auto good = wah::toml_to_profile(v11(kVer, kCurves));
    check(good && good->curves && good->curves->position == kPos &&
              good->curves->freq == std::vector<float>({420.0f, 600.0f, 900.0f, 1400.0f, 2180.0f}) &&
              good->curves->q == std::vector<float>({4.1f, 4.2f, 4.3f, 4.4f, 4.6f}) &&
              good->curves->gain_db == std::vector<float>({17.0f, 17.1f, 17.2f, 17.3f, 17.4f}),
          "29a a v1.1 file with format_version 1.1 and a [curves] block reads: position, freq, q and gain_db as written");
    check(good && good->meta.name == "Test Wah" && good->filter.freq_heel == 450.0f && good->filter.q_max == 4.8f && good->dynamics.resonance_high == 0.5f,
          "29b the v1 blocks of a v1.1 file read as before");

    // 29c to 29e: what a capture may leave out.
    const auto ends = wah::toml_to_profile(with("q = [4.1, 4.2, 4.3, 4.4, 4.6]", "q = [4.1, 4.6]"));
    check(ends && ends->curves && ends->curves->q == std::vector<float>({4.1f, 4.6f}) && ends->curves->gain_db.size() == 5,
          "29c q with just the two ends (heel, toe) reads, the other curves unchanged");
    const auto gends = wah::toml_to_profile(with("gain_db = [17.0, 17.1, 17.2, 17.3, 17.4]", "gain_db = [17.0, 17.4]"));
    check(gends && gends->curves && gends->curves->gain_db == std::vector<float>({17.0f, 17.4f}), "29d gain_db with just the two ends reads");
    const auto bare = wah::toml_to_profile(v11(kVer, replace_once(replace_once(kCurves, "q = [4.1, 4.2, 4.3, 4.4, 4.6]\n", ""), "gain_db = [17.0, 17.1, 17.2, 17.3, 17.4]\n", "")));
    check(bare && bare->curves && bare->curves->q.empty() && bare->curves->gain_db.empty() && bare->curves->freq.size() == 5,
          "29e a block with only position and freq reads, q and gain_db empty (not measured)");

    // 29f: what is allowed, so that nobody tightens it by accident.
    accepted(with("17.0,", "-1.5,"), "29f a negative gain_db is allowed (finite is all the spec asks)");
    accepted(with("[420, 600, 900,", "[420, 900, 600,"), "29g a freq that is not monotonic is stored as measured, not refused");
    accepted(with("0.25, 0.5, 0.75", "0.1, 0.9, 0.95"), "29h positions need not be evenly spaced");
    accepted(v11(kVer, points_curves(2)), "29i two positions (heel and toe) are enough");
    accepted(v11(kVer, points_curves(wah::MAX_CURVE_POINTS)), "29j MAX_CURVE_POINTS (128) positions are accepted");
    refused(v11(kVer, points_curves(wah::MAX_CURVE_POINTS + 1)), "29k 129 positions are refused");
    accepted(v11(kVer, ""), "29l format_version 1.1 with no [curves] block is a valid file (nothing to read)");
    accepted(v11("format_version = \"1.0\"\n", ""), "29m format_version 1.0 with no block is a valid v1 file");
    accepted(v11("", ""), "29n no format_version and no block is a v1 file (as before)");

    // 29o to 29t: refused, each the good file with one change.
    refused(with("[0.0, 0.25", "[0.05, 0.25"), "29o position that does not start at 0.0 is refused");
    refused(with("0.75, 1.0]", "0.75, 0.95]"), "29p position that does not end at 1.0 is refused");
    refused(with("0.25, 0.5, 0.75", "0.5, 0.5, 0.75"), "29q position with a repeated value is refused");
    refused(with("0.25, 0.5, 0.75", "0.5, 0.25, 0.75"), "29r position that goes down is refused");
    refused(v11(kVer, replace_once(replace_once(kCurves, "position = [0.0, 0.25, 0.5, 0.75, 1.0]", "position = []"), "freq = [420, 600, 900, 1400, 2180]", "freq = []")),
            "29s empty position and freq are refused");
    refused(with("freq = ", "freqs = "), "29t a block without freq is refused");
    refused(with("1400, 2180]", "1400]"), "29u freq with one value too few is refused");
    refused(with("2180]", "2180, 2500]"), "29v freq with one value too many is refused");
    refused(with("[420, ", "[0, "), "29w freq of zero is refused");
    refused(with("[420, ", "[-420, "), "29x negative freq is refused");
    refused(with("[420, ", "[nan, "), "29y freq nan is refused");
    refused(with("[420, ", "[inf, "), "29z freq inf is refused");
    refused(with("q = [4.1, 4.2, 4.3, 4.4, 4.6]", "q = [4.1, 4.2, 4.6]"), "29aa q with 3 values for 5 positions (neither 5 nor 2) is refused");
    refused(with("q = [4.1, 4.2, 4.3, 4.4, 4.6]", "q = [4.1]"), "29ab q with one value is refused");
    refused(with("q = [4.1, 4.2, 4.3, 4.4, 4.6]", "q = [4.1, 4.2, 4.3, 4.4, 4.5, 4.6]"), "29ac q with 6 values for 5 positions is refused");
    refused(with("q = [4.1,", "q = [0,"), "29ad q of zero is refused");
    refused(with("q = [4.1,", "q = [-4.1,"), "29ae negative q is refused");
    refused(with("q = [4.1,", "q = [nan,"), "29af q nan is refused");
    refused(with("gain_db = [17.0, 17.1, 17.2, 17.3, 17.4]", "gain_db = [17.0, 17.2, 17.4]"), "29ag gain_db with 3 values for 5 positions is refused");
    refused(with("gain_db = [17.0,", "gain_db = [nan,"), "29ah gain_db nan is refused");
    refused(with("gain_db = [17.0,", "gain_db = [inf,"), "29ai gain_db inf is refused");
    // An item that is not a number must not be dropped: dropped, [4.1, 4.6, abc] would read as the two ends.
    refused(with("q = [4.1, 4.2, 4.3, 4.4, 4.6]", "q = [4.1, 4.6, abc]"), "29aj a q item that is not a number is refused, not dropped");
    refused(with("gain_db = [17.0, 17.1, 17.2, 17.3, 17.4]", "gain_db = [17.0, 17.4, x]"), "29ak a gain_db item that is not a number is refused, not dropped");
    // A q that is not an array on one line (a scalar, or an array over several lines, which this reader does not read) must not be taken as "not measured".
    refused(with("q = [4.1, 4.2, 4.3, 4.4, 4.6]", "q = 4.1"), "29az q written as a single number is refused, not read as not measured");
    refused(with("q = [4.1, 4.2, 4.3, 4.4, 4.6]", "q = [4.1, 4.2,\n4.3, 4.4, 4.6]"), "29ba q over several lines is refused, not read as not measured");
    refused(with("position = [0.0, 0.25, 0.5, 0.75, 1.0]", "position = [0.0, 0.25, 0.5, 0.75, 1.0]  # heel to toe"), "29bb a comment after an array on the same line refuses the file");
    accepted(with("freq = ", "# measured with the Cry Baby\nfreq = "), "29bc a comment on its own line inside the block is fine");

    // 29al to 29ap: the version.
    refused(v11("", kCurves), "29al a [curves] block without format_version is refused");
    refused(v11("format_version = \"1.0\"\n", kCurves), "29am a [curves] block with format_version 1.0 is refused");
    refused(v11("format_version = \"1.2\"\n", kCurves), "29an a format_version this reader does not know (1.2) is refused");
    refused(v11("format_version = \"2.0\"\n", ""), "29ao format_version 2.0 is refused even with no [curves] block");
    refused(v11("format_version = \"abc\"\n", ""), "29ap a format_version that is not a version is refused");

    // 29aq to 29av: the writer.
    wah::Profile with_curves = original;
    wah::Curves c;
    for (int i = 0; i <= 10; i++) {
        const float pos = (float)i / 10.0f;
        c.position.push_back(pos);
        c.freq.push_back(420.0f + 1760.0f * pos * pos);
        c.q.push_back(4.1f + 0.5f * pos);
        c.gain_db.push_back(17.0f + 0.4f * pos);
    }
    with_curves.curves = c;
    const std::string text = wah::profile_to_toml(with_curves);
    const auto back = wah::toml_to_profile(text);
    check(back && back->curves && back->curves->position == c.position && back->curves->freq == c.freq && back->curves->q == c.q && back->curves->gain_db == c.gain_db,
          "29aq write then read: 11 positions with freq, q and gain_db come back exactly");
    const size_t at_version = text.find(kVer), at_filter = text.find("[filter]"), at_curves = text.find("\n[curves]\n");
    check(at_version != std::string::npos && at_version < at_filter && at_curves != std::string::npos && at_curves > text.find("[dynamics]"),
          "29ar the writer puts format_version 1.1 in [meta] and [curves] after [dynamics]");
    std::string stripped = text;
    if (at_version != std::string::npos) stripped.erase(at_version, kVer.size());
    if (at_curves != std::string::npos) stripped.resize(at_curves - (at_version != std::string::npos ? kVer.size() : 0));
    check(stripped == wah::profile_to_toml(original) && back && back->filter.q_min == original.filter.q_min,
          "29as a v1.1 file is the v1 file of the same profile plus two additions (format_version, [curves]): a v1 reader loses nothing");

    wah::Profile ends_only = original;
    wah::Curves e2 = c;
    e2.q = {4.1f, 4.6f};
    e2.gain_db = {17.0f, 17.4f};
    ends_only.curves = e2;
    const auto eback = wah::toml_to_profile(wah::profile_to_toml(ends_only));
    check(eback && eback->curves && eback->curves->q == e2.q && eback->curves->gain_db == e2.gain_db && eback->curves->position.size() == 11,
          "29at write then read: q and gain_db with just the two ends come back as two values");

    wah::Profile bare_p = original;
    wah::Curves e3 = c;
    e3.q.clear();
    e3.gain_db.clear();
    bare_p.curves = e3;
    const std::string bare_text = wah::profile_to_toml(bare_p);
    const auto bback = wah::toml_to_profile(bare_text);
    check(bback && bback->curves && bback->curves->q.empty() && bback->curves->gain_db.empty() && bback->curves->freq == e3.freq &&
              bare_text.find("\nq = ") == std::string::npos && bare_text.find("\ngain_db = ") == std::string::npos,
          "29au write then read: empty q and gain_db are not written and come back empty (not measured)");

    // 29av to 29ax: v1 stays as it was.
    check(wah::profile_to_toml(original) == kGoldenV1.substr(1), "29av a v1 profile (no curves) is written as before round 29, except that a float is the shortest text that reads back exact (golden text, changed in round 32)");
    const auto v1 = wah::toml_to_profile(kGoldenV1.substr(1));
    check(v1 && !v1->curves && v1->filter.q_min == original.filter.q_min && v1->meta.created == original.meta.created, "29aw the golden v1 text reads with no curves");
    wah::Engine plain, curved;
    plain.init(original, 48000.0f);
    curved.init(with_curves, 48000.0f);
    plain.set_position(0.6f);
    curved.set_position(0.6f);
    std::vector<float> b1(512), b2(512);
    for (size_t i = 0; i < b1.size(); i++) b1[i] = b2[i] = (float)std::sin(0.05 * (double)i) * 0.5f;
    plain.process(b1.data(), (int)b1.size());
    curved.process(b2.data(), (int)b2.size());
    check(b1 == b2, "29ax the engine ignores [curves] in this round (no engine change: that is M7)");

    // ── Round 31: one check of the Curves for the reader and the writer (check_curves), the writer that refuses what the reader would refuse,
    //    and the reader that says why. 31a to 31d: check_curves on its own; 31e: the reader and check_curves agree on texts that the TEST writes
    //    (not the writer under test); 31f to 31h: the writer; 31i to 31k: the reasons.
    {
        auto good_curves = []() {
            wah::Curves k;
            k.position = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
            k.freq = {420.0f, 600.0f, 900.0f, 1400.0f, 2180.0f};
            k.q = {4.1f, 4.2f, 4.3f, 4.4f, 4.6f};
            k.gain_db = {17.0f, 17.1f, 17.2f, 17.3f, 17.4f};
            return k;
        };
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        struct Bad { const char* name; const char* field; wah::Curves c; };
        std::vector<Bad> bad;
        auto add = [&](const char* name, const char* field, auto edit) { wah::Curves k = good_curves(); edit(k); bad.push_back({name, field, k}); };
        add("one position", "position", [](wah::Curves& k) { k.position = {0.0f}; k.freq = {420.0f}; k.q.clear(); k.gain_db.clear(); });
        add("no position", "position", [](wah::Curves& k) { k.position.clear(); k.freq.clear(); k.q.clear(); k.gain_db.clear(); });
        add("129 positions", "position", [](wah::Curves& k) {
            k.position.clear(); k.freq.clear(); k.q.clear(); k.gain_db.clear();
            for (int i = 0; i < 129; i++) { k.position.push_back((float)i / 128.0f); k.freq.push_back(400.0f + (float)i); }
        });
        add("first not 0", "position", [](wah::Curves& k) { k.position[0] = 0.01f; });
        add("last not 1", "position", [](wah::Curves& k) { k.position[4] = 0.99f; });
        add("repeated position", "position", [](wah::Curves& k) { k.position[2] = k.position[1]; });
        add("falling position", "position", [](wah::Curves& k) { k.position[2] = 0.1f; });
        add("NaN position", "position", [&](wah::Curves& k) { k.position[2] = nan; });
        add("freq count", "freq", [](wah::Curves& k) { k.freq.pop_back(); });
        add("freq 0", "freq", [](wah::Curves& k) { k.freq[2] = 0.0f; });
        add("freq negative", "freq", [](wah::Curves& k) { k.freq[2] = -5.0f; });
        add("freq NaN", "freq", [&](wah::Curves& k) { k.freq[2] = nan; });
        add("freq inf", "freq", [&](wah::Curves& k) { k.freq[2] = inf; });
        add("q count 3", "q", [](wah::Curves& k) { k.q = {4.0f, 4.1f, 4.2f}; });
        add("q 0", "q", [](wah::Curves& k) { k.q[1] = 0.0f; });
        add("q negative", "q", [](wah::Curves& k) { k.q[1] = -1.0f; });
        add("q NaN", "q", [&](wah::Curves& k) { k.q[1] = nan; });
        add("q inf", "q", [&](wah::Curves& k) { k.q[1] = inf; });
        add("gain_db count 4", "gain_db", [](wah::Curves& k) { k.gain_db.pop_back(); });
        add("gain_db NaN", "gain_db", [&](wah::Curves& k) { k.gain_db[1] = nan; });
        add("gain_db inf", "gain_db", [&](wah::Curves& k) { k.gain_db[1] = inf; });

        std::vector<std::pair<std::string, wah::Curves>> allowed;
        allowed.push_back({"full", good_curves()});
        { wah::Curves k = good_curves(); k.q = {4.1f, 4.6f}; k.gain_db = {17.0f, 17.4f}; allowed.push_back({"two ends", k}); }
        { wah::Curves k = good_curves(); k.q.clear(); k.gain_db.clear(); allowed.push_back({"no q, no gain", k}); }
        { wah::Curves k = good_curves(); k.gain_db = {-3.0f, -2.0f}; allowed.push_back({"negative gain", k}); }
        { wah::Curves k = good_curves(); k.freq = {900.0f, 800.0f, 700.0f, 600.0f, 500.0f}; allowed.push_back({"falling freq", k}); }

        bool ok = true;
        for (const auto& a : allowed) ok = ok && wah::check_curves(a.second).empty();
        check(ok, "31a check_curves accepts: the full curves, the two ends, no q and no gain, a negative gain, a freq that falls");
        ok = true;
        for (const Bad& b : bad) ok = ok && !wah::check_curves(b.c).empty();
        check(ok, "31b check_curves refuses each of the 21 broken Curves (position, freq, q and gain_db rules)");
        ok = true;
        for (const Bad& b : bad) ok = ok && wah::check_curves(b.c).rfind(b.field, 0) == 0;
        check(ok, "31c the reason starts with the field that is wrong (position, freq, q or gain_db)");
        check(wah::check_curves(bad[0].c).find('\n') == std::string::npos && !wah::check_curves(bad[0].c).empty(), "31d the reason is one line");

        // 31e: the TEST turns a Curves into text, the way the spec shows it; the reader must accept it exactly when check_curves does.
        auto test_text = [&](const wah::Curves& k) {
            auto arr = [](const char* key, const std::vector<float>& v) {
                std::string s = std::string(key) + " = [";
                char b[48];
                for (size_t i = 0; i < v.size(); i++) {
                    std::snprintf(b, sizeof b, "%s%.9g", i ? ", " : "", (double)v[i]);
                    s += b;
                }
                return s + "]\n";
            };
            std::string block = "[curves]\n" + arr("position", k.position) + arr("freq", k.freq);
            if (!k.q.empty()) block += arr("q", k.q);
            if (!k.gain_db.empty()) block += arr("gain_db", k.gain_db);
            return v11(kVer, block);
        };
        ok = true;
        int compared = 0;
        for (const auto& a : allowed) { ok = ok && parses(test_text(a.second)) == wah::check_curves(a.second).empty(); compared++; }
        for (const Bad& b : bad) {
            // NaN and inf have no spelling the reader reads as a number, so those texts are refused for that reason too: still the same answer.
            ok = ok && parses(test_text(b.c)) == wah::check_curves(b.c).empty();
            compared++;
        }
        check(ok && compared == 26, "31e for the 5 allowed and the 21 broken Curves, the reader accepts the text the test wrote exactly when check_curves accepts the Curves");

        // 31f to 31h: the writer.
        wah::Profile p;
        p.meta.name = "Writer test";
        std::string tmp = "wah_test_toml_round31.wah";
        std::remove(tmp.c_str());
        ok = true;
        for (const Bad& b : bad) {
            wah::Profile q = p;
            q.curves = b.c;
            ok = ok && wah::profile_to_toml(q).empty();
        }
        check(ok, "31f profile_to_toml gives an empty string for each of the 21 broken Curves");
        ok = true;
        for (const Bad& b : bad) {
            wah::Profile q = p;
            q.curves = b.c;
            ok = ok && !wah::save_toml(q, tmp);
        }
        check(ok && !std::ifstream(tmp).is_open(), "31g save_toml returns false for each of the 21 and creates no file");
        {   // a file that is already there is not touched by a refused save (checked by reading it back, not by the writer)
            { std::ofstream keep(tmp); keep << "KEEP\n"; }
            wah::Profile q = p;
            q.curves = bad[3].c;
            const bool refused_save = !wah::save_toml(q, tmp);
            std::ifstream in(tmp);
            std::string line;
            std::getline(in, line);
            check(refused_save && line == "KEEP", "31h a refused save leaves a file that is already there as it was");
            std::remove(tmp.c_str());
        }
        ok = true;
        for (const auto& a : allowed) {
            wah::Profile q = p;
            q.curves = a.second;
            const std::string t = wah::profile_to_toml(q);
            const auto back = wah::toml_to_profile(t);
            ok = ok && !t.empty() && back && back->curves && back->curves->position == a.second.position && back->curves->freq == a.second.freq &&
                 back->curves->q == a.second.q && back->curves->gain_db == a.second.gain_db && wah::save_toml(q, tmp) && wah::load_toml(tmp).has_value();
            std::remove(tmp.c_str());
        }
        check(ok, "31h2 the 5 allowed Curves are written, saved, loaded and read back as they were");

        // 31i to 31k: the reader says why.
        auto why = [](const std::string& text) { std::string e = "stale"; const auto r = wah::toml_to_profile(text, &e); return r ? std::string("ACCEPTED:") + e : e; };
        auto has = [](const std::string& s, const char* part) { return s.find(part) != std::string::npos; };
        check(why(v11(kVer, kCurves)) == "ACCEPTED:", "31i an accepted text clears the error (it held \"stale\")");
        const std::string r_ver = why(v11("format_version = \"2.0\"\n", kCurves)), r_blk = why(v11("", kCurves));
        check(has(r_ver, "format_version") && has(r_blk, "[curves]") && has(r_blk, "format_version"),
              "31j an unknown version names format_version; a block without 1.1 names [curves] and format_version");
        const std::string r_pos = why(with("position = [0.0, 0.25, 0.5, 0.75, 1.0]", "position = [0.0, 0.5, 0.25, 0.75, 1.0]"));
        const std::string r_q = why(with("q = [4.1, 4.2, 4.3, 4.4, 4.6]", "q = [4.1, 4.2, 4.3]"));
        const std::string r_nan = why(with("freq = [420, 600, 900, 1400, 2180]", "freq = [420, abc, 900, 1400, 2180]"));
        const std::string r_one = why(with("gain_db = [17.0, 17.1, 17.2, 17.3, 17.4]", "gain_db = 17.0"));
        check(has(r_pos, "[curves]") && has(r_pos, "position") && has(r_q, "[curves]") && has(r_q, "q:") && has(r_nan, "freq") && has(r_one, "gain_db") &&
                  !has(r_pos, "ACCEPTED") && r_pos != r_q,
              "31k a broken rule names [curves] and the field (position, q); an item that is not a number or a scalar names its key (freq, gain_db)");
        const std::string r_name = why("[meta]\nauthor = \"x\"\n");
        check(has(r_name, "name"), "31l a file with no name says that the name is missing");
        std::string r_open = "stale";
        check(!wah::load_toml("this_file_does_not_exist_31.wah", &r_open) && has(r_open, "open"), "31m load_toml of a file that is not there says it cannot open it");
        {   // a file that is there but refused: load_toml passes the reason of the reader on
            const std::string bad_file = "wah_test_toml_round31_bad.wah";
            { std::ofstream f(bad_file); f << with("q = [4.1, 4.2, 4.3, 4.4, 4.6]", "q = [4.1, 0.0, 4.3, 4.4, 4.6]"); }
            std::string why_load = "stale";
            const bool refused_load = !wah::load_toml(bad_file, &why_load).has_value();
            std::remove(bad_file.c_str());
            check(refused_load && has(why_load, "[curves]") && has(why_load, "q:"), "31m2 load_toml of a file that is there but refused gives the reader's reason ([curves] q: ...)");
        }
        check(!wah::toml_to_profile("[meta]\n", nullptr).has_value() && !wah::toml_to_profile("[meta]\n").has_value(), "31n a null error pointer is allowed, and so is no argument (the callers do not change)");
    }


    // ── Round 32: the points noticed in round 29 ─────────────────────────────────────────────────────────────────
    {
        auto why32 = [](const std::string& text) { std::string e = "stale"; const auto r = wah::toml_to_profile(text, &e); return r ? std::string("ACCEPTED") : e; };
        auto has32 = [](const std::string& s, const char* part) { return s.find(part) != std::string::npos; };
        // The reader's view of one [meta] string: a profile text with the line `name = ...` replaced by the given line.
        auto name_of = [](const std::string& line) -> std::string {
            const auto r = wah::toml_to_profile("[meta]\n" + line + "\n");
            return r ? "OK:" + r->meta.name : "REFUSED";
        };
        auto desc_of = [](const std::string& lines) -> std::string {
            const auto r = wah::toml_to_profile("[meta]\nname = \"n\"\n" + lines + "\n");
            return r ? "OK:" + r->meta.description : "REFUSED";
        };

        // 32a: the Full Format block of the spec, as the spec writes it (a comment after nearly every value, and comment-only lines).
        const std::string spec_text = R"SPEC([meta]
name        = ""        # required — display name of the profile
author      = ""        # creator's name or handle
make        = ""        # pedal manufacturer (e.g. "Vox", "Dunlop")
model       = ""        # pedal model (e.g. "V846", "Cry Baby GCB-95")
description = ""        # freeform notes — vintage info, mods, etc.
created     = ""        # ISO 8601 datetime, set automatically on export

[filter]
topology        = "bandpass_2pole"  # "bandpass_2pole" or "bandpass_4pole"
freq_heel       = 420               # Hz — center frequency at heel position
freq_toe        = 2180              # Hz — center frequency at toe position
q_min           = 4.1               # lowest Q value across the sweep
q_max           = 4.6               # highest Q value across the sweep
peak_gain_db    = 17.2              # dB boost at the resonant peak
bass_shelf_db   = -2.1              # dB — low frequency shelf relative to unity
bass_shelf_freq = 200               # Hz — shelf corner frequency

[sweep]
curve = "log"           # "linear", "log", or "custom"
lut   = []              # 128-point array, required when curve = "custom"
                        # index = normalized pedal position (0.0–1.0, 128 steps)

[levels]
input_gain_db  = 0.0    # dB gain applied before the filter
output_gain_db = 0.0    # dB gain applied after the filter
mix            = 1.0    # wet/dry mix (1.0 = fully wet, 0.0 = fully dry)

[dynamics]
resonance_low  = 0.0    # resonance nonlinearity at quiet input (-20 dBu)
resonance_mid  = 0.3    # resonance nonlinearity at medium input (-10 dBu)
resonance_high = 0.6    # resonance nonlinearity at hot input (0 dBu)
                        # describes how Q and peak_gain compress as input
)SPEC";
        const auto sp = wah::toml_to_profile(replace_once(spec_text, "name        = \"\"        #", "name        = \"Spec\"      #"));
        check(sp && sp->meta.name == "Spec" && sp->filter.freq_heel == 420.0f && sp->filter.freq_toe == 2180.0f && sp->filter.q_min == 4.1f &&
                  sp->filter.q_max == 4.6f && sp->filter.peak_gain_db == 17.2f && sp->filter.bass_shelf_db == -2.1f && sp->filter.bass_shelf_freq == 200.0f &&
                  sp->sweep.curve == wah::SweepCurve::Log && sp->levels.mix == 1.0f && sp->dynamics.resonance_mid == 0.3f && sp->dynamics.resonance_high == 0.6f,
              "32a the Full Format block of the spec, comments and all, is read with every value it holds (420, not the default 450)");

        // 32b: where a comment may be, and where a # is not one.
        check(name_of("name = \"Test Wah\"  # shown in the list") == "OK:Test Wah", "32b a comment after a string value is dropped");
        check(name_of("name = \"issue #5 fixed\"") == "OK:issue #5 fixed", "32b2 a # inside a string is text, not a comment");
        check(name_of("name = \"a \\\"q\\\" # b\"  # real comment") == "OK:a \"q\" # b", "32b3 a quote with a backslash does not end the string: the # after it is still inside it");
        check(name_of("name = \"x\"#no space before the comment") == "OK:x", "32b4 a comment with no space before it");
        {
            const auto h = wah::toml_to_profile("[meta]  # the meta\nname = \"n\"\n[filter]   # the filter\nfreq_heel = 420 # Hz\n");
            check(h && h->meta.name == "n" && h->filter.freq_heel == 420.0f, "32c a comment after a section header leaves it a header");
        }
        {
            const auto c2 = wah::toml_to_profile(v11(kVer + "# a comment line\n", kCurves + "# another, between the arrays and the end\n"));
            check(c2 && c2->curves, "32d a comment on its own line in the header and in [curves] changes nothing");
        }
        {   // rule 6 of v1.1 (round 29, D66) is kept: after an array of [curves] a comment refuses the file, and the reason says so
            const std::string r = why32(with("freq = [420, 600, 900, 1400, 2180]", "freq = [420, 600, 900, 1400, 2180]  # [1, 2]"));
            check(r != "ACCEPTED" && has32(r, "[curves]") && has32(r, "freq") && has32(r, "comment"), "32d1 a comment after a [curves] array is still refused (rule 6), and the reason names the key and the comment");
        }
        refused(with("freq = [420, 600, 900, 1400, 2180]", "freq = [420, 600, 900, 1400, 2180]  # [1, 2]\nfreq = 3"), "32d2 a second freq line after a commented one is refused too");

        // 32e: a line break, a quote, a backslash, a tab, a carriage return and a # in a string go through the writer and the reader.
        {
            wah::Profile p;
            p.meta.name = "He said \"wah\" # not a comment";
            p.meta.author = "C:\\pedals\\new\\tab";
            p.meta.make = "tab\there";
            p.meta.description = "line one\nline two\r\nline three \"quoted\"\n[filter]\nfreq_heel = 999";
            p.filter.freq_heel = 450.0f;
            const std::string t = wah::profile_to_toml(p);
            const auto back = wah::toml_to_profile(t);
            check(back && back->meta.name == p.meta.name && back->meta.author == p.meta.author && back->meta.make == p.meta.make &&
                      back->meta.description == p.meta.description && back->filter.freq_heel == 450.0f,
                  "32e a name, author, make and description with a line break, quote, backslash, tab, CR and # come back as they were (and a [filter] inside the text stays text)");
            bool one_line = true;
            { std::istringstream is(t); std::string l; while (std::getline(is, l)) if (!l.empty() && l[0] != '[' && l.find(" = ") == std::string::npos) one_line = false; }
            check(one_line && t.find('\r') == std::string::npos, "32e2 the written text has a value on every line: no raw line break or CR inside a string");
            const std::string tmp = "wah_test_toml_round32.wah";
            const bool saved = wah::save_toml(p, tmp);
            const auto loaded = wah::load_toml(tmp);
            std::remove(tmp.c_str());
            check(saved && loaded && loaded->meta.description == p.meta.description, "32e3 save_toml and load_toml keep a description with line breaks");
        }

        // 32f: the escapes the reader knows, a stray backslash, and the """ form of a hand-written file.
        check(name_of("name = \"a\\nb\\tc\\\\d\\\"e\\rf\"") == "OK:a\nb\tc\\d\"e\rf", "32f the reader knows \\n \\t \\\\ \\\" \\r");
        check(name_of("name = \"a\\qb\"") == "OK:a\\qb", "32f2 an escape that is not known is kept as it was typed (backslash and letter)");
        check(desc_of("description = \"\"\"\nline one\nline two\"\"\"") == "OK:line one\nline two", "32g a \"\"\" string: the line break right after the opening is dropped, the others kept");
        check(desc_of("description = \"\"\"one line\"\"\"") == "OK:one line", "32g2 a \"\"\" string on one line");
        check(desc_of("description = \"\"\"\nfirst # not a comment\n[filter]\nfreq_heel = 999\nlast\"\"\"  # but this is\n[filter]\nfreq_heel = 420") ==
                  "OK:first # not a comment\n[filter]\nfreq_heel = 999\nlast",
              "32g3 inside a \"\"\" string a #, a [header] and a key = value are text; the comment after the closing \"\"\" is dropped; the block after it is read");
        check(has32(why32("[meta]\nname = \"n\"\ndescription = \"\"\"\nnever closed\n[filter]\nfreq_heel = 999\n"), "description"),
              "32g4 a \"\"\" string that is never closed is refused (it would swallow the rest of the file), and the reason names the key");
        check(has32(why32("[meta]\nname = \"n\"\ndescription = \"never closed\n"), "description"), "32g6 a string with no closing quote is refused and the reason names the key");

        // 32h: a section header written twice is refused, with the header in the reason (it used to drop the first block, or let the last win).
        const std::string once = v11(kVer, kCurves);
        for (const char* h : {"[meta]", "[filter]", "[sweep]", "[levels]", "[dynamics]", "[curves]"}) {
            const std::string r = why32(once + "\n" + h + "\n");
            check(r != "ACCEPTED" && has32(r, h) && (has32(r, "twice") || has32(r, "more than once")), (std::string("32h ") + h + " written twice is refused and the reason names it").c_str());
        }
        check(why32(once) == "ACCEPTED", "32h2 the same file with every header once is accepted (the check above refuses the second header, not the file)");
        check(why32("[meta]\nname = \"n\"\n[filter]\nfreq_heel = 420\n[filter]\nfreq_toe = 2000\n") != "ACCEPTED", "32h3 the first [filter]'s freq_heel is no longer lost in silence: the file is refused");

        // 32i: a float is written as the shortest text that reads back exact.
        {
            wah::Profile p;
            p.meta.name = "floats";
            p.filter.q_min = 4.2f;
            p.filter.q_max = 4.8f;
            p.dynamics.resonance_low = 0.1f;
            p.dynamics.resonance_mid = 0.3f;
            const std::string t = wah::profile_to_toml(p);
            check(t.find("q_min = 4.2\n") != std::string::npos && t.find("q_max = 4.8\n") != std::string::npos && t.find("resonance_low = 0.1\n") != std::string::npos &&
                      t.find("resonance_mid = 0.3\n") != std::string::npos,
                  "32i 4.2, 4.8, 0.1 and 0.3 are written as 4.2, 4.8, 0.1 and 0.3 (not 4.199999809)");
            const std::vector<float> awkward = {0.1f, 1.0f / 3.0f, 4.2f, 1e-7f, 123456.7f, 16777216.0f, 3.4028235e38f, 1.17549435e-38f, 0.99999994f, 2180.0f};
            wah::Profile q;
            q.meta.name = "awkward";
            wah::Curves c;
            c.position = {0.0f, 1.0f};
            c.freq = {awkward[0], awkward[1]};
            bool exact = true;
            for (size_t i = 0; i + 1 < awkward.size(); i++) {
                c.freq = {awkward[i], awkward[i + 1]};
                q.curves = c;
                const auto back = wah::toml_to_profile(wah::profile_to_toml(q));
                exact = exact && back && back->curves && back->curves->freq == c.freq;
            }
            check(exact, "32i2 ten awkward floats (a third, 1e-7, the largest, the smallest normal, one below 1) read back bit for bit");
            wah::Profile r;
            r.meta.name = "short";
            r.filter.freq_heel = 450.0f;
            r.filter.freq_toe = 2100.0f;
            r.filter.q_min = 0.25f;
            const std::string tr = wah::profile_to_toml(r);
            check(tr.find("freq_heel = 450\n") != std::string::npos && tr.find("freq_toe = 2100\n") != std::string::npos && tr.find("q_min = 0.25\n") != std::string::npos,
                  "32i3 450, 2100 and 0.25 stay as short as they were");
        }
    }


    // ── Round 33 (D70): the silent losses ────────────────────────────────────────────────────────────────────────
    {
        const std::string v1 = kGoldenV1.substr(1);
        auto g = [&v1](const std::string& from, const std::string& to) { return replace_once(v1, from, to); };
        auto why33 = [](const std::string& text) { std::string e = "stale"; const auto r = wah::toml_to_profile(text, &e); return r ? std::string("ACCEPTED") : e; };
        auto has33 = [](const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; };
        auto numbers = [](int n) { std::string t; for (int i = 0; i < n; i++) t += (i ? ", " : "") + std::to_string(i % 100) + ".5"; return t; };
        const std::string log_lut = "curve = \"log\"\nlut = []\n";

        // 33a: a number field that is not a number (or an array, or empty) is refused, and the reason names the section and the key.
        struct Field { const char* section; const char* key; const char* value; };
        const Field fields[] = {{"[filter]", "freq_heel", "450"}, {"[filter]", "freq_toe", "2100"}, {"[filter]", "q_min", "4.2"}, {"[filter]", "q_max", "4.8"},
                                {"[filter]", "peak_gain_db", "17.5"}, {"[filter]", "bass_shelf_db", "-1.5"}, {"[filter]", "bass_shelf_freq", "200"},
                                {"[levels]", "input_gain_db", "0"}, {"[levels]", "output_gain_db", "0"}, {"[levels]", "mix", "1"},
                                {"[dynamics]", "resonance_low", "0.1"}, {"[dynamics]", "resonance_mid", "0.3"}, {"[dynamics]", "resonance_high", "0.5"}};
        bool all_abc = true, all_array = true, all_empty = true, all_nan = true, all_inf = true, all_big = true, all_good = true;
        std::string first_bad;
        for (const Field& f : fields) {
            const std::string line = std::string(f.key) + " = " + f.value + "\n";
            auto reason = [&](const std::string& value) { return why33(g(line, std::string(f.key) + " = " + value + "\n")); };
            auto refused_named = [&](const std::string& value) { const std::string r = reason(value); const bool ok = r != "ACCEPTED" && has33(r, f.section) && has33(r, f.key); if (!ok && first_bad.empty()) first_bad = std::string(f.key) + " = " + value + " -> " + r; return ok; };
            all_abc = refused_named("abc") && all_abc;
            all_array = refused_named("[1.0]") && all_array;
            all_empty = refused_named("") && all_empty;
            all_nan = refused_named("nan") && all_nan;
            all_inf = refused_named("inf") && refused_named("-inf") && all_inf;
            all_big = refused_named("1e39") && all_big;
            all_good = (reason("1.5") == "ACCEPTED") && all_good;
        }
        check(all_abc, "33a each of the 13 number fields of [filter], [levels] and [dynamics] set to abc refuses the file, and the reason names the section and the key");
        check(all_array, "33a2 each of the 13 given an array instead of a number is refused, naming the key");
        check(all_empty, "33a3 each of the 13 with an empty value is refused, naming the key");
        check(all_nan, "33b each of the 13 set to nan is refused, naming the key");
        check(all_inf, "33b2 each of the 13 set to inf or -inf is refused, naming the key");
        check(all_big, "33b3 each of the 13 set to 1e39 (does not fit a float) is refused, naming the key");
        check(all_good, "33b4 each of the 13 set to 1.5 is accepted (the checks above refuse the field, not the file)");
        if (!first_bad.empty()) std::cout << "     first miss: " << first_bad << "\n";
        check(why33(g("freq_heel = 450\n", "freq_heel = 3.4028235e38\n")) == "ACCEPTED" && why33(g("freq_heel = 450\n", "freq_heel = -0.0\n")) == "ACCEPTED",
              "33b5 the largest float and minus zero are still accepted");
        refused(with("freq = [420, 600, 900, 1400, 2180]", "freq = [420, inf, 900, 1400, 2180]"), "33b6 an inf in a [curves] array is refused");
        refused(with("gain_db = [17.0, 17.1, 17.2, 17.3, 17.4]", "gain_db = [17.0, 17.1, nan, 17.3, 17.4]"), "33b7 a nan in a [curves] array is refused");

        // 33c, 33d: an unknown topology or curve is refused, no longer read as the 2-pole or ignored.
        {
            const std::string t3 = why33(g("topology = \"bandpass_2pole\"", "topology = \"bandpass_3pole\""));
            const std::string t0 = why33(g("topology = \"bandpass_2pole\"", "topology = 2"));
            const std::string t4 = why33(g("topology = \"bandpass_2pole\"", "topology = [\"bandpass_4pole\"]"));
            check(t3 != "ACCEPTED" && has33(t3, "[filter]") && has33(t3, "topology") && has33(t3, "bandpass_3pole"), "33c an unknown topology is refused; the reason names topology and what was found");
            check(t0 != "ACCEPTED" && has33(t0, "topology") && t4 != "ACCEPTED" && has33(t4, "topology") && has33(t4, "an array"), "33c2 a topology that is a number or an array is refused, and the reason says it is an array");
            const auto four = wah::toml_to_profile(g("topology = \"bandpass_2pole\"", "topology = \"bandpass_4pole\""));
            const auto two = wah::toml_to_profile(v1);
            check(four && four->filter.topology == wah::Topology::Bandpass4Pole && two && two->filter.topology == wah::Topology::Bandpass2Pole, "33c3 bandpass_4pole and bandpass_2pole are still read");
            const std::string c0 = why33(g("curve = \"log\"", "curve = \"exp\""));
            const std::string c1 = why33(g("curve = \"log\"", "curve = 1"));
            check(c0 != "ACCEPTED" && has33(c0, "[sweep]") && has33(c0, "curve") && has33(c0, "exp") && c1 != "ACCEPTED" && has33(c1, "curve"), "33d an unknown curve (or a number) is refused; the reason names curve");
            const auto lin = wah::toml_to_profile(g("curve = \"log\"", "curve = \"linear\""));
            check(lin && lin->sweep.curve == wah::SweepCurve::Linear, "33d2 curve = \"linear\" is still read");
        }

        // 33e: a custom curve has exactly 128 numbers in lut; any other curve ignores lut.
        {
            auto custom = [&](const std::string& lut_line) { return g(log_lut, "curve = \"custom\"\n" + lut_line + "\n"); };
            const auto ok128 = wah::toml_to_profile(custom("lut = [" + numbers(128) + "]"));
            check(ok128 && ok128->sweep.curve == wah::SweepCurve::Custom && ok128->sweep.lut[0] == 0.5f && ok128->sweep.lut[127] == 27.5f, "33e a custom curve with 128 numbers is read (first and last checked)");
            for (int n : {0, 1, 127, 129, 256}) {
                const std::string r = why33(custom("lut = [" + numbers(n) + "]"));
                check(r != "ACCEPTED" && has33(r, "[sweep]") && has33(r, "lut") && has33(r, "128"), ("33e2 a custom curve with " + std::to_string(n) + " numbers is refused; the reason names lut and 128").c_str());
            }
            std::string bad = numbers(128);
            bad.replace(bad.find("1.5"), 3, "abc");
            const std::string rb = why33(custom("lut = [" + bad + "]"));
            check(rb != "ACCEPTED" && has33(rb, "lut"), "33e3 a custom lut of 128 items, one of them not a number, is refused (it used to be dropped)");
            std::string bad129 = numbers(129);
            bad129.replace(bad129.find("1.5"), 3, "abc");
            const std::string rb2 = why33(custom("lut = [" + bad129 + "]"));
            check(rb2 != "ACCEPTED" && has33(rb2, "lut"), "33e3b a custom lut of 129 items, one not a number (128 numbers are left), is refused: the bad item counts, not only the length");
            const std::string r0 = why33(g(log_lut, "curve = \"custom\"\n")), r1 = why33(custom("lut = 5"));
            check(r0 != "ACCEPTED" && has33(r0, "lut") && r1 != "ACCEPTED" && has33(r1, "lut"), "33e4 a custom curve with no lut, or with a lut that is one value, is refused");
            check(why33(g(log_lut, "curve = \"log\"\nlut = [1, 2, 3]\n")) == "ACCEPTED" && why33(g(log_lut, "curve = \"linear\"\nlut = [abc, 2]\n")) == "ACCEPTED",
                  "33e5 with curve log or linear the lut is ignored, whatever it holds");
            check(why33(custom("lut = [" + numbers(128) + "]  # the lut")) == "ACCEPTED", "33e6 a comment after a lut (not a [curves] array) is still fine");
            wah::Profile p;
            p.meta.name = "custom";
            p.sweep.curve = wah::SweepCurve::Custom;
            for (int i = 0; i < wah::LUT_SIZE; i++) p.sweep.lut[i] = (float)i / (wah::LUT_SIZE - 1);
            const auto back = wah::toml_to_profile(wah::profile_to_toml(p));
            check(back && back->sweep.curve == wah::SweepCurve::Custom && back->sweep.lut == p.sweep.lut, "33e7 a custom profile is written and read back with its 128 values, bit for bit");
            wah::Profile z;
            z.meta.name = "zeros";
            z.sweep.curve = wah::SweepCurve::Custom;
            check(wah::toml_to_profile(wah::profile_to_toml(z)).has_value(), "33e8 a custom profile whose lut was never filled is still written in a way the reader accepts");
        }

        // 33f: a key written twice in a section is refused, the reason names the section and the key; the same key in two sections is fine.
        {
            const std::string r_f = why33(g("freq_heel = 450\n", "freq_heel = 450\nfreq_heel = 451\n"));
            const std::string r_same = why33(g("freq_toe = 2100\n", "freq_toe = 2100\nfreq_toe = 2100\n"));
            const std::string r_n = why33(g("name = \"Test Wah\"\n", "name = \"Test Wah\"\nname = \"Other\"\n"));
            const std::string r_l = why33(g("lut = []\n", "lut = []\nlut = []\n"));
            const std::string r_c = why33(with("freq = [420, 600, 900, 1400, 2180]\n", "freq = [420, 600, 900, 1400, 2180]\nfreq = [420, 600, 900, 1400, 2180]\n"));
            check(r_f != "ACCEPTED" && has33(r_f, "[filter]") && has33(r_f, "freq_heel") && has33(r_f, "more than once"), "33f a key written twice (two values) is refused; the reason names the section and the key");
            check(r_same != "ACCEPTED" && has33(r_same, "freq_toe"), "33f2 a key written twice with the same value is refused too");
            check(r_n != "ACCEPTED" && has33(r_n, "[meta]") && has33(r_n, "name") && r_l != "ACCEPTED" && has33(r_l, "lut") && r_c != "ACCEPTED" && has33(r_c, "[curves]") && has33(r_c, "freq"),
                  "33f3 a string, an array and a [curves] array written twice are refused");
            check(why33(g("freq_heel = 450\n", "freq_heel = 450\nname = \"only here\"\n")) == "ACCEPTED", "33f4 the same key in two different sections is fine");
            check(why33(g("freq_heel = 450\n", "freq_heel = 450\nunknown_key = 1\nunknown_key2 = 2\n")) == "ACCEPTED", "33f5 two different keys, known or not, are fine");
        }

        // 33g: a denormal is a number (strtof, not stof); a number that overflows is not; one that underflows to zero is read as zero.
        {
            const auto tiny = wah::toml_to_profile(g("q_min = 4.2\n", "q_min = 7e-45\n"));
            check(tiny && tiny->filter.q_min == 7e-45f && tiny->filter.q_min > 0.0f, "33g q_min = 7e-45 is read as that denormal (it used to be lost)");
            const auto none = wah::toml_to_profile(g("q_min = 4.2\n", "q_min = 1e-50\n"));
            check(none && none->filter.q_min == 0.0f, "33g2 a number too small for a float (1e-50) is read as 0: underflow is accepted, overflow is not");
            wah::Profile q;
            q.meta.name = "denormal";
            wah::Curves c;
            c.position = {0.0f, 1.0f};
            c.freq = {7e-45f, 1.17549435e-38f};
            q.curves = c;
            const auto back = wah::toml_to_profile(wah::profile_to_toml(q));
            check(back && back->curves && back->curves->freq == c.freq, "33g3 a denormal and the smallest normal in [curves] are written and read back bit for bit");
        }
    }


    // ── Round 34 (D71): the blocks [shape_heel] and [shape_toe] ──────────────────────────────────────────────
    {
        auto why34 = [](const std::string& text) { std::string e = "stale"; const auto r = wah::toml_to_profile(text, &e); return r ? std::string("ACCEPTED") : e; };
        auto has34 = [](const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; };
        const std::string head = "[meta]\nname = \"Shape\"\nformat_version = \"1.1\"\n";
        const std::string heel = "[shape_heel]\nfreq = [20, 40, 80, 160]\ndb = [-12.0, -6.0, 6.0, 4.0]\n";
        const std::string toe = "[shape_toe]\nfreq = [20, 40, 80, 160, 320]\ndb = [-14.0, -10.0, 3.0, 12.0, 5.0]\n";
        const std::string good = head + "\n" + heel + "\n" + toe;
        // The good file with ONE change in the heel block (or the toe block).
        auto in_heel = [&](const std::string& from, const std::string& to) { return head + "\n" + replace_once(heel, from, to) + "\n" + toe; };
        auto in_toe = [&](const std::string& from, const std::string& to) { return head + "\n" + heel + "\n" + replace_once(toe, from, to); };
        auto nums = [](int n, float first, float step) { std::string t; for (int i = 0; i < n; i++) { char b[32]; snprintf(b, sizeof(b), "%.4f", first + step * (float)i); t += (i ? ", " : "") + std::string(b); } return t; };

        // 34a: the good file, with different counts in the two blocks, and with [curves] beside it, and the example of the spec.
        const auto g = wah::toml_to_profile(good);
        check(g && g->shape && !g->curves && g->shape->heel.freq == std::vector<float>({20, 40, 80, 160}) && g->shape->heel.db == std::vector<float>({-12.0f, -6.0f, 6.0f, 4.0f}) &&
                  g->shape->toe.freq == std::vector<float>({20, 40, 80, 160, 320}) && g->shape->toe.db == std::vector<float>({-14.0f, -10.0f, 3.0f, 12.0f, 5.0f}),
              "34a a file with [shape_heel] and [shape_toe] (4 and 5 cells, negative dB) is read; there is no [curves] and none is needed");
        const auto both = wah::toml_to_profile(v11(kVer, kCurves) + "\n" + heel + "\n" + toe);
        check(both && both->shape && both->curves && both->curves->position.size() == 5, "34a2 [curves] and the two shape blocks in one file are all read");
        const std::string spec_example = "[meta]\nname           = \"Measured pedal\"\nformat_version = \"1.1\"\n\n[shape_heel]\nfreq = [20, 28, 40, 57, 80, 113, 160]\ndb   = [-12.0, -9.5, -6.0, 0.5, 6.0, 9.5, 4.0]\n\n[shape_toe]\nfreq = [20, 28, 40, 57, 80, 113, 160]\ndb   = [-14.0, -12.5, -10.0, -4.5, 3.0, 8.5, 12.0]\n";
        const auto se = wah::toml_to_profile(spec_example);
        check(se && se->shape && se->shape->heel.freq.size() == 7 && se->shape->toe.db.back() == 12.0f, "34a3 the example of the spec is read");

        // 34b: version (rule 7): the blocks need format_version 1.1.
        {
            const std::string nov = "[meta]\nname = \"Shape\"\n\n" + heel + "\n" + toe;
            const std::string v10 = "[meta]\nname = \"Shape\"\nformat_version = \"1.0\"\n\n" + heel + "\n" + toe;
            const std::string r0 = why34(nov), r1 = why34(v10);
            check(r0 != "ACCEPTED" && has34(r0, "[shape_heel]") && has34(r0, "format_version") && r1 != "ACCEPTED" && has34(r1, "format_version"),
                  "34b the blocks in a file with no format_version, or with 1.0, are refused, and the reason says format_version");
            const std::string only_toe = "[meta]\nname = \"Shape\"\n\n" + toe;
            check(has34(why34(only_toe), "format_version"), "34b2 the version is checked before the pair: a lone [shape_toe] in a v1 file says format_version");
        }

        // 34c: both or none (rule 8).
        {
            const std::string h = why34(head + "\n" + heel), t = why34(head + "\n" + toe);
            check(h != "ACCEPTED" && has34(h, "[shape_toe]") && has34(h, "come together") && t != "ACCEPTED" && has34(t, "[shape_heel]") && has34(t, "come together"),
                  "34c one block without the other is refused, and the reason names the one that is missing");
            check(why34(head) == "ACCEPTED" && wah::toml_to_profile(head) && !wah::toml_to_profile(head)->shape, "34c2 no block at all is a profile with no shape");
        }

        // 34d: the rules of freq and db (rule 9), each from the good file with ONE change; the reason names the block and the key.
        struct Case { const char* what; std::string text; const char* block; const char* key; const char* part; };
        const std::vector<Case> cases = {
            {"one value", in_heel("freq = [20, 40, 80, 160]\ndb = [-12.0, -6.0, 6.0, 4.0]", "freq = [20]\ndb = [1]"), "[shape_heel]", "freq", "2 to 128"},
            {"129 values", in_heel("freq = [20, 40, 80, 160]\ndb = [-12.0, -6.0, 6.0, 4.0]", "freq = [" + nums(129, 20, 1) + "]\ndb = [" + nums(129, 0, 0.1f) + "]"), "[shape_heel]", "freq", "2 to 128"},
            {"no freq", in_heel("freq = [20, 40, 80, 160]\n", ""), "[shape_heel]", "freq", "2 to 128"},
            {"no db", in_heel("db = [-12.0, -6.0, 6.0, 4.0]\n", ""), "[shape_heel]", "db", "one value per freq"},
            {"freq 0", in_heel("freq = [20,", "freq = [0,"), "[shape_heel]", "freq", "above 0"},
            {"freq negative", in_heel("freq = [20,", "freq = [-5,"), "[shape_heel]", "freq", "above 0"},
            {"freq nan", in_heel("freq = [20,", "freq = [nan,"), "[shape_heel]", "freq", "not an array of numbers"},
            {"freq equal", in_heel("freq = [20, 40, 80, 160]", "freq = [20, 20, 80, 160]"), "[shape_heel]", "freq", "strictly increasing"},
            {"freq falling", in_heel("freq = [20, 40, 80, 160]", "freq = [20, 40, 80, 70]"), "[shape_heel]", "freq", "strictly increasing"},
            {"db one fewer", in_heel("db = [-12.0, -6.0, 6.0, 4.0]", "db = [-12.0, -6.0, 6.0]"), "[shape_heel]", "db", "one value per freq"},
            {"db one more", in_heel("db = [-12.0, -6.0, 6.0, 4.0]", "db = [-12.0, -6.0, 6.0, 4.0, 1.0]"), "[shape_heel]", "db", "one value per freq"},
            {"db inf", in_heel("db = [-12.0,", "db = [inf,"), "[shape_heel]", "db", "not an array of numbers"},
            {"item not a number", in_heel("freq = [20, 40,", "freq = [20, abc,"), "[shape_heel]", "freq", "not an array of numbers"},
            {"db a single value", in_heel("db = [-12.0, -6.0, 6.0, 4.0]", "db = 5"), "[shape_heel]", "db", "not an array of numbers"},
            {"comment after the array", in_heel("db = [-12.0, -6.0, 6.0, 4.0]", "db = [-12.0, -6.0, 6.0, 4.0]  # dB"), "[shape_heel]", "db", "comment"},
            {"toe: freq falling", in_toe("freq = [20, 40, 80, 160, 320]", "freq = [20, 40, 80, 160, 100]"), "[shape_toe]", "freq", "strictly increasing"},
            {"toe: db one fewer", in_toe("db = [-14.0, -10.0, 3.0, 12.0, 5.0]", "db = [-14.0, -10.0, 3.0, 12.0]"), "[shape_toe]", "db", "one value per freq"},
        };
        bool all_named = true;
        std::string miss;
        for (const Case& c : cases) {
            const std::string r = why34(c.text);
            const bool ok = r != "ACCEPTED" && has34(r, c.block) && has34(r, c.key) && has34(r, c.part);
            if (!ok) { all_named = false; if (miss.empty()) miss = std::string(c.what) + " -> " + r; }
        }
        check(all_named, "34d each of 16 broken rules (count, freq above 0 and rising, db one per freq, items, comment) refuses the file and the reason names the block, the key and the rule");
        if (!miss.empty()) std::cout << "     first miss: " << miss << "\n";
        check(why34(in_heel("freq = [20, 40, 80, 160]\ndb = [-12.0, -6.0, 6.0, 4.0]", "freq = [20, 40]\ndb = [1, 2]")) == "ACCEPTED" &&
                  why34(in_heel("freq = [20, 40, 80, 160]\ndb = [-12.0, -6.0, 6.0, 4.0]", "freq = [" + nums(128, 20, 1) + "]\ndb = [" + nums(128, -50, 0.5f) + "]")) == "ACCEPTED",
              "34d2 2 and 128 cells are accepted (the checks above refuse the rule, not the file)");
        check(why34(in_heel("db = [-12.0, -6.0, 6.0, 4.0]", "# dB per cell\ndb = [-12.0, -6.0, 6.0, 4.0]")) == "ACCEPTED", "34d3 a comment on its own line in a block is fine");

        // 34e: check_shape and the reader agree: the same texts, written by the TEST, accepted or refused together, with the same reason.
        {
            auto text_of = [&](const wah::Shape& sh) {
                auto arr = [](const std::vector<float>& v) { std::ostringstream o; o.precision(9); o << "["; for (size_t i = 0; i < v.size(); i++) o << (i ? ", " : "") << v[i]; o << "]"; return o.str(); };
                return head + "\n[shape_heel]\nfreq = " + arr(sh.heel.freq) + "\ndb = " + arr(sh.heel.db) + "\n\n[shape_toe]\nfreq = " + arr(sh.toe.freq) + "\ndb = " + arr(sh.toe.db) + "\n";
            };
            wah::Shape ok; ok.heel = {{20, 40, 80}, {1, 2, 3}}; ok.toe = {{20, 40}, {-1, -2}};
            std::vector<wah::Shape> shapes = {ok};
            auto mut = [&](auto change) { wah::Shape s = ok; change(s); shapes.push_back(s); };
            mut([](wah::Shape& s) { s.heel.freq = {20}; s.heel.db = {1}; });
            mut([](wah::Shape& s) { s.toe.freq.clear(); s.toe.db.clear(); });
            mut([](wah::Shape& s) { s.heel.freq = std::vector<float>(129, 1.0f); for (size_t i = 0; i < 129; i++) s.heel.freq[i] = 1.0f + (float)i; s.heel.db = std::vector<float>(129, 0.0f); });
            mut([](wah::Shape& s) { s.heel.freq[0] = 0.0f; });
            mut([](wah::Shape& s) { s.toe.freq[0] = -1.0f; });
            mut([](wah::Shape& s) { s.heel.freq[1] = 20.0f; });
            mut([](wah::Shape& s) { s.toe.freq = {40, 20}; });
            mut([](wah::Shape& s) { s.heel.db.pop_back(); });
            mut([](wah::Shape& s) { s.toe.db.push_back(0.0f); });
            mut([](wah::Shape& s) { s.heel.db[0] = std::numeric_limits<float>::infinity(); });
            mut([](wah::Shape& s) { s.toe.db[1] = std::numeric_limits<float>::quiet_NaN(); });
            mut([](wah::Shape& s) { s.heel.freq[2] = std::numeric_limits<float>::infinity(); });
            bool agree = true, same_reason = true;
            size_t reader_refusals = 0;
            std::string disagree;
            for (size_t i = 0; i < shapes.size(); i++) {
                const std::string c = wah::check_shape(shapes[i]);
                const std::string r = why34(text_of(shapes[i]));
                const bool reader_ok = r == "ACCEPTED";
                if (!reader_ok) reader_refusals++;
                if (reader_ok != c.empty()) { agree = false; if (disagree.empty()) disagree = "shape " + std::to_string(i) + ": check says \"" + c + "\", reader says \"" + r + "\""; }
                else if (!reader_ok && r.find("not an array of numbers") == std::string::npos && r != c) same_reason = false;
            }
            check(agree && reader_refusals == shapes.size() - 1, "34e check_shape and the reader accept and refuse the same 13 shapes: the good one is accepted and the 12 broken ones are refused by both ('inf' and 'nan' too)");
            if (!disagree.empty()) std::cout << "     first miss: " << disagree << "\n";
            check(same_reason, "34e2 when the reader refuses a shape that check_shape refuses, it gives check_shape's reason word for word");
            check(wah::check_shape(shapes[0]).empty() && has34(wah::check_shape(shapes[2]), "[shape_toe] freq") && has34(wah::check_shape(shapes[4]), "[shape_heel] freq") && has34(wah::check_shape(shapes[8]), "[shape_heel] db"),
                  "34e3 check_shape's reason starts with the block and the key");
        }

        // 34f: the writer: a valid shape is written (after [curves], heel then toe, with format_version 1.1) and comes back as it was; an invalid one is not written.
        {
            wah::Profile p;
            p.meta.name = "writer";
            wah::Shape sh; sh.heel = {{20, 40, 80}, {0.1f, -2.5f, 3.0f}}; sh.toe = {{20.5f, 41.0f}, {1.0f / 3.0f, -7.0f}};
            p.shape = sh;
            const std::string t = wah::profile_to_toml(p);
            const auto back = wah::toml_to_profile(t);
            check(!t.empty() && has34(t, "format_version = \"1.1\"") && !has34(t, "[curves]") && t.find("[shape_heel]") < t.find("[shape_toe]") && has34(t, "db = [0.1, -2.5, 3]") &&
                      back && back->shape && back->shape->heel.freq == sh.heel.freq && back->shape->heel.db == sh.heel.db && back->shape->toe.freq == sh.toe.freq && back->shape->toe.db == sh.toe.db,
                  "34f a profile with a shape and no curves is written with format_version 1.1, heel before toe, the shortest text for each number, and read back bit for bit");
            wah::Profile pc = p;
            wah::Curves c; c.position = {0.0f, 1.0f}; c.freq = {420.0f, 2180.0f};
            pc.curves = c;
            const std::string tc = wah::profile_to_toml(pc);
            const auto bc = wah::toml_to_profile(tc);
            check(bc && bc->curves && bc->shape && tc.find("[curves]") < tc.find("[shape_heel]") && tc.find("[shape_heel]") < tc.find("[shape_toe]"), "34f2 with [curves] too: [curves], then [shape_heel], then [shape_toe]");
            wah::Profile bad = p;
            bad.shape->toe.freq = {40.0f, 20.0f};
            check(wah::profile_to_toml(bad).empty(), "34f3 a shape that breaks a rule gives an empty string: nothing is written that would come back refused");
            const std::string tmp = "wah_test_toml_round34.wah";
            { std::ofstream f(tmp); f << "keep me"; }
            const bool refused_save = !wah::save_toml(bad, tmp);
            std::string kept; { std::ifstream f(tmp); std::getline(f, kept); }
            const bool saved = wah::save_toml(p, tmp);
            const auto loaded = wah::load_toml(tmp);
            std::remove(tmp.c_str());
            check(refused_save && kept == "keep me" && saved && loaded && loaded->shape && loaded->shape->heel.db == sh.heel.db, "34f4 save_toml refuses a bad shape and leaves the file alone; a good one is saved and loaded");
            wah::Profile lone = p;
            lone.shape->heel = {};
            check(wah::profile_to_toml(lone).empty() && !wah::check_shape(*lone.shape).empty(), "34f5 an empty table at one end is not written (both or none)");
        }

        // 34g: the numbers: ten awkward floats go through the writer and the reader in the dB column, and four in the freq column (strictly rising).
        {
            const std::vector<float> awkward = {0.1f, 1.0f / 3.0f, -4.2f, 1e-7f, 123456.7f, -16777216.0f, 3.4028235e38f, 1.17549435e-38f, 0.99999994f, 2180.0f};
            wah::Profile p;
            p.meta.name = "awkward";
            wah::Shape sh;
            sh.heel.freq = {1e-6f, 0.1f, 2180.0f, 123456.7f};
            sh.heel.db = {awkward[0], awkward[1], awkward[2], awkward[3]};
            sh.toe.freq = {20.0f, 21.0f, 22.0f, 3.4028235e38f};
            sh.toe.db = {awkward[4], awkward[5], awkward[6], awkward[7]};
            p.shape = sh;
            const auto back = wah::toml_to_profile(wah::profile_to_toml(p));
            check(back && back->shape && back->shape->heel.freq == sh.heel.freq && back->shape->heel.db == sh.heel.db && back->shape->toe.freq == sh.toe.freq && back->shape->toe.db == sh.toe.db,
                  "34g awkward floats in freq and db (1e-6, a third, the largest float, the smallest normal) come back bit for bit");
        }

        // 34h: the engine ignores the shape (M7).
        {
            wah::Profile plain, shaped;
            plain.meta.name = shaped.meta.name = "engine";
            wah::Shape sh; sh.heel = {{20, 40, 80}, {5.0f, 9.0f, 20.0f}}; sh.toe = {{20, 40}, {-30.0f, 30.0f}};
            shaped.shape = sh;
            wah::Engine e1, e2;
            e1.init(plain, 48000.0f);
            e2.init(shaped, 48000.0f);
            e1.set_position(0.4f);
            e2.set_position(0.4f);
            std::vector<float> b1(512), b2(512);
            for (size_t i = 0; i < b1.size(); i++) b1[i] = b2[i] = (float)std::sin(0.05 * (double)i) * 0.5f;
            e1.process(b1.data(), (int)b1.size());
            e2.process(b2.data(), (int)b2.size());
            check(b1 == b2, "34h the engine ignores the shape blocks in this round (no engine change: that is M7)");
        }

        // 34i: v1 stays as it was and the keys of a v1 file are not read as a shape.
        check(wah::profile_to_toml(original) == kGoldenV1.substr(1) && wah::toml_to_profile(kGoldenV1.substr(1)) && !wah::toml_to_profile(kGoldenV1.substr(1))->shape,
              "34i a v1 profile is written as before and reads with no shape");
    }

    if (failures) {
        std::cout << "\n✗ Test FAILED: " << failures << " check(s)\n";
        return 1;
    }
    std::cout << "\n✓ Test PASSED: round trip of v1 and v1.1 profiles successful\n";
    return 0;
}
