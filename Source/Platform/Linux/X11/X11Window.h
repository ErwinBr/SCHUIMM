#pragma once

// Internal to the X11 backend.

#include "Core/Logging/Logger.h"
#include "Platform/Interface/Window.h"
#include "Platform/Interface/WindowEventQueue.h"

#include <bitset>

#include "Platform/Linux/X11/X11Headers.h"

namespace schuimm::x11 {

class X11Platform;

class X11Window final : public schuimm::Window {
public:
    X11Window(X11Platform& platform, Logger& logger, ::Window handle, Colormap colormap, const WindowDescription& description);
    ~X11Window() override;

    X11Window(const X11Window&) = delete;
    X11Window& operator=(const X11Window&) = delete;

    [[nodiscard]] Extent2D size() const override { return size_; }
    [[nodiscard]] Extent2D framebufferSize() const override { return size_; } // X11 works in pixels
    void setSize(Extent2D size) override;
    void setTitle(const std::string& title) override;
    void setFullscreen(bool fullscreen) override;
    [[nodiscard]] bool isFullscreen() const override { return fullscreen_; }
    [[nodiscard]] bool isFocused() const override { return focused_; }
    [[nodiscard]] bool isMinimized() const override { return minimized_; }
    void drainEvents(std::vector<WindowEvent>& out) override;
    void setLiveResizeCallback(std::function<void()> callback) override;
    [[nodiscard]] Result<std::unique_ptr<OpenGLSurface>> createOpenGLSurface(
        const OpenGLSurfaceDescription& description) override;

    [[nodiscard]] ::Window handle() const noexcept { return handle_; }
    void handleEvent(const XEvent& event);
    void setSurfaceAttached(bool attached) noexcept { hasSurface_ = attached; }

private:
    void readWindowState();
    void setMinimized(bool minimized);
    [[nodiscard]] Vec2 position(int x, int y) const noexcept;

    X11Platform& platform_;
    Logger& logger_;
    ::Window handle_ = 0;
    Colormap colormap_ = 0;
    Extent2D size_;
    bool resizable_ = true;
    bool fullscreen_ = false;
    bool focused_ = false;
    bool minimized_ = false;
    bool hasSurface_ = false;
    std::bitset<256> keysDown_; // For marking auto-repeat KeyPress events.
    WindowEventQueue events_;
};

} // namespace schuimm::x11
