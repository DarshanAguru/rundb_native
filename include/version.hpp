#pragma once

#include <string_view>

namespace rundb {

/**
 * @brief Global RunDB Native version string and semver identifiers.
 *
 * Central source of truth for the server version across INFO command,
 * startup banner, CLI --version, and introspection catalogs.
 *
 * To change or migrate to a new version, simply update this variable:
 */
inline constexpr std::string_view VERSION = "1.0.0";

inline constexpr int VERSION_MAJOR = 1;
inline constexpr int VERSION_MINOR = 0;
inline constexpr int VERSION_PATCH = 0;

} // namespace rundb
