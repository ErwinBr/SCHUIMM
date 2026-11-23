#pragma once

#include "Core/Common/Error.h"
#include "Core/Logging/LogSink.h"
#include "Platform/Interface/Window.h"

#include <memory>
#include <string_view>

namespace schuimm {

// Entry point to one native windowing backend (Win32, Cocoa, X11, later Wayland).
// Create it on the main thread; it must outlive every Window it creates.
class Platform {
public:
    virtual ~Platform() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;

    [[nodiscard]] virtual Result<std::unique_ptr<Window>> createWindow(const WindowDescription& description) = 0;

    // Processes all pending native events without blocking.
    virtual void pollEvents() = 0;
    // Blocks until at least one native event arrives or the timeout elapses, then processes events.
    virtual void waitEvents(double timeoutSeconds) = 0;

    // Optional sink for the platform's native diagnostic channel
    // (OutputDebugString on Windows, unified logging on macOS). May return nullptr.
    [[nodiscard]] virtual std::unique_ptr<LogSink> createDiagnosticSink() { return nullptr; }
};

} // namespace schuimm
