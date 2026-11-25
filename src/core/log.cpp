#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace vigil::log {
namespace {

constexpr std::uintmax_t kMaxLogBytes = 4u * 1024u * 1024u;
constexpr std::size_t kRecentLines = 200;

std::mutex g_mutex;
std::string g_dir;
std::deque<std::string> g_recent;

std::string timestamp(const char* format) {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), format, &tmv);
    return buf;
}

}  // namespace

void init(const std::string& logDirectory) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dir = logDirectory;
    std::error_code ec;
    if (!g_dir.empty()) std::filesystem::create_directories(g_dir, ec);
}

const std::string& directory() { return g_dir; }

void write(const char* channel, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!channel) return;
    g_recent.push_back(timestamp("%H:%M:%S") + "  " + channel + "  " + message);
    while (g_recent.size() > kRecentLines) g_recent.pop_front();
    if (g_dir.empty()) return;
    const std::filesystem::path path = std::filesystem::path(g_dir) / (std::string(channel) + ".log");
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && std::filesystem::file_size(path, ec) > kMaxLogBytes) {
        std::filesystem::path old = path;
        old += ".old";
        std::filesystem::remove(old, ec);
        std::filesystem::rename(path, old, ec);
    }
    std::ofstream out(path, std::ios::app | std::ios::binary);
    if (!out) return;
    out << timestamp("%Y-%m-%d %H:%M:%S") << "  " << message << "\n";
}

void writef(const char* channel, const char* fmt, ...) {
    if (!fmt) return;
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write(channel, buf);
}

std::vector<std::string> recent(std::size_t maxLines) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::size_t n = std::min(maxLines, g_recent.size());
    return std::vector<std::string>(g_recent.end() - static_cast<std::ptrdiff_t>(n), g_recent.end());
}

}  // namespace vigil::log
