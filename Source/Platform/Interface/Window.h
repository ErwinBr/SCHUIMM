#pragma once

#include "Core/Common/Error.h"
#include "Core/Common/Geometry.h"
#include "Platform/Interface/OpenGLSurface.h"
#include "Platform/Interface/WindowEvent.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace schuimm {

struct WindowDescription {
    std::string title = "Schuimm";
    Extent2D size{1280, 720}; // Logical size of the client area.
    bool resizable = true;
    bool fullscreen = false;
};

class Window {
public:
    virtual ~Window() = default;

    [[nodiscard]] virtual Extent2D size() const = 0;            // Logical client size.
    [[nodiscard]] virtual Extent2D framebufferSize() const = 0; // Pixels.
    virtual void setSize(Extent2D size) = 0;
    virtual void setTitle(const std::string& title) = 0;

    // Borderless desktop-resolution fullscreen (no display mode change). On macOS this uses the
    // native fullscreen space and completes asynchronously; FullscreenChangedEvent reports the result.
    virtual void setFullscreen(bool fullscreen) = 0;
    [[nodiscard]] virtual bool isFullscreen() const = 0;
    [[nodiscard]] virtual bool isFocused() const = 0;
    [[nodiscard]] virtual bool isMinimized() const = 0;

    // Appends all events queued since the last call.
    virtual void drainEvents(std::vector<WindowEvent>& out) = 0;

    // Windows and macOS run a modal loop while the user drags the window border, during which
    // Platform::pollEvents() does not return. The callback is invoked from inside that loop
    // whenever the size changes so the application can keep drawing. Pass {} to clear.
    virtual void setLiveResizeCallback(std::function<void()> callback) = 0;

    [[nodiscard]] virtual Result<std::unique_ptr<OpenGLSurface>> createOpenGLSurface(
        const OpenGLSurfaceDescription& description) = 0;
};

} // namespace schuimm
