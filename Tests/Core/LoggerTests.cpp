#include "TestFramework.h"

#include "Core/Logging/Logger.h"
#include "Core/Logging/StandardSinks.h"

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace schuimm;

namespace {

struct CapturedLine {
    LogLevel level;
    std::string category;
    std::string message;
    std::string line;
};

class CaptureSink final : public LogSink {
public:
    explicit CaptureSink(std::vector<CapturedLine>& out)
        : out_(out)
    {
    }
    void write(const LogRecord& record, std::string_view line) override
    {
        out_.push_back({record.level, std::string(record.category), std::string(record.message), std::string(line)});
    }
    void flush() override { ++flushes; }
    int flushes = 0;

private:
    std::vector<CapturedLine>& out_;
};

std::chrono::system_clock::time_point fixedTime()
{
    // Construct a local time: 2026-03-04 05:06:07.089
    std::tm local{};
    local.tm_year = 2026 - 1900;
    local.tm_mon = 2;
    local.tm_mday = 4;
    local.tm_hour = 5;
    local.tm_min = 6;
    local.tm_sec = 7;
    local.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&local)) + std::chrono::milliseconds(89);
}

} // namespace

SCHUIMM_TEST(Logger_FormatsTimestampLevelAndCategory)
{
    const LogRecord record{LogLevel::Warning, fixedTime(), "Graphics", "shader warning"};
    CHECK_EQ(formatLogLine(record), std::string("2026-03-04 05:06:07.089 [WARNING] [Graphics] shader warning"));

    const LogRecord noCategory{LogLevel::Info, fixedTime(), "", "hello"};
    CHECK_EQ(formatLogLine(noCategory), std::string("2026-03-04 05:06:07.089 [INFO   ] hello"));
}

SCHUIMM_TEST(Logger_WithoutSinksIsSilent)
{
    Logger logger;
    logger.info("Test", "nobody listens");
    CHECK(logger.isEnabled(LogLevel::Info));
}

SCHUIMM_TEST(Logger_FiltersByMinimumLevel)
{
    std::vector<CapturedLine> lines;
    Logger logger(LogLevel::Info, fixedTime);
    logger.addSink(std::make_unique<CaptureSink>(lines));

    logger.trace("Core", "trace");
    logger.debug("Core", "debug");
    logger.info("Core", "info");
    logger.warning("Core", "warning");
    logger.error("Core", "error");
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].level == LogLevel::Info);
    CHECK(lines[2].level == LogLevel::Error);
    CHECK_EQ(lines[1].category, std::string("Core"));
    CHECK_EQ(lines[1].message, std::string("warning"));

    logger.setMinimumLevel(LogLevel::Off);
    logger.error("Core", "suppressed");
    CHECK_EQ(lines.size(), std::size_t{3});
    CHECK(!logger.isEnabled(LogLevel::Error));

    logger.setMinimumLevel(LogLevel::Trace);
    logger.trace("Core", "now visible");
    CHECK_EQ(lines.size(), std::size_t{4});
}

SCHUIMM_TEST(Logger_DispatchesToAllSinksAndFlushes)
{
    std::vector<CapturedLine> first;
    std::vector<CapturedLine> second;
    auto firstSink = std::make_unique<CaptureSink>(first);
    CaptureSink* firstObserver = firstSink.get();
    Logger logger(LogLevel::Trace, fixedTime);
    logger.addSink(std::move(firstSink));
    logger.addSink(std::make_unique<CaptureSink>(second));
    logger.addSink(nullptr); // ignored

    logger.info("App", "both");
    CHECK_EQ(first.size(), std::size_t{1});
    CHECK_EQ(second.size(), std::size_t{1});
    CHECK_EQ(first[0].line, second[0].line);

    logger.flush();
    CHECK_EQ(firstObserver->flushes, 1);
}

SCHUIMM_TEST(Logger_IsThreadSafe)
{
    std::vector<CapturedLine> lines;
    Logger logger(LogLevel::Trace);
    logger.addSink(std::make_unique<CaptureSink>(lines));

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&logger] {
            for (int i = 0; i < 250; ++i) {
                logger.debug("Thread", "message");
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    CHECK_EQ(lines.size(), std::size_t{1000});
}

SCHUIMM_TEST(Logger_FileSinkWritesLines)
{
    const std::string path = std::string(SCHUIMM_TEST_OUTPUT_DIR) + "/logger_file_sink_test.log";
    {
        auto sink = FileSink::open(path, FileSink::Mode::Truncate);
        REQUIRE(sink.has_value());
        Logger logger(LogLevel::Info, fixedTime);
        logger.addSink(std::move(*sink));
        logger.info("File", "first line");
        logger.error("File", "second line");
    } // Logger destruction flushes and closes the file.

    std::ifstream input(path);
    REQUIRE(input.is_open());
    std::string first;
    std::string second;
    std::getline(input, first);
    std::getline(input, second);
    CHECK_EQ(first, std::string("2026-03-04 05:06:07.089 [INFO   ] [File] first line"));
    CHECK_EQ(second, std::string("2026-03-04 05:06:07.089 [ERROR  ] [File] second line"));
}

SCHUIMM_TEST(Logger_FileSinkReportsOpenFailure)
{
    const auto sink = FileSink::open(std::string(SCHUIMM_TEST_OUTPUT_DIR) + "/missing-directory/x/y.log");
    REQUIRE(!sink.has_value());
    CHECK(sink.error().code == ErrorCode::IoFailure);
}
