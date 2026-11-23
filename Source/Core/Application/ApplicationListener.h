#pragma once

#include "Core/Common/Error.h"
#include "Core/Common/Geometry.h"

namespace schuimm {

class Application;
class Renderer2D;

// Game/application callbacks driven by Application::run(). All callbacks run on the main thread.
class ApplicationListener {
public:
    virtual ~ApplicationListener() = default;

    // Called once before the first frame. Returning an error aborts run().
    virtual Status onInitialize(Application& application)
    {
        (void)application;
        return {};
    }

    // deltaSeconds is clamped (see ClockSettings) and 0 on the first frame after a resume.
    virtual void onUpdate(Application& application, double deltaSeconds) = 0;
    virtual void onRender(Application& application, Renderer2D& renderer) = 0;

    // Return false to veto the close request (e.g. to show a confirmation first).
    virtual bool onCloseRequested(Application& application)
    {
        (void)application;
        return true;
    }

    virtual void onFocusChanged(Application& application, bool focused)
    {
        (void)application;
        (void)focused;
    }
    virtual void onSuspend(Application& application) { (void)application; }
    virtual void onResume(Application& application) { (void)application; }
    virtual void onResized(Application& application, Extent2D windowSize, Extent2D framebufferSize)
    {
        (void)application;
        (void)windowSize;
        (void)framebufferSize;
    }
    virtual void onFullscreenChanged(Application& application, bool fullscreen)
    {
        (void)application;
        (void)fullscreen;
    }

    // Called once after the last frame, while all systems are still alive.
    virtual void onShutdown(Application& application) { (void)application; }
};

} // namespace schuimm
