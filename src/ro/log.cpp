#include "log.h"

#include <atomic>
#include <cstdarg>

namespace ro {

namespace {
std::atomic<int> g_level{static_cast<int>(LogLevel::Warning)};
}

LogLevel logLevel()
{
    return static_cast<LogLevel>(g_level.load(std::memory_order_relaxed));
}

void setLogLevel(LogLevel level)
{
    g_level.store(static_cast<int>(level), std::memory_order_relaxed);
}

namespace detail {

void logMessage(LogLevel level, const char *file, int line, const char *fmt, ...)
{
    const char *name = "?";
    switch (level) {
    case LogLevel::Error: name = "error"; break;
    case LogLevel::Warning: name = "warn"; break;
    case LogLevel::Info: name = "info"; break;
    case LogLevel::Debug: name = "debug"; break;
    case LogLevel::Off: return;
    }

    std::fprintf(stderr, "[remoteobject %s] %s:%d: ", name, file, line);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
}

}  // namespace detail

}  // namespace ro
