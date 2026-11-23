#include "DemoOptions.h"

#include <charconv>
#include <string_view>

namespace demo {

using schuimm::ErrorCode;
using schuimm::makeError;

const char* usageText()
{
    return "Usage: EngineDemo [options]\n"
           "  --frames N           Quit automatically after N rendered frames\n"
           "  --screenshot FILE    Save the last frame as a binary PPM image (with --frames or --self-test)\n"
           "  --vsync on|off       Initial vertical synchronisation (default on)\n"
           "  --fullscreen         Start in fullscreen\n"
           "  --self-test          Scripted check of resize, fullscreen and VSync; exit code reports the result\n"
           "  --log FILE           Also write the log to FILE\n"
           "  --verbose            Debug-level logging\n"
           "  --help               Show this text\n"
           "\n"
           "Controls: Esc quit | F11 or Alt+Enter fullscreen | V toggle VSync | R reset window size\n"
           "          Arrow keys move the triangle | mouse moves the sprite | buttons tint it | wheel scales it\n";
}

schuimm::Result<DemoOptions> parseOptions(int argc, char** argv)
{
    DemoOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        const auto nextValue = [&](std::string_view name) -> schuimm::Result<std::string_view> {
            if (i + 1 >= argc) {
                return makeError(ErrorCode::InvalidArgument, std::string(name) + " requires a value");
            }
            return std::string_view(argv[++i]);
        };

        if (argument == "--frames") {
            auto value = nextValue(argument);
            if (!value) {
                return std::unexpected(value.error());
            }
            std::uint64_t frames = 0;
            const auto [end, error] = std::from_chars(value->data(), value->data() + value->size(), frames);
            if (error != std::errc() || end != value->data() + value->size() || frames == 0) {
                return makeError(ErrorCode::InvalidArgument, "--frames expects a positive integer");
            }
            options.frameLimit = frames;
        } else if (argument == "--screenshot") {
            auto value = nextValue(argument);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.screenshotPath = std::string(*value);
        } else if (argument == "--log") {
            auto value = nextValue(argument);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.logFilePath = std::string(*value);
        } else if (argument == "--vsync") {
            auto value = nextValue(argument);
            if (!value) {
                return std::unexpected(value.error());
            }
            if (*value != "on" && *value != "off") {
                return makeError(ErrorCode::InvalidArgument, "--vsync expects 'on' or 'off'");
            }
            options.vsync = *value == "on";
        } else if (argument == "--fullscreen") {
            options.fullscreen = true;
        } else if (argument == "--self-test") {
            options.selfTest = true;
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--help" || argument == "-h") {
            options.showHelp = true;
        } else if (argument.starts_with("-psn_")) {
            // Process serial number passed by older macOS Finder launches: ignore.
        } else {
            return makeError(ErrorCode::InvalidArgument, "unknown option '" + std::string(argument) + "'");
        }
    }
    if (!options.screenshotPath.empty() && !options.frameLimit && !options.selfTest) {
        return makeError(ErrorCode::InvalidArgument, "--screenshot requires --frames or --self-test");
    }
    return options;
}

} // namespace demo
