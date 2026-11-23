// EngineDemo: composition root and manual integration test for the native foundation.
//
// This is the only place where concrete backends are chosen and wired together; everything
// else talks to interfaces (Platform, Window, GraphicsDevice, Renderer2D).

#include "Core/Application/Application.h"
#include "Core/Common/StringBuilder.h"
#include "Core/Logging/Logger.h"
#include "Core/Logging/StandardSinks.h"
#include "DemoListener.h"
#include "DemoOptions.h"
#include "Graphics/OpenGL/OpenGLGraphicsDevice.h"
#include "Platform/NativePlatform.h"

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

    int exitCode = 0;
    {
        demo::DemoListener listener(*options);
        Application application({logger, **platform, **window, **graphics}, listener);
        if (auto status = application.run(); !status) {
            logger.error("Demo", concat("Application failed: ", describe(status.error())));
            exitCode = 1;
        } else {
            exitCode = listener.exitCode();
        }
    }

    // Explicit, deterministic teardown in reverse order of creation.
    graphics->reset();
    window->reset();
    platform->reset();
    logger.info("Demo", concat("Clean shutdown, exit code ", exitCode));
    return exitCode;
}
