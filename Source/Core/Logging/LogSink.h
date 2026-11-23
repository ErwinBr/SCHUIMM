#pragma once

#include "Core/Logging/LogLevel.h"

#include <chrono>
#include <string_view>

namespace schuimm {

struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::chrono::system_clock::time_point timestamp;
    std::string_view category;
    std::string_view message;
};

// Destination for log output. Sinks are called with the Logger's mutex held,
// so implementations need no locking of their own but must not log re-entrantly.
class LogSink {
public:
    virtual ~LogSink() = default;

    // `formattedLine` is the complete line without a trailing newline.
    virtual void write(const LogRecord& record, std::string_view formattedLine) = 0;
    virtual void flush() {}
};

} // namespace schuimm
