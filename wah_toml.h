/**
 * wah_toml.h
 * TOML parsing and serialization for .wah profile files.
 *
 * Implements a minimal TOML parser/serializer for the .wah format.
 * Handles the specific structure defined in WAH_FORMAT.md.
 */

#pragma once

#include "libwah.h"
#include <string>
#include <optional>

namespace wah {

/**
 * Parse a .wah file from disk.
 * Returns nullopt if the file cannot be opened or the parse fails; then, if error is not null, it says why (one line).
 * When the file is accepted, *error is set to the empty string.
 */
std::optional<Profile> load_toml(const std::string& path, std::string* error = nullptr);

/**
 * Save a profile to a .wah file.
 * Returns false on write error.
 */
bool save_toml(const Profile& profile, const std::string& path);

/**
 * Serialize a profile to a TOML string (for in-memory use).
 */
std::string profile_to_toml(const Profile& profile);

/**
 * Parse a TOML string into a profile.
 * Returns nullopt on parse error; then, if error is not null, it says why (one line, naming the field or the rule:
 * "format_version", "[curves] position: ...", "[meta] name ..."). When the text is accepted, *error is set to the empty string.
 */
std::optional<Profile> toml_to_profile(const std::string& toml_text, std::string* error = nullptr);

/**
 * The rules of the [curves] block of format v1.1 (spec/WAH_FORMAT.md, rules 3 to 5), as one function that the reader and the
 * writer both use, so that they cannot disagree. Returns the empty string if the Curves is acceptable, otherwise the rule it breaks
 * (one line, starting with the field: "position: ...", "freq: ...", "q: ...", "gain_db: ...").
 */
std::string check_curves(const Curves& curves);

/**
 * The rules of the [shape_heel] and [shape_toe] blocks of format v1.1 (spec/WAH_FORMAT.md, rules 8 and 9), as one function that the
 * reader and the writer both use. Returns the empty string if the Shape is acceptable, otherwise the rule it breaks (one line,
 * starting with the block and the key: "[shape_heel] freq: ...", "[shape_toe] db: ...").
 */
std::string check_shape(const Shape& shape);

} // namespace wah
