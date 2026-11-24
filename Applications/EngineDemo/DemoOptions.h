#pragma once

#include "Core/Common/Error.h"

#include <cstdint>
#include <optional>
#include <string>

namespace demo {

struct DemoOptions {
    std::optional<std::uint64_t> frameLimit; // --frames N: quit after N frames
    std::string screenshotPath;              // --screenshot FILE.ppm: capture the last frame
    std::string logFilePath;                 // --log FILE
    std::string assetRoot;                   // --assets DIR (absolute); default: next to the executable
    bool audio = true;                       // --no-audio
    bool vsync = true;                       // --vsync on|off
    bool fullscreen = false;                 // --fullscreen
    bool selfTest = false;                   // --self-test
    bool verbose = false;                    // --verbose
    bool showHelp = false;                   // --help
};

[[nodiscard]] schuimm::Result<DemoOptions> parseOptions(int argc, char** argv);
[[nodiscard]] const char* usageText();

} // namespace demo
