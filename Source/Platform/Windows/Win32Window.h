#pragma once

// Internal to the Win32 backend.

#include "Core/Logging/Logger.h"
#include "Platform/Interface/Window.h"
#include "Platform/Interface/WindowEventQueue.h"
#include "Platform/Windows/Win32Headers.h"

namespace schuimm::win32 {

class Win32Platform;

// Window sizes are logical (96-DPI units, like points on macOS): size() is the client size
// divided by the DPI scale, framebufferSize() is the client size in physical pixels.
class Win32Window final : public Window {
public:
    static Result<std::unique_ptr<Window>> create(Win32Platform& platform, Logger& logger,
                                                  const WindowDescription& description);

    Win32Window(Win32Platform& platform, Logger& logger, const WindowDescription& description);
    ~Win32Window() override;

    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;

    [[nodiscard]] Extent2D size() const override;
    [[nodiscard]] Extent2D framebufferSize() const override { return clientSize_; }
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

    [[nodiscard]] HWND handle() const noexcept { return handle_; }
    [[nodiscard]] HDC deviceContext() const noexcept { return deviceContext_; }
    void setSurfaceAttached(bool attached) noexcept { hasSurface_ = attached; }

    static LRESULT CALLBACK windowProcedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);

private:
    [[nodiscard]] Status createNativeWindow(const WindowDescription& description);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void handleKey(WPARAM wParam, LPARAM lParam, bool down);
    void handleMouseButton(MouseButton button, bool down, LPARAM lParam);
    void updateClientSize();
    void updateDpiScale();
    [[nodiscard]] Vec2 logicalPosition(LPARAM lParam) const noexcept;
    [[nodiscard]] DWORD windowedStyle() const noexcept;

    Win32Platform& platform_;
    Logger& logger_;
    HWND handle_ = nullptr;
    HDC deviceContext_ = nullptr;
    Extent2D clientSize_;
    float dpiScale_ = 1.0f;
    bool resizable_ = true;
    bool fullscreen_ = false;
    bool focused_ = false;
    bool minimized_ = false;
    bool inSizeMove_ = false;
    bool hasSurface_ = false;
    int buttonsDown_ = 0;
    WINDOWPLACEMENT windowedPlacement_{};
    WindowEventQueue events_;
    std::function<void()> liveResizeCallback_;
};

} // namespace schuimm::win32
