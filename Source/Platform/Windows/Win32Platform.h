#pragma once

// Internal to the Win32 backend.

#include "Core/Logging/Logger.h"
#include "Platform/Interface/Platform.h"
#include "Platform/Windows/Win32Headers.h"

#include <vector>

namespace schuimm::win32 {

// Entry points newer than Windows 7, resolved at run time.
struct DynamicApis {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    GetDpiForWindowFn getDpiForWindow = nullptr;
};

class Win32Platform final : public Platform {
public:
    explicit Win32Platform(Logger& logger);
    ~Win32Platform() override;

    Win32Platform(const Win32Platform&) = delete;
    Win32Platform& operator=(const Win32Platform&) = delete;

    [[nodiscard]] std::string_view name() const override { return "Win32"; }
    [[nodiscard]] Result<std::unique_ptr<Window>> createWindow(const WindowDescription& description) override;
    void pollEvents() override;
    void waitEvents(double timeoutSeconds) override;
    [[nodiscard]] std::unique_ptr<LogSink> createDiagnosticSink() override;

    [[nodiscard]] Status initialise();
    [[nodiscard]] HINSTANCE instance() const noexcept { return instance_; }
    [[nodiscard]] const wchar_t* windowClassName() const noexcept { return L"SchuimmWindow"; }
    [[nodiscard]] const DynamicApis& apis() const noexcept { return apis_; }

private:
    void enableDpiAwareness();

    Logger& logger_;
    HINSTANCE instance_ = nullptr;
    ATOM windowClass_ = 0;
    DynamicApis apis_;
};

} // namespace schuimm::win32
