#pragma once
// Vigil — lightweight channel logger.
// Each channel writes to <data>/logs/<channel>.log, capped and rotated.

#include <string>

namespace vigil::log {

void init(const std::string& logDirectory);
void write(const char* channel, const std::string& message);
void writef(const char* channel, const char* fmt, ...);

}  // namespace vigil::log
