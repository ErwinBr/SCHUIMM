#include "Core/Application/Application.h"

#include "Core/Common/Assert.h"
#include "Core/Common/StringBuilder.h"

#include <type_traits>

namespace schuimm {

const char* toString(LifecycleState state) noexcept
{
    switch (state) {
    case LifecycleState::Created: return "Created";
    case LifecycleState::Running: return "Running";
    case LifecycleState::Suspended: return "Suspended";
    case LifecycleState::Stopping: return "Stopping";
    case LifecycleState::Stopped: return "Stopped";
    }
    return "Unknown";
}

Application::Application(Services services, ApplicationListener& listener, ApplicationSettings settings,
                         TimeFunction timeFunction)
    : services_(services)
    , listener_(listener)
    , settings_(settings)
    , clock_(std::move(timeFunction), settings.clock)
{
}

Application::~Application()
{
    services_.window.setLiveResizeCallback({});
}

Status Application::run()
{
    if (state_ != LifecycleState::Created) {
        return makeError(ErrorCode::InvalidState, "Application::run() may only be called once");
    }

    Logger& log = services_.logger;
    log.info("Application", "Initialising");
    if (auto status = listener_.onInitialize(*this); !status) {
        log.error("Application", concat("Initialisation failed: ", describe(status.error())));
        state_ = LifecycleState::Stopped;
        return status;
    }

    focused_ = services_.window.isFocused();
    minimized_ = services_.window.isMinimized();
    services_.window.setLiveResizeCallback([this] { onLiveResize(); });

    state_ = LifecycleState::Running;
    log.info("Application", "Running");
    updateSuspension();
    clock_.resetDelta();

    while (isRunning()) {
        runFrame();
    }

    log.info("Application", "Shutting down");
    services_.window.setLiveResizeCallback({});
    listener_.onShutdown(*this);
    state_ = LifecycleState::Stopped;
    log.info("Application", concat("Stopped after ", frameIndex_, " frames"));
    return {};
}

void Application::requestQuit()
{
    if (isRunning()) {
        services_.logger.info("Application", "Quit requested");
        state_ = LifecycleState::Stopping;
    }
}

void Application::runFrame()
{
    pumpingEvents_ = true;
    if (state_ == LifecycleState::Suspended) {
        // Nothing to draw: block instead of spinning, but wake up regularly.
        services_.platform.waitEvents(settings_.suspendedWaitSeconds);
    } else {
        services_.platform.pollEvents();
    }
    pumpingEvents_ = false;

    dispatchEvents();
    if (state_ == LifecycleState::Running) {
        updateAndRender();
    }
}

void Application::dispatchEvents()
{
    events_.clear();
    services_.window.drainEvents(events_);
    for (const WindowEvent& event : events_) {
        dispatch(event);
    }
    events_.clear();
}

void Application::dispatch(const WindowEvent& event)
{
    std::visit(
        [this](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, CloseRequestedEvent>) {
                services_.logger.info("Application", "Close requested");
                if (isRunning() && listener_.onCloseRequested(*this)) {
                    requestQuit();
                }
            } else if constexpr (std::is_same_v<T, WindowResizedEvent>) {
                listener_.onResized(*this, e.windowSize, e.framebufferSize);
            } else if constexpr (std::is_same_v<T, FocusChangedEvent>) {
                if (focused_ != e.focused) {
                    focused_ = e.focused;
                    if (!focused_) {
                        // Key-up events for keys held while focus leaves are never delivered.
                        input_.releaseAll();
                    }
                    services_.logger.debug("Application", focused_ ? "Focus gained" : "Focus lost");
                    listener_.onFocusChanged(*this, focused_);
                    updateSuspension();
                }
            } else if constexpr (std::is_same_v<T, MinimizedChangedEvent>) {
                if (minimized_ != e.minimized) {
                    minimized_ = e.minimized;
                    updateSuspension();
                }
            } else if constexpr (std::is_same_v<T, FullscreenChangedEvent>) {
                listener_.onFullscreenChanged(*this, e.fullscreen);
            } else if constexpr (std::is_same_v<T, KeyEvent>) {
                if (e.down) {
                    input_.keyDown(e.key);
                } else {
                    input_.keyUp(e.key);
                }
            } else if constexpr (std::is_same_v<T, MouseButtonEvent>) {
                input_.mouseMoved(e.position);
                if (e.down) {
                    input_.mouseButtonDown(e.button);
                } else {
                    input_.mouseButtonUp(e.button);
                }
            } else if constexpr (std::is_same_v<T, MouseMovedEvent>) {
                input_.mouseMoved(e.position);
            } else if constexpr (std::is_same_v<T, MouseWheelEvent>) {
                input_.mouseWheel(e.delta);
            }
        },
        event);
}

void Application::updateSuspension()
{
    if (!isRunning()) {
        return;
    }
    const bool shouldSuspend = (settings_.suspendWhenMinimized && minimized_) ||
                               (settings_.pauseWhenUnfocused && !focused_);
    if (shouldSuspend && state_ == LifecycleState::Running) {
        state_ = LifecycleState::Suspended;
        services_.logger.info("Application", "Suspended");
        listener_.onSuspend(*this);
    } else if (!shouldSuspend && state_ == LifecycleState::Suspended) {
        state_ = LifecycleState::Running;
        // The time spent suspended must not show up as one huge frame delta.
        clock_.resetDelta();
        services_.logger.info("Application", "Resumed");
        listener_.onResume(*this);
    }
}

void Application::updateAndRender()
{
    input_.update();
    const double deltaSeconds = clock_.tick();
    listener_.onUpdate(*this, deltaSeconds);
    if (state_ != LifecycleState::Running) {
        return; // The listener requested quit (or the state changed) during update.
    }

    // A zero-sized framebuffer (e.g. some window managers while minimising) cannot be drawn to.
    if (!services_.window.framebufferSize().isEmpty()) {
        GraphicsDevice& graphics = services_.graphics;
        graphics.beginFrame();
        listener_.onRender(*this, graphics.renderer());
        graphics.endFrame();
        graphics.present();
    }
    ++frameIndex_;
}

void Application::onLiveResize()
{
    // Invoked from inside the platform's modal resize loop (Win32/Cocoa), i.e. while
    // pollEvents() is executing. Process what has been queued so far and draw one frame so
    // the window content follows the drag. Re-entrancy is excluded explicitly.
    if (!pumpingEvents_ || inLiveResizeFrame_ || state_ != LifecycleState::Running) {
        return;
    }
    inLiveResizeFrame_ = true;
    dispatchEvents();
    if (state_ == LifecycleState::Running) {
        updateAndRender();
    }
    inLiveResizeFrame_ = false;
}

} // namespace schuimm
