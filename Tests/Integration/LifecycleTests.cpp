// Native lifecycle integration tests: real platform, window, OpenGL context and graphics device.
// They need a display (and on Linux an X server), so they are labelled `integration`.
//
// They check that the objects can be created, used, destroyed and created again in the
// documented order without leaking native state between cycles.

#include "Core/Logging/Logger.h"
#include "Core/Logging/StandardSinks.h"
#include "Graphics/OpenGL/OpenGLGraphicsDevice.h"
#include "Platform/NativePlatform.h"
#include "TestFramework.h"

#include <memory>

using namespace schuimm;

namespace {

// Warnings and errors from the backends are printed so a failing cycle explains itself.
struct TestLogger {
    TestLogger() { logger.addSink(std::make_unique<ConsoleSink>()); }
    Logger logger{LogLevel::Warning};
};

std::unique_ptr<Window> createWindow(Platform& platform)
{
    WindowDescription description;
    description.title = "SCHUIMM LifecycleTests";
    description.size = {320, 180};
    auto window = platform.createWindow(description);
    if (!window) {
        std::fprintf(stderr, "  window creation failed: %s\n", describe(window.error()).c_str());
        return nullptr;
    }
    return std::move(*window);
}

std::unique_ptr<GraphicsDevice> createDevice(Window& window, Logger& logger)
{
    auto surface = window.createOpenGLSurface(defaultOpenGLSurfaceDescription());
    if (!surface) {
        std::fprintf(stderr, "  surface creation failed: %s\n", describe(surface.error()).c_str());
        return nullptr;
    }
    auto device = createOpenGLGraphicsDevice(std::move(*surface), GraphicsDeviceSettings{}, logger);
    if (!device) {
        std::fprintf(stderr, "  device creation failed: %s\n", describe(device.error()).c_str());
        return nullptr;
    }
    return std::move(*device);
}

void renderFrames(Platform& platform, Window& window, GraphicsDevice& device, int frames)
{
    std::vector<WindowEvent> events;
    for (int i = 0; i < frames; ++i) {
        platform.pollEvents();
        window.drainEvents(events);
        events.clear();
        device.beginFrame();
        device.renderer().clear(Color{0.1f, 0.2f, 0.3f, 1.0f});
        device.endFrame();
        device.present();
    }
}

} // namespace

SCHUIMM_TEST(Lifecycle_FullStackCreateRenderDestroy)
{
    TestLogger testLogger;
    Logger& logger = testLogger.logger;
    auto platform = createNativePlatform(logger);
    REQUIRE(platform.has_value());
    auto window = createWindow(**platform);
    REQUIRE(window != nullptr);
    auto device = createDevice(*window, logger);
    REQUIRE(device != nullptr);
    CHECK(device->capabilities().coreProfile);
    renderFrames(**platform, *window, *device, 5);

    device.reset();
    window.reset();
    platform->reset();
}

SCHUIMM_TEST(Lifecycle_RecreateSurfaceOnSameWindow)
{
    // The OpenGL context (and on Windows the pixel format, which can be set only once per
    // window) must survive being destroyed and created again on the same window.
    TestLogger testLogger;
    Logger& logger = testLogger.logger;
    auto platform = createNativePlatform(logger);
    REQUIRE(platform.has_value());
    auto window = createWindow(**platform);
    REQUIRE(window != nullptr);
    for (int cycle = 0; cycle < 2; ++cycle) {
        auto device = createDevice(*window, logger);
        REQUIRE(device != nullptr);
        renderFrames(**platform, *window, *device, 3);
    }
    window.reset();
    platform->reset();
}

SCHUIMM_TEST(Lifecycle_RecreateWindows)
{
    TestLogger testLogger;
    Logger& logger = testLogger.logger;
    auto platform = createNativePlatform(logger);
    REQUIRE(platform.has_value());
    for (int cycle = 0; cycle < 3; ++cycle) {
        auto window = createWindow(**platform);
        REQUIRE(window != nullptr);
        auto device = createDevice(*window, logger);
        REQUIRE(device != nullptr);
        renderFrames(**platform, *window, *device, 3);
    }
    platform->reset();
}

SCHUIMM_TEST(Lifecycle_RecreatePlatform)
{
    // A second platform after the first one was torn down: re-registers the Win32 window class,
    // re-opens the X display, and reuses AppKit's NSApp singleton.
    TestLogger testLogger;
    Logger& logger = testLogger.logger;
    for (int cycle = 0; cycle < 2; ++cycle) {
        auto platform = createNativePlatform(logger);
        REQUIRE(platform.has_value());
        auto window = createWindow(**platform);
        REQUIRE(window != nullptr);
        auto device = createDevice(*window, logger);
        REQUIRE(device != nullptr);
        renderFrames(**platform, *window, *device, 2);
    }
}
