/**
 * wah_toml.cpp
 * TOML parsing and serialization implementation.
 */

#include "wah_toml.h"
#include <fstream>
#include <sstream>
#include <map>

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

static std::string to_string(float value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.10g", value);
    return std::string(buf);
}

static std::optional<float> parse_float(const std::string& str) {
    try {
        size_t idx;
        float value = std::stof(str, &idx);
        if (idx == str.length()) return value;
    } catch (...) {}
    return {};
}

static std::optional<std::string> parse_string(const std::string& str) {
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
};

static std::map<std::string, TomlTable> parse_toml(const std::string& text) {
    std::map<std::string, TomlTable> tables;
    std::istringstream iss(text);
    std::string line;
    std::string current_section;

    while (std::getline(iss, line)) {
        line = trim(line);

        // Skip empty lines and comments
        if (line.empty() || starts_with(line, "#")) {
            continue;
        }

        // Section header
        if (starts_with(line, "[") && line.back() == ']') {
            current_section = line.substr(1, line.length() - 2);
            current_section = trim(current_section);
            tables[current_section] = TomlTable{};
            continue;
        }

        if (current_section.empty()) {
            continue;  // key-value before any section
        }

        // Key-value pair
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;  // skip malformed lines
        }

        std::string key = trim(line.substr(0, eq_pos));
        std::string value_str = trim(line.substr(eq_pos + 1));

        // Try to parse as different types
        // Array [...]
        if (starts_with(value_str, "[") && value_str.back() == ']') {
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
                    }
                }
            }
            tables[current_section].arrays[key] = array;
            continue;
        }

        // String (quoted)
        if (starts_with(value_str, "\"") && value_str.back() == '"') {
            tables[current_section].values[key] = value_str.substr(1, value_str.length() - 2);
            continue;
        }

        // Number or other
        tables[current_section].values[key] = value_str;
    }

    return tables;
}

std::optional<Profile> toml_to_profile(const std::string& toml_text) {
    auto tables = parse_toml(toml_text);

    Profile profile;

    // Parse [meta]
    if (tables.count("meta")) {
        auto& meta_table = tables["meta"];
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

    // Parse [filter]
    if (tables.count("filter")) {
        auto& filter_table = tables["filter"];
        if (filter_table.values.count("topology")) {
            std::string topo = filter_table.values["topology"];
            profile.filter.topology = (topo == "bandpass_4pole") ? Topology::Bandpass4Pole : Topology::Bandpass2Pole;
        }
        if (filter_table.values.count("freq_heel")) {
            auto val = parse_float(filter_table.values["freq_heel"]);
            if (val) profile.filter.freq_heel = val.value();
        }
        if (filter_table.values.count("freq_toe")) {
            auto val = parse_float(filter_table.values["freq_toe"]);
            if (val) profile.filter.freq_toe = val.value();
        }
        if (filter_table.values.count("q_min")) {
            auto val = parse_float(filter_table.values["q_min"]);
            if (val) profile.filter.q_min = val.value();
        }
        if (filter_table.values.count("q_max")) {
            auto val = parse_float(filter_table.values["q_max"]);
            if (val) profile.filter.q_max = val.value();
        }
        if (filter_table.values.count("peak_gain_db")) {
            auto val = parse_float(filter_table.values["peak_gain_db"]);
            if (val) profile.filter.peak_gain_db = val.value();
        }
        if (filter_table.values.count("bass_shelf_db")) {
            auto val = parse_float(filter_table.values["bass_shelf_db"]);
            if (val) profile.filter.bass_shelf_db = val.value();
        }
        if (filter_table.values.count("bass_shelf_freq")) {
            auto val = parse_float(filter_table.values["bass_shelf_freq"]);
            if (val) profile.filter.bass_shelf_freq = val.value();
        }
    }

    // Parse [sweep]
    if (tables.count("sweep")) {
        auto& sweep_table = tables["sweep"];
        if (sweep_table.values.count("curve")) {
            std::string curve = sweep_table.values["curve"];
            if (curve == "linear") {
                profile.sweep.curve = SweepCurve::Linear;
            } else if (curve == "log") {
                profile.sweep.curve = SweepCurve::Log;
            } else if (curve == "custom") {
                profile.sweep.curve = SweepCurve::Custom;
            }
        }
        if (sweep_table.arrays.count("lut")) {
            auto& lut_array = sweep_table.arrays["lut"];
            for (int i = 0; i < std::min((int)lut_array.size(), LUT_SIZE); i++) {
                profile.sweep.lut[i] = lut_array[i];
            }
        }
    }

    // Parse [levels]
    if (tables.count("levels")) {
        auto& levels_table = tables["levels"];
        if (levels_table.values.count("input_gain_db")) {
            auto val = parse_float(levels_table.values["input_gain_db"]);
            if (val) profile.levels.input_gain_db = val.value();
        }
        if (levels_table.values.count("output_gain_db")) {
            auto val = parse_float(levels_table.values["output_gain_db"]);
            if (val) profile.levels.output_gain_db = val.value();
        }
        if (levels_table.values.count("mix")) {
            auto val = parse_float(levels_table.values["mix"]);
            if (val) profile.levels.mix = val.value();
        }
    }

    // Parse [dynamics]
    if (tables.count("dynamics")) {
        auto& dyn_table = tables["dynamics"];
        if (dyn_table.values.count("resonance_low")) {
            auto val = parse_float(dyn_table.values["resonance_low"]);
            if (val) profile.dynamics.resonance_low = val.value();
        }
        if (dyn_table.values.count("resonance_mid")) {
            auto val = parse_float(dyn_table.values["resonance_mid"]);
            if (val) profile.dynamics.resonance_mid = val.value();
        }
        if (dyn_table.values.count("resonance_high")) {
            auto val = parse_float(dyn_table.values["resonance_high"]);
            if (val) profile.dynamics.resonance_high = val.value();
        }
    }

    // Sanity check: name is required
    if (profile.meta.name.empty()) {
        return {};
    }

    return profile;
}

// ─────────────────────────────────────────────
// TOML Serialization
// ─────────────────────────────────────────────

std::string profile_to_toml(const Profile& profile) {
    std::ostringstream oss;

    // [meta]
    oss << "[meta]\n";
    oss << "name = \"" << profile.meta.name << "\"\n";
    if (!profile.meta.author.empty()) {
        oss << "author = \"" << profile.meta.author << "\"\n";
    }
    if (!profile.meta.make.empty()) {
        oss << "make = \"" << profile.meta.make << "\"\n";
    }
    if (!profile.meta.model.empty()) {
        oss << "model = \"" << profile.meta.model << "\"\n";
    }
    if (!profile.meta.description.empty()) {
        oss << "description = \"" << profile.meta.description << "\"\n";
    }
    if (!profile.meta.created.empty()) {
        oss << "created = \"" << profile.meta.created << "\"\n";
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

    return oss.str();
}

// ─────────────────────────────────────────────
// File I/O
// ─────────────────────────────────────────────

std::optional<Profile> load_toml(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    return toml_to_profile(buffer.str());
}

bool save_toml(const Profile& profile, const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << profile_to_toml(profile);
    return file.good();
}

} // namespace wah
