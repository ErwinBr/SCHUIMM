#pragma once

// Internal to the macOS backend (Objective-C++ only). Never include from engine code.

#import <Cocoa/Cocoa.h>

#include "Core/Logging/Logger.h"
#include "Platform/Interface/Platform.h"
#include "Platform/macOS/CocoaFramePool.h"

#include <vector>

@class SchuimmApplicationDelegate;

namespace schuimm::cocoa {

class CocoaWindow;

class CocoaPlatform final : public Platform {
public:
    explicit CocoaPlatform(Logger& logger);
    ~CocoaPlatform() override;

    CocoaPlatform(const CocoaPlatform&) = delete;
    CocoaPlatform& operator=(const CocoaPlatform&) = delete;

    [[nodiscard]] std::string_view name() const override { return "Cocoa"; }
    [[nodiscard]] Result<std::unique_ptr<Window>> createWindow(const WindowDescription& description) override;
    void pollEvents() override;
    void waitEvents(double timeoutSeconds) override;
    [[nodiscard]] std::unique_ptr<LogSink> createDiagnosticSink() override;

    void registerWindow(CocoaWindow* window);
    void unregisterWindow(CocoaWindow* window);
    // Cmd+Q / "Quit" menu / logout: turned into close requests instead of terminating the process.
    void handleTerminateRequest();

private:
    void createMenuBar();

    Logger& logger_;
    SchuimmApplicationDelegate* applicationDelegate_ = nil;
    id keyUpMonitor_ = nil;
    std::vector<CocoaWindow*> windows_; // Non-owning; windows unregister themselves.
    // Declared last: created after AppKit is set up, drained first on destruction.
    FramePool framePool_;
};

} // namespace schuimm::cocoa
