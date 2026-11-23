#include "Platform/Windows/Win32Platform.h"

#include "Core/Common/StringBuilder.h"
#include "Platform/NativePlatform.h"
#include "Platform/Windows/Win32Window.h"

#include <algorithm>
#include <cmath>

namespace schuimm::win32 {

std::wstring toWide(std::string_view utf8)
{
    if (utf8.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), length);
    return result;
}

std::string lastErrorMessage(DWORD error)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::string message = concat("error ", error);
    if (length > 0 && buffer != nullptr) {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
        std::string text(static_cast<std::size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), text.data(), bytes, nullptr, nullptr);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
            text.pop_back();
        }
        message += ": " + text;
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    return message;
}

namespace {

// Mirrors log output to the debugger (Visual Studio Output window, DebugView).
class DebugOutputSink final : public LogSink {
public:
    void write(const LogRecord& record, std::string_view formattedLine) override
    {
        (void)record;
        std::wstring line = toWide(formattedLine);
        line += L"\n";
        OutputDebugStringW(line.c_str());
    }
};

template <typename Function>
Function loadFunction(const wchar_t* module, const char* name)
{
    HMODULE handle = GetModuleHandleW(module);
    if (handle == nullptr) {
        handle = LoadLibraryW(module); // Intentionally kept loaded for the process lifetime.
    }
    return handle != nullptr ? reinterpret_cast<Function>(reinterpret_cast<void*>(GetProcAddress(handle, name))) : nullptr;
}

} // namespace

Win32Platform::Win32Platform(Logger& logger)
    : logger_(logger)
    , instance_(GetModuleHandleW(nullptr))
{
}

Win32Platform::~Win32Platform()
{
    if (windowClass_ != 0) {
        UnregisterClassW(windowClassName(), instance_);
    }
}

Status Win32Platform::initialise()
{
    enableDpiAwareness();
    apis_.getDpiForWindow = loadFunction<DynamicApis::GetDpiForWindowFn>(L"user32.dll", "GetDpiForWindow");

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    // CS_OWNDC gives every window a private device context, which OpenGL requires.
    windowClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &Win32Window::windowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    windowClass.hIcon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));
    windowClass.lpszClassName = windowClassName();
    windowClass_ = RegisterClassExW(&windowClass);
    if (windowClass_ == 0) {
        return makeError(ErrorCode::PlatformFailure, "RegisterClassExW failed: " + lastErrorMessage());
    }
    logger_.info("Platform", "Win32 platform initialised");
    return {};
}

void Win32Platform::enableDpiAwareness()
{
    // Per-monitor v2 (Windows 10 1703+), then per-monitor (8.1+), then system-aware (Vista+).
    using SetContextFn = BOOL(WINAPI*)(HANDLE);
    using SetAwarenessFn = HRESULT(WINAPI*)(int);
    using SetAwareFn = BOOL(WINAPI*)();
    if (auto setContext = loadFunction<SetContextFn>(L"user32.dll", "SetProcessDpiAwarenessContext")) {
        if (setContext(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4)))) { // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
            logger_.debug("Platform", "DPI awareness: per-monitor v2");
            return;
        }
    }
    if (auto setAwareness = loadFunction<SetAwarenessFn>(L"shcore.dll", "SetProcessDpiAwareness")) {
        if (SUCCEEDED(setAwareness(2))) { // PROCESS_PER_MONITOR_DPI_AWARE
            logger_.debug("Platform", "DPI awareness: per-monitor");
            return;
        }
    }
    if (auto setAware = loadFunction<SetAwareFn>(L"user32.dll", "SetProcessDPIAware")) {
        setAware();
        logger_.debug("Platform", "DPI awareness: system");
    }
}

Result<std::unique_ptr<Window>> Win32Platform::createWindow(const WindowDescription& description)
{
    if (description.size.isEmpty()) {
        return makeError(ErrorCode::InvalidArgument, "window size must be positive");
    }
    return Win32Window::create(*this, logger_, description);
}

void Win32Platform::pollEvents()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            continue; // The engine never posts WM_QUIT; shutdown goes through close requests.
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void Win32Platform::waitEvents(double timeoutSeconds)
{
    const DWORD milliseconds = static_cast<DWORD>(std::ceil(std::max(0.0, timeoutSeconds) * 1000.0));
    MsgWaitForMultipleObjects(0, nullptr, FALSE, milliseconds, QS_ALLINPUT);
    pollEvents();
}

std::unique_ptr<LogSink> Win32Platform::createDiagnosticSink()
{
    return std::make_unique<DebugOutputSink>();
}

} // namespace schuimm::win32

namespace schuimm {

Result<std::unique_ptr<Platform>> createNativePlatform(Logger& logger)
{
    auto platform = std::make_unique<win32::Win32Platform>(logger);
    if (auto status = platform->initialise(); !status) {
        return std::unexpected(status.error());
    }
    return std::unique_ptr<Platform>(std::move(platform));
}

} // namespace schuimm
