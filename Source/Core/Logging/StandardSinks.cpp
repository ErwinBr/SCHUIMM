#include "Core/Logging/StandardSinks.h"

namespace schuimm {

void ConsoleSink::write(const LogRecord& record, std::string_view formattedLine)
{
    std::FILE* stream = record.level >= LogLevel::Warning ? stderr : stdout;
    if (stream == stderr) {
        std::fflush(stdout); // Keep stdout and stderr lines in chronological order.
    }
    std::fwrite(formattedLine.data(), 1, formattedLine.size(), stream);
    std::fputc('\n', stream);
    if (record.level >= LogLevel::Warning) {
        std::fflush(stream);
    }
}

void ConsoleSink::flush()
{
    std::fflush(stdout);
    std::fflush(stderr);
}

Result<std::unique_ptr<FileSink>> FileSink::open(const std::string& path, Mode mode)
{
    const auto openMode = std::ios::out | std::ios::binary | (mode == Mode::Append ? std::ios::app : std::ios::trunc);
    std::ofstream stream(path, openMode);
    if (!stream.is_open()) {
        return makeError(ErrorCode::IoFailure, "cannot open log file '" + path + "'");
    }
    return std::unique_ptr<FileSink>(new FileSink(std::move(stream)));
}

FileSink::FileSink(std::ofstream stream)
    : stream_(std::move(stream))
{
}

void FileSink::write(const LogRecord& record, std::string_view formattedLine)
{
    stream_.write(formattedLine.data(), static_cast<std::streamsize>(formattedLine.size()));
    stream_.put('\n');
    if (record.level >= LogLevel::Warning) {
        stream_.flush();
    }
}

void FileSink::flush()
{
    stream_.flush();
}

} // namespace schuimm
