#include "DemoListener.h"

#include "Core/Common/StringBuilder.h"
#include "DemoAssets.h"
#include "Graphics/Interface/Presentation.h"

#include <algorithm>
#include <cmath>

namespace demo {

using namespace schuimm;

namespace {
constexpr const char* category = "Demo";
constexpr Extent2D defaultWindowSize{1280, 720};
constexpr Extent2D selfTestWindowSize{960, 540};
constexpr double stepTimeoutSeconds = 6.0;
constexpr double fpsMeasureSeconds = 1.5;
} // namespace

DemoListener::DemoListener(DemoOptions options)
    : options_(std::move(options))
{
}

Status DemoListener::onInitialize(Application& app)
{
    Renderer2D& renderer = app.graphics().renderer();

    const Image checker = makeCheckerboard(128, 8);
    auto checkerboard = renderer.createTexture({checker.size(), checker.pixels(), TextureFilter::Nearest});
    if (!checkerboard) {
        return std::unexpected(checkerboard.error());
    }
    checkerboard_ = std::move(*checkerboard);

    const Image orb = makeAlphaSprite(64);
    auto sprite = renderer.createTexture({orb.size(), orb.pixels(), TextureFilter::Linear});
    if (!sprite) {
        return std::unexpected(sprite.error());
    }
    sprite_ = std::move(*sprite);

    const GraphicsCapabilities& caps = app.graphics().capabilities();
    app.logger().info(category, concat("Platform backend: ", app.platform().name(), ", graphics: ", caps.api, " ",
                                       caps.majorVersion, ".", caps.minorVersion, caps.coreProfile ? " core" : ""));
    app.logger().info(category, "Textures created (checkerboard 128x128, alpha sprite 64x64)");
    if (options_.selfTest) {
        app.logger().info(category, "Self-test enabled");
    }
    return {};
}

void DemoListener::onUpdate(Application& app, double deltaSeconds)
{
    time_ += deltaSeconds;

    if (captureRequested_) {
        saveCapturedFrame(app);
    }

    handleInput(app, deltaSeconds);
    logInputEdges(app);

    titleTimer_ += deltaSeconds;
    if (titleTimer_ >= 0.5) {
        titleTimer_ = 0.0;
        updateTitle(app);
    }

    if (options_.selfTest) {
        runSelfTest(app);
    } else if (options_.frameLimit) {
        const std::uint64_t frame = app.frameIndex() + 1; // frame about to be rendered
        if (!options_.screenshotPath.empty() && frame == *options_.frameLimit) {
            app.graphics().requestFrameCapture();
            captureRequested_ = true;
        }
        if (frame > *options_.frameLimit) {
            app.logger().info(category, concat("Frame limit of ", *options_.frameLimit, " reached"));
            app.requestQuit();
        }
    }
}

void DemoListener::handleInput(Application& app, double deltaSeconds)
{
    const InputState& input = app.input();

    if (input.wasPressed(Key::Escape)) {
        app.requestQuit();
        return;
    }
    const bool altDown = input.isDown(Key::LeftAlt) || input.isDown(Key::RightAlt);
    if (input.wasPressed(Key::F11) || (altDown && input.wasPressed(Key::Enter))) {
        toggleFullscreen(app);
    }
    if (input.wasPressed(Key::V)) {
        toggleVSync(app);
    }
    if (input.wasPressed(Key::R) && !app.window().isFullscreen()) {
        app.window().setSize(defaultWindowSize);
    }

    // Held keys move the triangle (tests continuous "held" state).
    const float speed = 200.0f * static_cast<float>(deltaSeconds);
    if (input.isDown(Key::Left)) trianglePosition_.x -= speed;
    if (input.isDown(Key::Right)) trianglePosition_.x += speed;
    if (input.isDown(Key::Up)) trianglePosition_.y -= speed;
    if (input.isDown(Key::Down)) trianglePosition_.y += speed;

    // The sprite follows the mouse, mapped from window coordinates into the virtual resolution.
    if (input.hasMousePosition()) {
        const PresentationSettings& presentation = app.graphics().presentation();
        spritePosition_ = windowToVirtual(input.mousePosition(), app.window().size(), app.window().framebufferSize(),
                                          presentation.virtualResolution, presentation.integerScaling);
    }
    if (input.wheelDelta().y != 0.0f) {
        spriteScale_ = std::clamp(spriteScale_ * std::pow(1.1f, input.wheelDelta().y), 0.25f, 4.0f);
    }
}

void DemoListener::logInputEdges(Application& app)
{
    const InputState& input = app.input();
    for (std::size_t i = 1; i < keyCount; ++i) {
        const auto key = static_cast<Key>(i);
        const ButtonState state = input.state(key);
        if (state == ButtonState::Pressed || state == ButtonState::Released) {
            app.logger().info("Input", concat("Key ", toString(key), " ", toString(state)));
        }
    }
    for (std::size_t i = 0; i < mouseButtonCount; ++i) {
        const auto button = static_cast<MouseButton>(i);
        const ButtonState state = input.state(button);
        if (state == ButtonState::Pressed || state == ButtonState::Released) {
            const Vec2 p = input.mousePosition();
            app.logger().info("Input", concat("Mouse ", toString(button), " ", toString(state), " at (",
                                              formatFixed(static_cast<double>(p.x), 0), ", ", formatFixed(static_cast<double>(p.y), 0), ")"));
        }
    }
    if (input.wheelDelta().x != 0.0f || input.wheelDelta().y != 0.0f) {
        app.logger().info("Input", concat("Mouse wheel (", formatFixed(static_cast<double>(input.wheelDelta().x), 2), ", ",
                                          formatFixed(static_cast<double>(input.wheelDelta().y), 2), "), sprite scale ",
                                          formatFixed(static_cast<double>(spriteScale_), 2)));
    }
}

void DemoListener::onRender(Application& app, Renderer2D& renderer)
{
    (void)app;
    const Extent2D size = renderer.virtualResolution();
    const float width = static_cast<float>(size.width);
    const float height = static_cast<float>(size.height);

    renderer.clear({0.10f, 0.12f, 0.18f, 1.0f});

    // 1. Coloured triangle.
    const Vec2 t = trianglePosition_;
    renderer.drawTriangle({{
        {{t.x, t.y - 90.0f}, {1.0f, 0.15f, 0.15f, 1.0f}},
        {{t.x + 95.0f, t.y + 75.0f}, {0.15f, 1.0f, 0.2f, 1.0f}},
        {{t.x - 95.0f, t.y + 75.0f}, {0.2f, 0.35f, 1.0f, 1.0f}},
    }});

    // 2. Textured quad (opaque checkerboard), centred.
    const RectF quad{width * 0.5f - 90.0f, height * 0.5f - 90.0f, 180.0f, 180.0f};
    renderer.drawTexture(*checkerboard_, quad);

    // 3. Alpha-blended sprites: one oscillating across the quad, one following the mouse.
    const float sweep = static_cast<float>(std::sin(time_ * 1.5));
    renderer.drawTexture(*sprite_, {quad.x + quad.width * 0.5f - 48.0f + sweep * 150.0f, quad.y + 40.0f, 96.0f, 96.0f},
                         {0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 0.8f, 0.4f, 0.85f});

    Color tint = Color::white();
    if (app.input().isDown(MouseButton::Left)) tint = {1.0f, 0.4f, 0.4f, 1.0f};
    if (app.input().isDown(MouseButton::Right)) tint = {0.4f, 1.0f, 0.4f, 1.0f};
    if (app.input().isDown(MouseButton::Middle)) tint = {0.6f, 0.6f, 1.0f, 1.0f};
    const float spriteSize = 64.0f * spriteScale_;
    renderer.drawTexture(*sprite_, {spritePosition_.x - spriteSize * 0.5f, spritePosition_.y - spriteSize * 0.5f, spriteSize, spriteSize},
                         {0.0f, 0.0f, 1.0f, 1.0f}, tint);

    // Corner markers to verify orientation and the letterbox mapping: red top-left, green
    // top-right, blue bottom-left, white bottom-right.
    const float m = 12.0f;
    renderer.drawTriangle({{{{0, 0}, {1, 0, 0, 1}}, {{m * 2, 0}, {1, 0, 0, 1}}, {{0, m * 2}, {1, 0, 0, 1}}}});
    renderer.drawTriangle({{{{width, 0}, {0, 1, 0, 1}}, {{width, m * 2}, {0, 1, 0, 1}}, {{width - m * 2, 0}, {0, 1, 0, 1}}}});
    renderer.drawTriangle({{{{0, height}, {0, 0, 1, 1}}, {{0, height - m * 2}, {0, 0, 1, 1}}, {{m * 2, height}, {0, 0, 1, 1}}}});
    renderer.drawTriangle({{{{width, height}, {1, 1, 1, 1}}, {{width - m * 2, height}, {1, 1, 1, 1}}, {{width, height - m * 2}, {1, 1, 1, 1}}}});
}

void DemoListener::updateTitle(Application& app)
{
    const FrameStatistics& stats = app.clock().statistics();
    const Extent2D fb = app.window().framebufferSize();
    app.window().setTitle(concat("SCHUIMM EngineDemo - ", formatFixed(stats.framesPerSecond, 1), " FPS (",
                                 formatFixed(stats.averageDeltaSeconds * 1000.0, 2), " ms) - VSync ",
                                 app.graphics().isVSyncEnabled() ? "on" : "off", " - ", fb.width, "x", fb.height,
                                 app.window().isFullscreen() ? " fullscreen" : ""));
}

void DemoListener::toggleFullscreen(Application& app)
{
    const bool target = !app.window().isFullscreen();
    app.logger().info(category, target ? "Entering fullscreen" : "Leaving fullscreen");
    app.window().setFullscreen(target);
}

void DemoListener::toggleVSync(Application& app)
{
    const bool target = !app.graphics().isVSyncEnabled();
    const bool applied = app.graphics().setVSync(target);
    app.logger().info(category, concat("VSync ", target ? "on" : "off", applied ? "" : " (not supported by driver)"));
}

void DemoListener::onResized(Application& app, Extent2D windowSize, Extent2D framebufferSize)
{
    sawResizeEvent_ = true;
    app.logger().debug(category, concat("Resized: window ", windowSize.width, "x", windowSize.height, ", framebuffer ",
                                        framebufferSize.width, "x", framebufferSize.height));
}

void DemoListener::onFullscreenChanged(Application& app, bool fullscreen)
{
    sawFullscreenEvent_ = true;
    app.logger().info(category, fullscreen ? "Fullscreen entered" : "Fullscreen left");
}

void DemoListener::onFocusChanged(Application& app, bool focused)
{
    app.logger().info(category, focused ? "Window focused" : "Window lost focus");
}

void DemoListener::onSuspend(Application& app)
{
    app.logger().info(category, "Suspended (minimised): rendering paused");
}

void DemoListener::onResume(Application& app)
{
    app.logger().info(category, "Resumed: rendering continues");
}

void DemoListener::onShutdown(Application& app)
{
    // GPU resources must be released while the graphics device is still alive.
    sprite_.reset();
    checkerboard_.reset();
    if (options_.selfTest) {
        if (failures_.empty()) {
            app.logger().info(category, "SELF-TEST PASSED");
        } else {
            for (const std::string& failure : failures_) {
                app.logger().error(category, concat("SELF-TEST FAILED: ", failure));
            }
            exitCode_ = 1;
        }
    }
    app.logger().info(category, "Demo resources released");
}

void DemoListener::saveCapturedFrame(Application& app)
{
    std::optional<Image> frame = app.graphics().takeCapturedFrame();
    if (!frame) {
        return; // Not rendered yet (e.g. zero-sized framebuffer); try again next frame.
    }
    captureRequested_ = false;
    if (writePpm(options_.screenshotPath, *frame)) {
        app.logger().info(category, concat("Screenshot written to ", options_.screenshotPath, " (", frame->width(),
                                           "x", frame->height(), ")"));
    } else {
        app.logger().error(category, concat("Could not write screenshot to ", options_.screenshotPath));
        exitCode_ = 1;
    }
}

// --- Scripted self-test ----------------------------------------------------------------------

void DemoListener::beginStep(Application& app, SelfTestStep step)
{
    step_ = step;
    stepStartTime_ = app.clock().secondsSinceStart();
    stepStartFrame_ = app.frameIndex();
    sawFullscreenEvent_ = false;
    sawResizeEvent_ = false;

    switch (step) {
    case SelfTestStep::Resize:
        app.logger().info("SelfTest", concat("Resizing window to ", selfTestWindowSize.width, "x", selfTestWindowSize.height));
        app.window().setSize(selfTestWindowSize);
        break;
    case SelfTestStep::FullscreenOn:
        app.logger().info("SelfTest", "Entering fullscreen");
        app.window().setFullscreen(true);
        break;
    case SelfTestStep::FullscreenOff:
        app.logger().info("SelfTest", "Leaving fullscreen");
        app.window().setFullscreen(false);
        break;
    case SelfTestStep::VSyncOff: {
        const bool applied = app.graphics().setVSync(false);
        app.logger().info("SelfTest", concat("VSync off", applied ? "" : " (unsupported)"));
        break;
    }
    case SelfTestStep::VSyncOn: {
        const bool applied = app.graphics().setVSync(true);
        app.logger().info("SelfTest", concat("VSync on", applied ? "" : " (unsupported)"));
        break;
    }
    case SelfTestStep::Screenshot:
        if (!options_.screenshotPath.empty()) {
            app.graphics().requestFrameCapture();
            captureRequested_ = true;
        }
        break;
    case SelfTestStep::Start:
    case SelfTestStep::Done:
        break;
    }
}

void DemoListener::recordResult(Application& app, const std::string& name, bool passed, const std::string& detail)
{
    if (passed) {
        app.logger().info("SelfTest", concat("PASS ", name, detail.empty() ? "" : ": ", detail));
    } else {
        app.logger().error("SelfTest", concat("FAIL ", name, detail.empty() ? "" : ": ", detail));
        failures_.push_back(name + (detail.empty() ? "" : ": " + detail));
    }
}

void DemoListener::runSelfTest(Application& app)
{
    Window& window = app.window();
    const double elapsed = app.clock().secondsSinceStart() - stepStartTime_;
    const bool timedOut = elapsed > stepTimeoutSeconds;
    const auto measuredFps = [&] {
        const double frames = static_cast<double>(app.frameIndex() - stepStartFrame_);
        return elapsed > 0.0 ? frames / elapsed : 0.0;
    };

    switch (step_) {
    case SelfTestStep::Start:
        // Resizing is ignored in fullscreen, so start from a windowed state.
        if (window.isFullscreen()) {
            if (!leavingInitialFullscreen_) {
                app.logger().info("SelfTest", "Window is fullscreen; leaving fullscreen before testing");
                leavingInitialFullscreen_ = true;
                window.setFullscreen(false);
            }
            stepStartTime_ = app.clock().secondsSinceStart(); // settle time restarts after the transition
            break;
        }
        // Let a few frames run so the window is fully mapped, focused and any transition has settled.
        if (app.frameIndex() >= 30 && elapsed > 1.0) {
            const GraphicsCapabilities& caps = app.graphics().capabilities();
            const bool versionOk = caps.majorVersion > 3 || (caps.majorVersion == 3 && caps.minorVersion >= 3);
            recordResult(app, "opengl-context", versionOk && caps.coreProfile,
                         concat(caps.version, caps.coreProfile ? " (core)" : " (not core)"));
            beginStep(app, SelfTestStep::Resize);
        }
        break;

    case SelfTestStep::Resize:
        if (window.size() == selfTestWindowSize && elapsed > 0.3) {
            recordResult(app, "resize", sawResizeEvent_,
                         concat("window ", window.size().width, "x", window.size().height, ", framebuffer ",
                                window.framebufferSize().width, "x", window.framebufferSize().height,
                                sawResizeEvent_ ? "" : " (no resize event)"));
            beginStep(app, SelfTestStep::FullscreenOn);
        } else if (timedOut) {
            recordResult(app, "resize", false, concat("window stayed ", window.size().width, "x", window.size().height));
            beginStep(app, SelfTestStep::FullscreenOn);
        }
        break;

    case SelfTestStep::FullscreenOn:
        if (window.isFullscreen() && sawFullscreenEvent_ && elapsed > 0.5) {
            recordResult(app, "fullscreen-enter", true,
                         concat("framebuffer ", window.framebufferSize().width, "x", window.framebufferSize().height));
            beginStep(app, SelfTestStep::FullscreenOff);
        } else if (timedOut) {
            recordResult(app, "fullscreen-enter", false, concat("isFullscreen=", window.isFullscreen(),
                                                               ", event=", sawFullscreenEvent_));
            beginStep(app, SelfTestStep::FullscreenOff);
        }
        break;

    case SelfTestStep::FullscreenOff:
        if (!window.isFullscreen() && sawFullscreenEvent_ && elapsed > 0.5) {
            recordResult(app, "fullscreen-leave", true,
                         concat("window ", window.size().width, "x", window.size().height));
            beginStep(app, SelfTestStep::VSyncOff);
        } else if (timedOut) {
            recordResult(app, "fullscreen-leave", false, concat("isFullscreen=", window.isFullscreen(),
                                                               ", event=", sawFullscreenEvent_));
            beginStep(app, SelfTestStep::VSyncOff);
        }
        break;

    case SelfTestStep::VSyncOff:
        if (elapsed >= fpsMeasureSeconds) {
            fpsVSyncOff_ = measuredFps();
            app.logger().info("SelfTest", concat("Frame rate with VSync off: ", formatFixed(fpsVSyncOff_, 1), " FPS"));
            beginStep(app, SelfTestStep::VSyncOn);
        }
        break;

    case SelfTestStep::VSyncOn:
        if (elapsed >= fpsMeasureSeconds) {
            fpsVSyncOn_ = measuredFps();
            app.logger().info("SelfTest", concat("Frame rate with VSync on: ", formatFixed(fpsVSyncOn_, 1), " FPS"));
            const bool supported = app.graphics().capabilities().swapControl;
            if (!supported) {
                app.logger().warning("SelfTest", "Swap control not supported by this driver; VSync not verified");
            } else if (fpsVSyncOff_ > fpsVSyncOn_ * 1.2) {
                recordResult(app, "vsync", true, concat(formatFixed(fpsVSyncOff_, 1), " FPS off vs ",
                                                        formatFixed(fpsVSyncOn_, 1), " FPS on"));
            } else {
                // Compositors and drivers may force synchronisation; report but do not fail.
                app.logger().warning("SelfTest", concat("VSync toggle had no measurable effect (", formatFixed(fpsVSyncOff_, 1),
                                                       " vs ", formatFixed(fpsVSyncOn_, 1),
                                                       " FPS); the driver or compositor may override the swap interval"));
            }
            beginStep(app, SelfTestStep::Screenshot);
        }
        break;

    case SelfTestStep::Screenshot:
        if (!captureRequested_) {
            beginStep(app, SelfTestStep::Done);
        }
        break;

    case SelfTestStep::Done:
        app.requestQuit();
        break;
    }
}

} // namespace demo
