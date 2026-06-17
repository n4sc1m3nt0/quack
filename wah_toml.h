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
 * Returns nullopt if parse fails.
 */
std::optional<Profile> load_toml(const std::string& path);

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
 * Returns nullopt on parse error.
 */
std::optional<Profile> toml_to_profile(const std::string& toml_text);

} // namespace wah
