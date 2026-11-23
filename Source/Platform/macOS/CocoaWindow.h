#pragma once

// Internal to the macOS backend (Objective-C++ only). Never include from engine code.

#import <Cocoa/Cocoa.h>

#include "Core/Logging/Logger.h"
#include "Platform/Interface/Window.h"
#include "Platform/Interface/WindowEventQueue.h"

@class SchuimmContentView;
@class SchuimmWindowDelegate;

namespace schuimm::cocoa {

class CocoaPlatform;

class CocoaWindow final : public Window {
public:
    CocoaWindow(CocoaPlatform& platform, Logger& logger, const WindowDescription& description);
    ~CocoaWindow() override;

    CocoaWindow(const CocoaWindow&) = delete;
    CocoaWindow& operator=(const CocoaWindow&) = delete;

    // Window
    [[nodiscard]] Extent2D size() const override;
    [[nodiscard]] Extent2D framebufferSize() const override;
    void setSize(Extent2D size) override;
    void setTitle(const std::string& title) override;
    void setFullscreen(bool fullscreen) override;
    [[nodiscard]] bool isFullscreen() const override;
    [[nodiscard]] bool isFocused() const override;
    [[nodiscard]] bool isMinimized() const override;
    void drainEvents(std::vector<WindowEvent>& out) override;
    void setLiveResizeCallback(std::function<void()> callback) override;
    [[nodiscard]] Result<std::unique_ptr<OpenGLSurface>> createOpenGLSurface(
        const OpenGLSurfaceDescription& description) override;

    // Called by the Objective-C delegate and view.
    void pushEvent(const WindowEvent& event) { events_.push(event); }
    void handleGeometryChange();
    void handleFullscreenChange(bool fullscreen);
    void handleVisibilityChange();

    // Called by CocoaOpenGLSurface.
    [[nodiscard]] NSView* contentView() const;
    void attachOpenGLContext(NSOpenGLContext* context);
    void detachOpenGLContext();

private:
    [[nodiscard]] bool queryHidden() const;

    CocoaPlatform& platform_;
    Logger& logger_;
    NSWindow* window_ = nil;
    SchuimmContentView* view_ = nil;
    SchuimmWindowDelegate* delegate_ = nil;
    __weak NSOpenGLContext* context_ = nil;
    WindowEventQueue events_;
    std::function<void()> liveResizeCallback_;
    bool hasSurface_ = false;
    bool reportedHidden_ = false;
};

} // namespace schuimm::cocoa
