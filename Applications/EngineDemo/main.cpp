// EngineDemo: composition root and interactive demonstration of the engine.
//
// This is the only place where concrete backends are chosen and wired together; everything
// else talks to interfaces (Platform, Window, GraphicsDevice, Renderer2D, AudioDevice).

#include "Audio/Miniaudio/MiniaudioAudioDevice.h"
#include "Core/Application/Application.h"
#include "Core/Common/StringBuilder.h"
#include "Core/Logging/Logger.h"
#include "Core/Logging/StandardSinks.h"
#include "DemoListener.h"
#include "DemoOptions.h"
#include "Graphics/OpenGL/OpenGLGraphicsDevice.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Platform/NativePlatform.h"
#include "Resource/AssetLocator.h"

#include <cstdio>

#if defined(_WIN32)
// Ask hybrid-graphics laptops (NVIDIA Optimus, AMD PowerXpress) to run this executable on the
// discrete GPU. These exported symbols are read by the drivers; they are not engine state.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

using namespace schuimm;

int main(int argc, char** argv)
{
    auto options = demo::parseOptions(argc, argv);
    if (!options) {
        std::fprintf(stderr, "%s\n\n%s", options.error().message.c_str(), demo::usageText());
        return 2;
    }
    if (options->showHelp) {
        std::fputs(demo::usageText(), stdout);
        return 0;
    }

    Logger logger(options->verbose ? LogLevel::Debug : LogLevel::Info);
    logger.addSink(std::make_unique<ConsoleSink>());
    if (!options->logFilePath.empty()) {
        if (auto sink = FileSink::open(options->logFilePath)) {
            logger.addSink(std::move(*sink));
        } else {
            logger.warning("Demo", sink.error().message);
        }
    }
    logger.info("Demo", "SCHUIMM EngineDemo starting");

    // Assets: an explicit root, or the packaged location next to the executable (inside the
    // bundle on macOS). Never the current working directory.
    auto assets = [&]() -> Result<AssetLocator> {
        if (!options->assetRoot.empty()) {
            return AssetLocator::create(options->assetRoot);
        }
        auto executable = fs::executablePath();
        if (!executable) {
            return std::unexpected(executable.error());
        }
        return locateAssetRoot(defaultAssetRootCandidates(*executable));
    }();
    if (!assets) {
        logger.error("Demo", concat("Assets not found: ", describe(assets.error())));
        return 1;
    }
    logger.info("Demo", concat("Asset root: ", assets->root()));

    auto platform = createNativePlatform(logger);
    if (!platform) {
        logger.error("Demo", concat("Platform initialisation failed: ", describe(platform.error())));
        return 1;
    }
    logger.addSink((*platform)->createDiagnosticSink());

    WindowDescription windowDescription;
    windowDescription.title = "SCHUIMM EngineDemo";
    windowDescription.size = {1280, 720};
    windowDescription.fullscreen = options->fullscreen;
    auto window = (*platform)->createWindow(windowDescription);
    if (!window) {
        logger.error("Demo", concat("Window creation failed: ", describe(window.error())));
        return 1;
    }

    auto surface = (*window)->createOpenGLSurface(defaultOpenGLSurfaceDescription());
    if (!surface) {
        logger.error("Demo", concat("OpenGL context creation failed: ", describe(surface.error())));
        return 1;
    }

    GraphicsDeviceSettings graphicsSettings;
    graphicsSettings.vsync = options->vsync;
    graphicsSettings.presentation.virtualResolution = {640, 360};
    graphicsSettings.presentation.scalingFilter = TextureFilter::Nearest;
    auto graphics = createOpenGLGraphicsDevice(std::move(*surface), graphicsSettings, logger);
    if (!graphics) {
        logger.error("Demo", concat("Graphics initialisation failed: ", describe(graphics.error())));
        return 1;
    }

    // Audio is optional: without an output device the demo runs silently.
    std::unique_ptr<AudioDevice> audio;
    if (options->audio) {
        if (auto device = createMiniaudioAudioDevice(AudioDeviceSettings{}, logger)) {
            audio = std::move(*device);
        } else {
            logger.warning("Demo", concat("Audio unavailable, continuing without sound: ", describe(device.error())));
        }
    }

    int exitCode = 0;
    {
        demo::DemoListener listener(*options, *assets);
        Application application({logger, **platform, **window, **graphics, audio.get()}, listener);
        if (auto status = application.run(); !status) {
            logger.error("Demo", concat("Application failed: ", describe(status.error())));
            exitCode = 1;
        } else {
            exitCode = listener.exitCode();
        }
    }

    // Explicit, deterministic teardown in reverse order of creation. The scene released its
    // textures, fonts, render targets and sounds in onShutdown(), while the devices were alive.
    audio.reset();
    graphics->reset();
    window->reset();
    platform->reset();
    logger.info("Demo", concat("Clean shutdown, exit code ", exitCode));
    return exitCode;
}
