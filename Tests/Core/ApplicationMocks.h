#pragma once

// Test doubles for the platform and graphics interfaces, used to exercise Application's
// lifecycle and main loop without a window system or GPU.

#include "Core/Application/Application.h"

#include <functional>
#include <vector>

namespace schuimm::test {

class MockWindow final : public Window {
public:
    Extent2D windowSize{1280, 720};
    Extent2D pixelSize{1280, 720};
    bool focused = true;
    bool minimized = false;
    bool fullscreen = false;
    std::vector<WindowEvent> pending;
    std::function<void()> liveResize;

    void push(const WindowEvent& event) { pending.push_back(event); }

    Extent2D size() const override { return windowSize; }
    Extent2D framebufferSize() const override { return pixelSize; }
    void setSize(Extent2D size) override { windowSize = pixelSize = size; }
    void setTitle(const std::string&) override {}
    void setFullscreen(bool value) override { fullscreen = value; }
    bool isFullscreen() const override { return fullscreen; }
    bool isFocused() const override { return focused; }
    bool isMinimized() const override { return minimized; }
    void drainEvents(std::vector<WindowEvent>& out) override
    {
        out.insert(out.end(), pending.begin(), pending.end());
        pending.clear();
    }
    void setLiveResizeCallback(std::function<void()> callback) override { liveResize = std::move(callback); }
    Result<std::unique_ptr<OpenGLSurface>> createOpenGLSurface(const OpenGLSurfaceDescription&) override
    {
        return makeError(ErrorCode::Unsupported, "mock window has no OpenGL");
    }
};

// Each pollEvents()/waitEvents() call runs the script with the 0-based pump index,
// which typically queues events on the MockWindow.
class MockPlatform final : public Platform {
public:
    std::function<void(int pump)> script;
    int polls = 0;
    int waits = 0;

    std::string_view name() const override { return "Mock"; }
    Result<std::unique_ptr<Window>> createWindow(const WindowDescription&) override
    {
        return std::unique_ptr<Window>(std::make_unique<MockWindow>());
    }
    void pollEvents() override
    {
        ++polls;
        pump();
    }
    void waitEvents(double) override
    {
        ++waits;
        pump();
    }

private:
    void pump()
    {
        if (script) {
            script(pumpIndex_);
        }
        ++pumpIndex_;
    }
    int pumpIndex_ = 0;
};

class MockTexture final : public Texture {
public:
    Extent2D size() const override { return {1, 1}; }
};

class MockRenderer final : public Renderer2D {
public:
    int clears = 0;
    Extent2D virtualResolution() const override { return {640, 360}; }
    void clear(Color) override { ++clears; }
    void drawTriangle(const std::array<ColoredVertex, 3>&) override {}
    void drawTexture(const Texture&, const RectF&, const RectF&, Color) override {}
    Result<std::unique_ptr<Texture>> createTexture(const TextureDescription&) override
    {
        return std::unique_ptr<Texture>(std::make_unique<MockTexture>());
    }
};

class MockGraphicsDevice final : public GraphicsDevice {
public:
    MockRenderer mockRenderer;
    GraphicsCapabilities caps;
    PresentationSettings settings;
    int beginFrames = 0;
    int endFrames = 0;
    int presents = 0;
    bool vsync = true;

    const GraphicsCapabilities& capabilities() const override { return caps; }
    Renderer2D& renderer() override { return mockRenderer; }
    void beginFrame() override { ++beginFrames; }
    void endFrame() override { ++endFrames; }
    void present() override { ++presents; }
    bool setVSync(bool enabled) override
    {
        vsync = enabled;
        return true;
    }
    bool isVSyncEnabled() const override { return vsync; }
    const PresentationSettings& presentation() const override { return settings; }
    void requestFrameCapture() override {}
    std::optional<Image> takeCapturedFrame() override { return std::nullopt; }
};

// Records every callback; optional hooks customise behaviour per test.
class RecordingListener final : public ApplicationListener {
public:
    int initializes = 0;
    int updates = 0;
    int renders = 0;
    int shutdowns = 0;
    int closeRequests = 0;
    int suspends = 0;
    int resumes = 0;
    int resizes = 0;
    std::vector<bool> focusChanges;
    std::vector<double> deltas;
    std::vector<LifecycleState> statesDuringUpdate;

    Status initResult{};
    int quitAfterUpdates = -1;    // requestQuit() once this many updates happened
    int vetoCloseRequests = 0;    // number of close requests to refuse
    std::function<void(Application&)> onEachUpdate;

    Status onInitialize(Application&) override
    {
        ++initializes;
        return initResult;
    }
    void onUpdate(Application& app, double deltaSeconds) override
    {
        ++updates;
        deltas.push_back(deltaSeconds);
        statesDuringUpdate.push_back(app.state());
        if (onEachUpdate) {
            onEachUpdate(app);
        }
        if (quitAfterUpdates >= 0 && updates >= quitAfterUpdates) {
            app.requestQuit();
        }
    }
    void onRender(Application&, Renderer2D& renderer) override
    {
        ++renders;
        renderer.clear(Color::black());
    }
    bool onCloseRequested(Application&) override
    {
        ++closeRequests;
        if (vetoCloseRequests > 0) {
            --vetoCloseRequests;
            return false;
        }
        return true;
    }
    void onFocusChanged(Application&, bool focused) override { focusChanges.push_back(focused); }
    void onSuspend(Application&) override { ++suspends; }
    void onResume(Application&) override { ++resumes; }
    void onResized(Application&, Extent2D, Extent2D) override { ++resizes; }
    void onShutdown(Application&) override { ++shutdowns; }
};

// Logger, mocks and a manually advanced clock wired together.
struct ApplicationFixture {
    Logger logger{LogLevel::Off};
    MockPlatform platform;
    MockWindow window;
    MockGraphicsDevice graphics;
    RecordingListener listener;
    std::chrono::nanoseconds now{std::chrono::seconds(1)};
    ApplicationSettings settings;

    // Every pump advances time by one 60 Hz frame unless the script changes `now`.
    std::unique_ptr<Application> makeApplication()
    {
        auto userScript = platform.script;
        platform.script = [this, userScript](int pump) {
            now += std::chrono::microseconds(16'667);
            if (userScript) {
                userScript(pump);
            }
        };
        return std::make_unique<Application>(Application::Services{logger, platform, window, graphics}, listener,
                                             settings, [this] { return now; });
    }
};

} // namespace schuimm::test
