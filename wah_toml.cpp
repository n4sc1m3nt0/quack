/**
 * wah_toml.cpp
 * TOML parsing and serialization implementation.
 */

#include "wah_toml.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <map>
#include <set>

namespace wah {

// ─────────────────────────────────────────────
// Helper utilities
// ─────────────────────────────────────────────

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

static bool starts_with(const std::string& str, const std::string& prefix) {
    return str.find(prefix) == 0;
}

// A float as the shortest text that reads back to the same float: 4.2f is written 4.2, not 4.199999809. It never has fewer digits
// than the part before the point, so that 450 is written 450 and not 4.5e+02 (1 to 9 significant digits are enough for any float).
static std::string to_string(float value) {
    char buf[40];
    if (!std::isfinite(value)) {
        snprintf(buf, sizeof(buf), "%g", (double)value);
        return std::string(buf);
    }
    int digits = snprintf(nullptr, 0, "%.0f", std::fabs((double)value));
    if (digits > 9) digits = 9;
    for (; digits <= 9; digits++) {
        snprintf(buf, sizeof(buf), "%.*g", digits, (double)value);
        if (std::strtof(buf, nullptr) == value) break;
    }
    return std::string(buf);
}

// A finite number that fits a float. nan, inf and a number too big for a float are not accepted; a number too small (a denormal, or one
// that rounds to zero) is. (std::stof throws on those, which lost a denormal.)
static std::optional<float> parse_float(const std::string& str) {
    if (str.empty()) return {};
    char* end = nullptr;
    const float value = std::strtof(str.c_str(), &end);
    if (end != str.c_str() + str.size() || !std::isfinite(value)) return {};
    return value;
}

[[maybe_unused]] static std::optional<std::string> parse_string(const std::string& str) {
    std::string trimmed = trim(str);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return trimmed.substr(1, trimmed.size() - 2);
    }
    return {};
}

// ─────────────────────────────────────────────
// TOML Parsing
// ─────────────────────────────────────────────

struct TomlTable {
    std::map<std::string, std::string> values;
    std::map<std::string, std::vector<float>> arrays;
    std::set<std::string> bad_arrays;  // keys of an array with an item that is not a number (the item is dropped from `arrays`)
    std::set<std::string> commented_arrays;  // keys of an array with a comment after it on the same line
    std::set<std::string> keys;  // every key written in the section (a key written twice refuses the file)
};

// The text of a header, a number or an array without its comment (everything from the first #). A string is not cut here: its end is
// found by closing_quote(), so a # inside a string is text.
static std::string strip_comment(const std::string& line) {
    const size_t hash = line.find('#');
    return hash == std::string::npos ? line : line.substr(0, hash);
}

static void append_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

// The text between the quotes of a string, with the escapes of a TOML basic string read: \\n \\t \\r \\b \\f \\\\ \\" and \\uXXXX.
// An escape that is not one of these is kept as it was typed (the backslash and the letter).
static std::string unescape(const std::string& raw) {
    std::string out;
    for (size_t i = 0; i < raw.size(); i++) {
        const char c = raw[i];
        if (c != '\\' || i + 1 >= raw.size()) {
            out += c;
            continue;
        }
        const char n = raw[++i];
        switch (n) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '\\': case '"': out += n; break;
            case 'u': {
                bool hex = i + 4 < raw.size();  // four more characters after the u
                for (size_t k = 1; hex && k <= 4; k++) hex = std::isxdigit((unsigned char)raw[i + k]) != 0;
                if (hex) {
                    append_utf8(out, (unsigned)std::strtoul(raw.substr(i + 1, 4).c_str(), nullptr, 16));
                    i += 4;
                } else {
                    out += "\\u";
                }
                break;
            }
            default: out += '\\'; out += n; break;
        }
    }
    return out;
}

// A string as the writer puts it: in quotes, on one line, with the characters that would break that written as escapes.
static std::string quote_string(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (c < 0x20 || c == 0x7F) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04X", (unsigned)c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out + "\"";
}

// Where a "string" that starts at s[0] ends: the index of its closing quote, or npos if it has none on this line.
static size_t closing_quote(const std::string& s) {
    for (size_t i = 1; i < s.size(); i++) {
        if (s[i] == '\\') i++;
        else if (s[i] == '"') return i;
    }
    return std::string::npos;
}

// error is empty if the text could be split into tables; otherwise one line that says what is wrong (a header written twice, a
// string that is never closed, text after a string).
static std::map<std::string, TomlTable> parse_toml(const std::string& text, std::string& error) {
    std::map<std::string, TomlTable> tables;
    std::vector<std::string> lines;
    {
        std::istringstream iss(text);
        std::string l;
        while (std::getline(iss, l)) lines.push_back(l);
    }
    std::string current_section;
    error.clear();

    for (size_t n = 0; n < lines.size(); n++) {
        const std::string line = trim(lines[n]);

        // Skip empty lines and comments
        if (line.empty() || starts_with(line, "#")) {
            continue;
        }

        // Section header (a comment may follow it)
        if (starts_with(line, "[")) {
            const std::string header = trim(strip_comment(line));
            if (header.back() != ']') continue;  // skip malformed lines
            current_section = trim(header.substr(1, header.length() - 2));
            if (tables.count(current_section)) {
                error = "[" + current_section + "] is written more than once";
                return tables;
            }
            tables[current_section] = TomlTable{};
            continue;
        }

        // Key-value pair
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;  // skip malformed lines
        }
        const std::string key = trim(line.substr(0, eq_pos));
        const std::string rest = trim(line.substr(eq_pos + 1));
        std::string after;  // what follows a string on its line: nothing, or a comment
        // A key written twice in a section refuses the file (before, the last one won in silence).
        auto claim = [&]() {
            if (current_section.empty() || tables[current_section].keys.insert(key).second) return true;
            error = "[" + current_section + "] " + key + " is written more than once";
            return false;
        };

        if (starts_with(rest, "\"\"\"")) {
            // A string over several lines: """ ... """. The line break right after the opening is dropped; a #, a [header] or a
            // key = value inside it is text.
            std::string raw;
            std::string seg = rest.substr(3);
            bool first = true, closed = false;
            for (;;) {
                size_t i = 0;
                for (; i < seg.size(); i++) {
                    if (seg[i] == '\\') { i++; continue; }
                    if (seg.compare(i, 3, "\"\"\"") == 0) { closed = true; break; }
                }
                if (closed) {
                    raw += seg.substr(0, i);
                    after = trim(seg.substr(i + 3));
                    break;
                }
                if (!seg.empty() && seg.back() == '\r') seg.pop_back();
                if (!(first && seg.empty())) raw += seg + "\n";
                first = false;
                if (++n >= lines.size()) {
                    error = key + ": the \"\"\" string is never closed";
                    return tables;
                }
                seg = lines[n];
            }
            if (!after.empty() && after[0] != '#') {
                error = key + ": text after the closing \"\"\"";
                return tables;
            }
            if (!claim()) return tables;
            if (!current_section.empty()) tables[current_section].values[key] = unescape(raw);
            continue;
        }

        if (starts_with(rest, "\"")) {
            const size_t end = closing_quote(rest);
            if (end == std::string::npos) {
                error = key + ": the string has no closing quote";
                return tables;
            }
            after = trim(rest.substr(end + 1));
            if (!after.empty() && after[0] != '#') {
                error = key + ": text after the closing quote";
                return tables;
            }
            if (!claim()) return tables;
            if (!current_section.empty()) tables[current_section].values[key] = unescape(rest.substr(1, end - 1));
            continue;
        }

        if (current_section.empty()) {
            continue;  // key-value before any section
        }

        if (!claim()) return tables;

        // Array [...] or a number (a comment may follow it)
        const std::string value_str = trim(strip_comment(rest));
        if (starts_with(value_str, "[") && value_str.back() == ']') {
            if (value_str != rest) tables[current_section].commented_arrays.insert(key);
            std::string array_content = value_str.substr(1, value_str.length() - 2);
            std::vector<float> array;
            std::istringstream arr_iss(array_content);
            std::string item;
            while (std::getline(arr_iss, item, ',')) {
                item = trim(item);
                if (!item.empty()) {
                    auto val = parse_float(item);
                    if (val) {
                        array.push_back(val.value());
                    } else {
                        tables[current_section].bad_arrays.insert(key);
                    }
                }
            }
            tables[current_section].arrays[key] = array;
            continue;
        }

        tables[current_section].values[key] = value_str;
    }

    return tables;
}

// The rules of [curves] (spec/WAH_FORMAT.md, rules 3 to 5): the one check that the reader and the writer share.
std::string check_curves(const Curves& c) {
    const size_t n = c.position.size();
    if (n < 2 || n > (size_t)MAX_CURVE_POINTS) return "position: needs 2 to 128 values";
    if (c.position.front() != 0.0f || c.position.back() != 1.0f) return "position: the first value must be 0.0 and the last 1.0";
    for (size_t i = 1; i < n; i++) {
        if (!(c.position[i] > c.position[i - 1])) return "position: the values must be strictly increasing";  // also refuses nan
    }
    if (c.freq.size() != n) return "freq: needs one value per position";
    for (float f : c.freq) {
        if (!std::isfinite(f) || f <= 0.0f) return "freq: each value must be finite and above 0";
    }
    if (!c.q.empty() && c.q.size() != n && c.q.size() != 2) return "q: needs one value per position or two (heel, toe), or none";
    if (!c.gain_db.empty() && c.gain_db.size() != n && c.gain_db.size() != 2) return "gain_db: needs one value per position or two (heel, toe), or none";
    for (float q : c.q) {
        if (!std::isfinite(q) || q <= 0.0f) return "q: each value must be finite and above 0";
    }
    for (float g : c.gain_db) {
        if (!std::isfinite(g)) return "gain_db: each value must be finite";
    }
    return std::string();
}

// The rules of [shape_heel] and [shape_toe] (spec/WAH_FORMAT.md, rule 9): one table at a time, the one check that the reader and the writer share.
static std::string check_shape_table(const ShapeTable& t, const char* block) {
    const std::string b = std::string("[") + block + "] ";
    const size_t n = t.freq.size();
    if (n < 2 || n > (size_t)MAX_SHAPE_POINTS) return b + "freq: needs 2 to 128 values";
    for (float f : t.freq) {
        if (!std::isfinite(f) || f <= 0.0f) return b + "freq: each value must be finite and above 0";
    }
    for (size_t i = 1; i < n; i++) {
        if (!(t.freq[i] > t.freq[i - 1])) return b + "freq: the values must be strictly increasing";
    }
    if (t.db.size() != n) return b + "db: needs one value per freq";
    for (float d : t.db) {
        if (!std::isfinite(d)) return b + "db: each value must be finite";
    }
    return std::string();
}

std::string check_shape(const Shape& shape) {
    const std::string heel = check_shape_table(shape.heel, "shape_heel");
    return heel.empty() ? check_shape_table(shape.toe, "shape_toe") : heel;
}

// What is wrong with the way the arrays of a block are written, or the empty string. Every key that is written must be an array of numbers
// on one line (an item that is not a number, or a single value, is an error, never "not measured": rule 6), and no comment may follow the
// array on its line (rule 6, kept in round 33). The first kind of problem is looked for in every key before the second.
static std::string arrays_problem(const TomlTable& t, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        if (t.bad_arrays.count(key) || t.values.count(key)) return std::string(key) + ": not an array of numbers written on one line";
    }
    for (const char* key : keys) {
        if (t.commented_arrays.count(key)) return std::string(key) + ": a comment after the array on the same line (put it on its own line)";
    }
    return std::string();
}

static std::vector<float> array_of(const TomlTable& t, const char* key) {
    auto it = t.arrays.find(key);
    return it == t.arrays.end() ? std::vector<float>{} : it->second;
}

// Format v1.1 [curves]: the rules are in spec/WAH_FORMAT.md. nullopt means the block is refused; why says the reason.
static std::optional<Curves> read_curves(const TomlTable& t, std::string& why) {
    why = arrays_problem(t, {"position", "freq", "q", "gain_db"});
    if (!why.empty()) return {};
    Curves c;
    c.position = array_of(t, "position");
    c.freq = array_of(t, "freq");
    c.q = array_of(t, "q");
    c.gain_db = array_of(t, "gain_db");
    why = check_curves(c);
    if (!why.empty()) return {};
    return c;
}

std::optional<Profile> toml_to_profile(const std::string& toml_text, std::string* error) {
    auto fail = [error](const std::string& why) {
        if (error) *error = why;
        return std::optional<Profile>();
    };
    std::string parse_error;
    auto tables = parse_toml(toml_text, parse_error);
    if (!parse_error.empty()) return fail(parse_error);

    Profile profile;
    bool v11 = false;  // format_version "1.1" (absent or "1.0" is v1; anything else is not known to this reader)

    // Parse [meta]
    if (tables.count("meta")) {
        auto& meta_table = tables["meta"];
        if (meta_table.values.count("format_version")) {
            const std::string& version = meta_table.values["format_version"];
            if (version == "1.1") {
                v11 = true;
            } else if (version != "1.0") {
                return fail("format_version must be \"1.0\" or \"1.1\" (found \"" + version + "\")");
            }
        }
        if (meta_table.values.count("name")) {
            profile.meta.name = meta_table.values["name"];
        }
        if (meta_table.values.count("author")) {
            profile.meta.author = meta_table.values["author"];
        }
        if (meta_table.values.count("make")) {
            profile.meta.make = meta_table.values["make"];
        }
        if (meta_table.values.count("model")) {
            profile.meta.model = meta_table.values["model"];
        }
        if (meta_table.values.count("description")) {
            profile.meta.description = meta_table.values["description"];
        }
        if (meta_table.values.count("created")) {
            profile.meta.created = meta_table.values["created"];
        }
    }

    // A number field that is written must be one finite number; otherwise the file is refused (it used to keep the default in silence).
    std::string problem;
    auto number = [&problem](const char* section, TomlTable& table, const char* key, float& dest) {
        if (!problem.empty()) return;
        const std::string where = std::string("[") + section + "] " + key + ": ";
        if (table.arrays.count(key)) {
            problem = where + "needs one number, not an array";
        } else if (table.values.count(key)) {
            const auto v = parse_float(table.values[key]);
            if (!v) problem = where + "not a finite number (found \"" + table.values[key] + "\")";
            else dest = *v;
        }
    };
    // A word field must be one of the words of the format.
    auto word = [&problem](const char* section, TomlTable& table, const char* key, std::initializer_list<const char*> allowed, std::string& dest) {
        if (!problem.empty() || (!table.values.count(key) && !table.arrays.count(key))) return;
        const std::string found = table.arrays.count(key) ? std::string("an array") : table.values[key];
        for (const char* w : allowed) {
            if (!table.arrays.count(key) && found == w) { dest = found; return; }
        }
        std::string list;
        for (const char* w : allowed) list += std::string(list.empty() ? "" : " or ") + "\"" + w + "\"";
        problem = std::string("[") + section + "] " + key + ": must be " + list + " (found " + (table.arrays.count(key) ? found : "\"" + found + "\"") + ")";
    };

    // Parse [filter]
    if (tables.count("filter")) {
        auto& t = tables["filter"];
        std::string topology;
        word("filter", t, "topology", {"bandpass_2pole", "bandpass_4pole"}, topology);
        if (!topology.empty()) profile.filter.topology = (topology == "bandpass_4pole") ? Topology::Bandpass4Pole : Topology::Bandpass2Pole;
        number("filter", t, "freq_heel", profile.filter.freq_heel);
        number("filter", t, "freq_toe", profile.filter.freq_toe);
        number("filter", t, "q_min", profile.filter.q_min);
        number("filter", t, "q_max", profile.filter.q_max);
        number("filter", t, "peak_gain_db", profile.filter.peak_gain_db);
        number("filter", t, "bass_shelf_db", profile.filter.bass_shelf_db);
        number("filter", t, "bass_shelf_freq", profile.filter.bass_shelf_freq);
    }

    // Parse [sweep]: a custom curve needs exactly LUT_SIZE numbers in lut; with any other curve the lut is not read.
    if (tables.count("sweep")) {
        auto& t = tables["sweep"];
        std::string curve;
        word("sweep", t, "curve", {"linear", "log", "custom"}, curve);
        if (curve == "linear") profile.sweep.curve = SweepCurve::Linear;
        else if (curve == "log") profile.sweep.curve = SweepCurve::Log;
        else if (curve == "custom") profile.sweep.curve = SweepCurve::Custom;
        if (problem.empty() && profile.sweep.curve == SweepCurve::Custom) {
            const auto it = t.arrays.find("lut");
            if (it == t.arrays.end() || t.bad_arrays.count("lut") || it->second.size() != (size_t)LUT_SIZE) {
                problem = "[sweep] lut: a custom curve needs exactly " + std::to_string(LUT_SIZE) + " numbers written on one line";
            } else {
                for (int i = 0; i < LUT_SIZE; i++) profile.sweep.lut[i] = it->second[i];
            }
        }
    }

    // Parse [levels]
    if (tables.count("levels")) {
        auto& t = tables["levels"];
        number("levels", t, "input_gain_db", profile.levels.input_gain_db);
        number("levels", t, "output_gain_db", profile.levels.output_gain_db);
        number("levels", t, "mix", profile.levels.mix);
    }

    // Parse [dynamics]
    if (tables.count("dynamics")) {
        auto& t = tables["dynamics"];
        number("dynamics", t, "resonance_low", profile.dynamics.resonance_low);
        number("dynamics", t, "resonance_mid", profile.dynamics.resonance_mid);
        number("dynamics", t, "resonance_high", profile.dynamics.resonance_high);
    }
    if (!problem.empty()) return fail(problem);

    // Parse [curves] (v1.1 only: a block in a file that does not say 1.1 is refused)
    if (tables.count("curves")) {
        if (!v11) return fail("[curves] needs format_version = \"1.1\" in [meta]");
        std::string why;
        auto curves = read_curves(tables["curves"], why);
        if (!curves) return fail("[curves] " + why);
        profile.curves = std::move(*curves);
    }

    // Parse [shape_heel] and [shape_toe] (v1.1 only, both or none: rules 7 and 8)
    const bool has_heel = tables.count("shape_heel") > 0, has_toe = tables.count("shape_toe") > 0;
    if (has_heel || has_toe) {
        if (!v11) return fail(std::string(has_heel ? "[shape_heel]" : "[shape_toe]") + " needs format_version = \"1.1\" in [meta]");
        if (!has_heel || !has_toe) {
            return fail(std::string(has_heel ? "[shape_toe]" : "[shape_heel]") + " is missing: [shape_heel] and [shape_toe] come together");
        }
        Shape shape;
        for (const char* block : {"shape_heel", "shape_toe"}) {
            const TomlTable& t = tables[block];
            const std::string why = arrays_problem(t, {"freq", "db"});
            if (!why.empty()) return fail(std::string("[") + block + "] " + why);
            ShapeTable& dest = (std::string(block) == "shape_heel") ? shape.heel : shape.toe;
            dest.freq = array_of(t, "freq");
            dest.db = array_of(t, "db");
        }
        const std::string why = check_shape(shape);
        if (!why.empty()) return fail(why);
        profile.shape = std::move(shape);
    }

    // Sanity check: name is required
    if (profile.meta.name.empty()) {
        return fail("[meta] name is missing");
    }

    if (error) error->clear();
    return profile;
}

// ─────────────────────────────────────────────
// TOML Serialization
// ─────────────────────────────────────────────

static void write_array(std::ostringstream& oss, const char* key, const std::vector<float>& values) {
    oss << key << " = [";
    for (size_t i = 0; i < values.size(); i++) {
        if (i > 0) oss << ", ";
        oss << to_string(values[i]);
    }
    oss << "]\n";
}

// A profile whose Curves or Shape the reader would refuse (check_curves and check_shape say so) gives an empty string: nothing is
// written that would come back refused. Call check_curves or check_shape to learn why.
std::string profile_to_toml(const Profile& profile) {
    if (profile.curves && !check_curves(*profile.curves).empty()) return std::string();
    if (profile.shape && !check_shape(*profile.shape).empty()) return std::string();
    std::ostringstream oss;

    // [meta]
    oss << "[meta]\n";
    oss << "name = " << quote_string(profile.meta.name) << "\n";
    if (!profile.meta.author.empty()) {
        oss << "author = " << quote_string(profile.meta.author) << "\n";
    }
    if (!profile.meta.make.empty()) {
        oss << "make = " << quote_string(profile.meta.make) << "\n";
    }
    if (!profile.meta.model.empty()) {
        oss << "model = " << quote_string(profile.meta.model) << "\n";
    }
    if (!profile.meta.description.empty()) {
        oss << "description = " << quote_string(profile.meta.description) << "\n";
    }
    if (!profile.meta.created.empty()) {
        oss << "created = " << quote_string(profile.meta.created) << "\n";
    }
    if (profile.curves || profile.shape) {
        oss << "format_version = \"1.1\"\n";
    }
    oss << "\n";

    // [filter]
    oss << "[filter]\n";
    oss << "topology = \"" << (profile.filter.topology == Topology::Bandpass4Pole ? "bandpass_4pole" : "bandpass_2pole") << "\"\n";
    oss << "freq_heel = " << to_string(profile.filter.freq_heel) << "\n";
    oss << "freq_toe = " << to_string(profile.filter.freq_toe) << "\n";
    oss << "q_min = " << to_string(profile.filter.q_min) << "\n";
    oss << "q_max = " << to_string(profile.filter.q_max) << "\n";
    oss << "peak_gain_db = " << to_string(profile.filter.peak_gain_db) << "\n";
    oss << "bass_shelf_db = " << to_string(profile.filter.bass_shelf_db) << "\n";
    oss << "bass_shelf_freq = " << to_string(profile.filter.bass_shelf_freq) << "\n";
    oss << "\n";

    // [sweep]
    oss << "[sweep]\n";
    oss << "curve = \"";
    switch (profile.sweep.curve) {
        case SweepCurve::Linear: oss << "linear"; break;
        case SweepCurve::Log: oss << "log"; break;
        case SweepCurve::Custom: oss << "custom"; break;
    }
    oss << "\"\n";

    if (profile.sweep.curve == SweepCurve::Custom) {
        oss << "lut = [";
        for (int i = 0; i < LUT_SIZE; i++) {
            if (i > 0) oss << ", ";
            oss << to_string(profile.sweep.lut[i]);
        }
        oss << "]\n";
    } else {
        oss << "lut = []\n";
    }
    oss << "\n";

    // [levels]
    oss << "[levels]\n";
    oss << "input_gain_db = " << to_string(profile.levels.input_gain_db) << "\n";
    oss << "output_gain_db = " << to_string(profile.levels.output_gain_db) << "\n";
    oss << "mix = " << to_string(profile.levels.mix) << "\n";
    oss << "\n";

    // [dynamics]
    oss << "[dynamics]\n";
    oss << "resonance_low = " << to_string(profile.dynamics.resonance_low) << "\n";
    oss << "resonance_mid = " << to_string(profile.dynamics.resonance_mid) << "\n";
    oss << "resonance_high = " << to_string(profile.dynamics.resonance_high) << "\n";

    // [curves] (v1.1)
    if (profile.curves) {
        oss << "\n[curves]\n";
        write_array(oss, "position", profile.curves->position);
        write_array(oss, "freq", profile.curves->freq);
        if (!profile.curves->q.empty()) write_array(oss, "q", profile.curves->q);
        if (!profile.curves->gain_db.empty()) write_array(oss, "gain_db", profile.curves->gain_db);
    }

    // [shape_heel] and [shape_toe] (v1.1): after [curves], the heel first
    if (profile.shape) {
        oss << "\n[shape_heel]\n";
        write_array(oss, "freq", profile.shape->heel.freq);
        write_array(oss, "db", profile.shape->heel.db);
        oss << "\n[shape_toe]\n";
        write_array(oss, "freq", profile.shape->toe.freq);
        write_array(oss, "db", profile.shape->toe.db);
    }

    return oss.str();
}

// ─────────────────────────────────────────────
// File I/O
// ─────────────────────────────────────────────

std::optional<Profile> load_toml(const std::string& path, std::string* error) {
    std::ifstream file(path);
    if (!file.is_open()) {
        if (error) *error = "cannot open the file";
        return {};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    return toml_to_profile(buffer.str(), error);
}

bool save_toml(const Profile& profile, const std::string& path) {
    // Refuse before the file is opened: a file that is already there is not truncated by a save that is refused.
    if (profile.curves && !check_curves(*profile.curves).empty()) return false;
    if (profile.shape && !check_shape(*profile.shape).empty()) return false;
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << profile_to_toml(profile);
    return file.good();
}

} // namespace wah
