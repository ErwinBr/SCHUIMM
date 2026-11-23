#pragma once

#include "Core/Application/ApplicationListener.h"
#include "Core/Common/Error.h"
#include "Core/Input/InputState.h"
#include "Core/Logging/Logger.h"
#include "Core/Timing/Clock.h"
#include "Graphics/Interface/GraphicsDevice.h"
#include "Platform/Interface/Platform.h"
#include "Platform/Interface/Window.h"

#include <vector>

namespace schuimm {

enum class LifecycleState {
    Created,   // Constructed, run() not called yet.
    Running,   // Updating and rendering every frame.
    Suspended, // Minimised (or unfocused with pauseWhenUnfocused): no update/render, low CPU.
    Stopping,  // Quit requested; the current frame finishes, then shutdown runs.
    Stopped,   // run() returned.
};

[[nodiscard]] const char* toString(LifecycleState state) noexcept;

struct ApplicationSettings {
    bool suspendWhenMinimized = true;
    bool pauseWhenUnfocused = false;
    // While suspended the loop blocks in Platform::waitEvents() for up to this long.
    double suspendedWaitSeconds = 0.1;
    ClockSettings clock;
};

// Coordinates platform, input, timing and graphics and owns the main loop:
//
//   while running:
//     platform.pollEvents()          (native event pump)
//     dispatch window events         (close, resize, focus, minimise, input)
//     input.update()                 (pressed/held/released for this frame)
//     dt = clock.tick()
//     listener.onUpdate(dt)
//     graphics.beginFrame(); listener.onRender(); graphics.endFrame(); graphics.present()
//
// All services are injected and must outlive the Application.
class Application {
public:
    struct Services {
        Logger& logger;
        Platform& platform;
        Window& window;
        GraphicsDevice& graphics;
    };

    Application(Services services, ApplicationListener& listener, ApplicationSettings settings = {},
                TimeFunction timeFunction = steadyNow);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Runs the main loop until quit. May be called once.
    Status run();

    void requestQuit();
    [[nodiscard]] bool isRunning() const noexcept
    {
        return state_ == LifecycleState::Running || state_ == LifecycleState::Suspended;
    }
    [[nodiscard]] LifecycleState state() const noexcept { return state_; }
    [[nodiscard]] std::uint64_t frameIndex() const noexcept { return frameIndex_; }

    [[nodiscard]] Logger& logger() noexcept { return services_.logger; }
    [[nodiscard]] Platform& platform() noexcept { return services_.platform; }
    [[nodiscard]] Window& window() noexcept { return services_.window; }
    [[nodiscard]] GraphicsDevice& graphics() noexcept { return services_.graphics; }
    [[nodiscard]] const InputState& input() const noexcept { return input_; }
    [[nodiscard]] const Clock& clock() const noexcept { return clock_; }

private:
    void runFrame();
    void dispatchEvents();
    void dispatch(const WindowEvent& event);
    void updateAndRender();
    void updateSuspension();
    void onLiveResize();

    Services services_;
    ApplicationListener& listener_;
    ApplicationSettings settings_;
    Clock clock_;
    InputState input_;
    LifecycleState state_ = LifecycleState::Created;
    std::vector<WindowEvent> events_;
    std::uint64_t frameIndex_ = 0;
    bool minimized_ = false;
    bool focused_ = true;
    bool pumpingEvents_ = false;
    bool inLiveResizeFrame_ = false;
};

} // namespace schuimm
