#include "Core/Logging/Logger.h"

#include <cstdio>
#include <ctime>

namespace schuimm {

const char* toString(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARNING";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Off: return "OFF";
    }
    return "?";
}

namespace {

std::tm toLocalTime(std::time_t time)
{
    std::tm result{};
#if defined(_WIN32)
    localtime_s(&result, &time);
#else
    localtime_r(&time, &result);
#endif
    return result;
}

} // namespace

std::string formatLogLine(const LogRecord& record)
{
    using namespace std::chrono;
    const std::time_t seconds = system_clock::to_time_t(record.timestamp);
    const auto millis = duration_cast<milliseconds>(record.timestamp.time_since_epoch()).count() % 1000;
    const std::tm local = toLocalTime(seconds);

    char prefix[64];
    const int length = std::snprintf(prefix, sizeof(prefix), "%04d-%02d-%02d %02d:%02d:%02d.%03d [%-7s] ",
        local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec,
        static_cast<int>(millis < 0 ? millis + 1000 : millis), toString(record.level));

    std::string line;
    line.reserve(static_cast<std::size_t>(length) + record.category.size() + record.message.size() + 3);
    line.append(prefix, static_cast<std::size_t>(length));
    if (!record.category.empty()) {
        line += '[';
        line += record.category;
        line += "] ";
    }
    line += record.message;
    return line;
}

Logger::Logger(LogLevel minimumLevel, WallClock wallClock)
    : minimumLevel_(minimumLevel)
    , wallClock_(wallClock ? std::move(wallClock) : WallClock([] { return std::chrono::system_clock::now(); }))
{
}

Logger::~Logger()
{
    flush();
}

void Logger::addSink(std::unique_ptr<LogSink> sink)
{
    if (!sink) {
        return;
    }
    const std::scoped_lock lock(mutex_);
    sinks_.push_back(std::move(sink));
}

void Logger::setMinimumLevel(LogLevel level)
{
    const std::scoped_lock lock(mutex_);
    minimumLevel_ = level;
}

LogLevel Logger::minimumLevel() const
{
    const std::scoped_lock lock(mutex_);
    return minimumLevel_;
}

bool Logger::isEnabled(LogLevel level) const
{
    const std::scoped_lock lock(mutex_);
    return level != LogLevel::Off && level >= minimumLevel_;
}

void Logger::log(LogLevel level, std::string_view category, std::string_view message)
{
    const std::scoped_lock lock(mutex_);
    if (level == LogLevel::Off || level < minimumLevel_ || sinks_.empty()) {
        return;
    }
    const LogRecord record{level, wallClock_(), category, message};
    const std::string line = formatLogLine(record);
    for (const auto& sink : sinks_) {
        sink->write(record, line);
    }
}

void Logger::flush()
{
    const std::scoped_lock lock(mutex_);
    for (const auto& sink : sinks_) {
        sink->flush();
    }
}

} // namespace schuimm
