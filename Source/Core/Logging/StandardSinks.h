#pragma once

#include "Core/Common/Error.h"
#include "Core/Logging/LogSink.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

namespace schuimm {

// Writes Info and below to stdout, Warning and above to stderr.
class ConsoleSink final : public LogSink {
public:
    void write(const LogRecord& record, std::string_view formattedLine) override;
    void flush() override;
};

// Appends (or truncates, see Mode) a UTF-8 text log file. Warnings and errors are flushed immediately
// so that they survive a crash.
class FileSink final : public LogSink {
public:
    enum class Mode { Truncate, Append };

    [[nodiscard]] static Result<std::unique_ptr<FileSink>> open(const std::string& path, Mode mode = Mode::Truncate);

    void write(const LogRecord& record, std::string_view formattedLine) override;
    void flush() override;

private:
    explicit FileSink(std::ofstream stream);
    std::ofstream stream_;
};

} // namespace schuimm
