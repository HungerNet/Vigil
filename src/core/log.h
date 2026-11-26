#pragma once
// Vigil — lightweight channel logger.
// Each channel writes to <data>/logs/<channel>.log, capped and rotated. The most recent lines
// of all channels are also kept in memory for the Status page.

#include <cstddef>
#include <string>
#include <vector>

namespace vigil::log {

void init(const std::string& logDirectory);
const std::string& directory();
void write(const char* channel, const std::string& message);
void writef(const char* channel, const char* fmt, ...);
// Newest last; "HH:MM:SS  channel  message".
std::vector<std::string> recent(std::size_t maxLines);

}  // namespace vigil::log
