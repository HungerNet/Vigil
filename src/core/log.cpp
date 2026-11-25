#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace vigil::log {
namespace {

constexpr std::uintmax_t kMaxLogBytes = 4u * 1024u * 1024u;

std::mutex g_mutex;
std::string g_dir;

std::string timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return buf;
}

}  // namespace

void init(const std::string& logDirectory) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dir = logDirectory;
    std::error_code ec;
    if (!g_dir.empty()) std::filesystem::create_directories(g_dir, ec);
}

void write(const char* channel, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_dir.empty() || !channel) return;
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
    out << timestamp() << "  " << message << "\n";
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

}  // namespace vigil::log
