#include "paths.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <filesystem>

namespace vigil::paths {
namespace fs = std::filesystem;

namespace {
std::string knownLocalAppData() {
    PWSTR path = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &path)) && path) {
        out = fs::path(path).string();
    }
    if (path) CoTaskMemFree(path);
    return out;
}
}  // namespace

std::string dataDirectory(const std::string& fallbackDir) {
    const std::string base = knownLocalAppData();
    if (!base.empty()) return (fs::path(base) / "Vigil").string();
    return (fs::path(fallbackDir.empty() ? fs::current_path() : fs::path(fallbackDir)) / "vigil").string();
}

std::string runtimeDirectory() { return (fs::path(dataDirectory()) / "runtime").string(); }

bool migrateLegacyData(const std::string& legacyDataDir, const std::string& dataDir) {
    std::error_code ec;
    if (legacyDataDir.empty() || dataDir.empty()) return false;
    const fs::path from(legacyDataDir), to(dataDir);
    if (fs::equivalent(from, to, ec)) return false;
    if (!fs::exists(from / "config.json", ec)) return false;
    if (fs::exists(to / "config.json", ec)) return false;
    fs::create_directories(to, ec);
    for (const char* item : {"config.json", "history", "sessions"}) {
        const fs::path src = from / item;
        if (!fs::exists(src, ec)) continue;
        fs::copy(src, to / item, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    }
    return fs::exists(to / "config.json", ec);
}

}  // namespace vigil::paths
