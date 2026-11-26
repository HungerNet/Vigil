#pragma once
// Vigil — where Vigil keeps its files.
//
//   %LOCALAPPDATA%\Vigil\            data root (config.json, history/, sessions/, logs/)
//   %LOCALAPPDATA%\Vigil\runtime\    the extracted Vigil.dll (written by Vigil.exe)
//
// Users never manage these folders by hand; Settings → Data opens them.

#include <string>

namespace vigil::paths {

// Data root. Falls back to `fallbackDir`\vigil when the known folder is unavailable.
std::string dataDirectory(const std::string& fallbackDir = {});
std::string runtimeDirectory();

// Copies a legacy data folder (2.0 kept data in <dll dir>\vigil) into the data root once, when
// the data root has no configuration yet. The legacy folder is left untouched. Returns true when
// something was migrated.
bool migrateLegacyData(const std::string& legacyDataDir, const std::string& dataDir);

}  // namespace vigil::paths
