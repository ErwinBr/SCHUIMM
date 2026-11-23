#pragma once

#include "Core/Logging/LogLevel.h"
#include "Core/Logging/LogSink.h"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm {

// Formats "YYYY-MM-DD HH:MM:SS.mmm [LEVEL  ] [Category] message" using local time.
[[nodiscard]] std::string formatLogLine(const LogRecord& record);

// Thread-safe logger with pluggable sinks. Not a singleton: create one in the
// composition root and pass it (by reference) to the systems that need it.
class Logger {
public:
    using WallClock = std::function<std::chrono::system_clock::time_point()>;

    explicit Logger(LogLevel minimumLevel = LogLevel::Info, WallClock wallClock = {});
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void addSink(std::unique_ptr<LogSink> sink);

    void setMinimumLevel(LogLevel level);
    [[nodiscard]] LogLevel minimumLevel() const;
    [[nodiscard]] bool isEnabled(LogLevel level) const;

    void log(LogLevel level, std::string_view category, std::string_view message);
    void trace(std::string_view category, std::string_view message) { log(LogLevel::Trace, category, message); }
    void debug(std::string_view category, std::string_view message) { log(LogLevel::Debug, category, message); }
    void info(std::string_view category, std::string_view message) { log(LogLevel::Info, category, message); }
    void warning(std::string_view category, std::string_view message) { log(LogLevel::Warning, category, message); }
    void error(std::string_view category, std::string_view message) { log(LogLevel::Error, category, message); }

    void flush();

private:
    mutable std::mutex mutex_;
    LogLevel minimumLevel_;
    WallClock wallClock_;
    std::vector<std::unique_ptr<LogSink>> sinks_;
};

} // namespace schuimm
