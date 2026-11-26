// ActorDemo: composition root. Wires the native platform, OpenGL and a ScriptHost together and
// runs ActorDemoListener.

#include "ActorDemo.h"
#include "Core/Application/Application.h"
#include "Core/Common/StringBuilder.h"
#include "Core/Logging/Logger.h"
#include "Core/Logging/StandardSinks.h"
#include "Graphics/OpenGL/OpenGLGraphicsDevice.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Platform/NativePlatform.h"
#include "Resource/AssetLocator.h"
#include "Script/Runtime/ScriptHost.h"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <string_view>

using namespace schuimm;

namespace {

constexpr const char* usage =
    "Usage: ActorDemo [options]\n"
    "  --frames N      quit after N frames\n"
    "  --auto          run a timed sequence of the interactive actions\n"
    "  --assets PATH   asset root (default: next to the executable)\n"
    "  --screenshot F  with --frames: save the last frame as a PPM image\n"
    "  --help          show this text\n";

Result<actordemo::Options> parseOptions(int argc, char** argv)
{
    actordemo::Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            options.showHelp = true;
        } else if (argument == "--auto") {
            options.automatic = true;
        } else if ((argument == "--frames" || argument == "--assets" || argument == "--screenshot") && i + 1 < argc) {
            const std::string_view value = argv[++i];
            if (argument == "--assets") {
                options.assetRoot = std::string(value);
                continue;
            }
            if (argument == "--screenshot") {
                options.screenshotPath = std::string(value);
                continue;
            }
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), options.frames);
            if (error != std::errc{} || end != value.data() + value.size()) {
                return makeError(ErrorCode::InvalidArgument, concat("invalid frame count '", value, "'"));
            }
        } else {
            return makeError(ErrorCode::InvalidArgument, concat("unknown or incomplete option '", argument, "'"));
        }
    }
    return options;
}

} // namespace

int main(int argc, char** argv)
{
    auto options = parseOptions(argc, argv);
    if (!options) {
        std::fprintf(stderr, "%s\n\n%s", options.error().message.c_str(), usage);
        return 2;
    }
    if (options->showHelp) {
        std::fputs(usage, stdout);
        return 0;
    }

    Logger logger(LogLevel::Info);
    logger.addSink(std::make_unique<ConsoleSink>());

    auto assets = [&]() -> Result<AssetLocator> {
        if (!options->assetRoot.empty()) return AssetLocator::create(options->assetRoot);
        auto executable = fs::executablePath();
        if (!executable) return std::unexpected(executable.error());
        return locateAssetRoot(defaultAssetRootCandidates(*executable));
    }();
    if (!assets) {
        logger.error("ActorDemo", concat("Assets not found: ", describe(assets.error())));
        return 1;
    }

    auto platform = createNativePlatform(logger);
    if (!platform) {
        logger.error("ActorDemo", concat("Platform initialisation failed: ", describe(platform.error())));
        return 1;
    }
    WindowDescription windowDescription;
    windowDescription.title = "SCHUIMM ActorDemo";
    windowDescription.size = {1280, 720};
    auto window = (*platform)->createWindow(windowDescription);
    if (!window) {
        logger.error("ActorDemo", concat("Window creation failed: ", describe(window.error())));
        return 1;
    }
    auto surface = (*window)->createOpenGLSurface(defaultOpenGLSurfaceDescription());
    if (!surface) {
        logger.error("ActorDemo", concat("OpenGL context creation failed: ", describe(surface.error())));
        return 1;
    }
    GraphicsDeviceSettings graphicsSettings;
    graphicsSettings.presentation.virtualResolution = {640, 360}; // The rooms' size.
    graphicsSettings.presentation.scalingFilter = TextureFilter::Nearest;
    auto graphics = createOpenGLGraphicsDevice(std::move(*surface), graphicsSettings, logger);
    if (!graphics) {
        logger.error("ActorDemo", concat("Graphics initialisation failed: ", describe(graphics.error())));
        return 1;
    }

    // Runs the room scripts (the courtyard's); the listener binds the adventure API to it.
    // Actor scripts are not executed yet.
    script::ScriptHost scripts(logger);

    int exitCode = 0;
    {
        actordemo::ActorDemoListener listener(*options, *assets);
        Application application({logger, **platform, **window, **graphics, nullptr, &scripts}, listener);
        if (auto status = application.run(); !status) {
            logger.error("ActorDemo", concat("Application failed: ", describe(status.error())));
            exitCode = 1;
        } else {
            exitCode = listener.exitCode();
        }
    }
    graphics->reset();
    window->reset();
    platform->reset();
    return exitCode;
}
