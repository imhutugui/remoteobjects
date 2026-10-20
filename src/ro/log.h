// remoteobject - tiny logging facility (stderr, no dependencies).
#pragma once

#include <cstdio>

namespace ro {

enum class LogLevel { Off = 0, Error = 1, Warning = 2, Info = 3, Debug = 4 };

LogLevel logLevel();
void setLogLevel(LogLevel level);

namespace detail {
void logMessage(LogLevel level, const char *file, int line, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;
}  // namespace detail

#define RO_LOG(level, ...)                                                                         \
    do {                                                                                           \
        if (static_cast<int>(level) <= static_cast<int>(::ro::logLevel()))                         \
            ::ro::detail::logMessage(level, __FILE__, __LINE__, __VA_ARGS__);                      \
    } while (0)

#define RO_ERROR(...) RO_LOG(::ro::LogLevel::Error, __VA_ARGS__)
#define RO_WARN(...) RO_LOG(::ro::LogLevel::Warning, __VA_ARGS__)
#define RO_INFO(...) RO_LOG(::ro::LogLevel::Info, __VA_ARGS__)
#define RO_DEBUG(...) RO_LOG(::ro::LogLevel::Debug, __VA_ARGS__)

}  // namespace ro
