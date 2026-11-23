#include "Platform/Windows/Win32Window.h"

#include "Core/Common/Assert.h"
#include "Core/Common/StringBuilder.h"
#include "Platform/Windows/Win32KeyMap.h"
#include "Platform/Windows/Win32OpenGLSurface.h"
#include "Platform/Windows/Win32Platform.h"

#include <cmath>

namespace schuimm::win32 {

namespace {
constexpr UINT_PTR modalLoopTimer = 1;
constexpr DWORD windowedStyleResizable = WS_OVERLAPPEDWINDOW;
constexpr DWORD windowedStyleFixed = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
constexpr DWORD fullscreenStyle = WS_POPUP;
constexpr DWORD extendedStyle = WS_EX_APPWINDOW;
} // namespace

Result<std::unique_ptr<Window>> Win32Window::create(Win32Platform& platform, Logger& logger,
                                                    const WindowDescription& description)
{
    auto window = std::make_unique<Win32Window>(platform, logger, description);
    if (auto status = window->createNativeWindow(description); !status) {
        return std::unexpected(status.error());
    }
    return std::unique_ptr<Window>(std::move(window));
}

Win32Window::Win32Window(Win32Platform& platform, Logger& logger, const WindowDescription& description)
    : platform_(platform)
    , logger_(logger)
    , resizable_(description.resizable)
{
    windowedPlacement_.length = sizeof(windowedPlacement_);
}

DWORD Win32Window::windowedStyle() const noexcept
{
    return resizable_ ? windowedStyleResizable : windowedStyleFixed;
}

Status Win32Window::createNativeWindow(const WindowDescription& description)
{
    // Create hidden at the requested size, then correct for DPI once the monitor is known.
    RECT rect{0, 0, description.size.width, description.size.height};
    AdjustWindowRectEx(&rect, windowedStyle(), FALSE, extendedStyle);
    const std::wstring title = toWide(description.title);
    handle_ = CreateWindowExW(extendedStyle, platform_.windowClassName(), title.c_str(),
                              windowedStyle() | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CW_USEDEFAULT, CW_USEDEFAULT,
                              rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, platform_.instance(), this);
    if (handle_ == nullptr) {
        return makeError(ErrorCode::PlatformFailure, "CreateWindowExW failed: " + lastErrorMessage());
    }
    deviceContext_ = GetDC(handle_);

    updateDpiScale();
    if (dpiScale_ != 1.0f) {
        setSize(description.size); // Re-apply in logical units for the actual DPI.
    }
    updateClientSize();

    ShowWindow(handle_, SW_SHOWNORMAL);
    SetForegroundWindow(handle_);
    SetFocus(handle_);
    focused_ = GetForegroundWindow() == handle_;

    logger_.info("Platform", concat("Created Win32 window ", size().width, "x", size().height, " (framebuffer ",
                                    clientSize_.width, "x", clientSize_.height, ", DPI scale ",
                                    formatFixed(static_cast<double>(dpiScale_), 2), ")"));
    if (description.fullscreen) {
        setFullscreen(true);
    }
    return {};
}

Win32Window::~Win32Window()
{
    SCHUIMM_VERIFY(!hasSurface_, "the OpenGL surface must be destroyed before its window");
    if (handle_ != nullptr) {
        SetWindowLongPtrW(handle_, GWLP_USERDATA, 0);
        if (deviceContext_ != nullptr) {
            ReleaseDC(handle_, deviceContext_);
        }
        DestroyWindow(handle_);
    }
}

Extent2D Win32Window::size() const
{
    return {static_cast<int>(std::lround(static_cast<float>(clientSize_.width) / dpiScale_)),
            static_cast<int>(std::lround(static_cast<float>(clientSize_.height) / dpiScale_))};
}

void Win32Window::setSize(Extent2D size)
{
    if (size.isEmpty() || fullscreen_) {
        return;
    }
    RECT rect{0, 0, static_cast<LONG>(std::lround(static_cast<float>(size.width) * dpiScale_)),
              static_cast<LONG>(std::lround(static_cast<float>(size.height) * dpiScale_))};
    AdjustWindowRectEx(&rect, windowedStyle(), FALSE, extendedStyle);
    SetWindowPos(handle_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Win32Window::setTitle(const std::string& title)
{
    SetWindowTextW(handle_, toWide(title).c_str());
}

void Win32Window::setFullscreen(bool fullscreen)
{
    if (fullscreen == fullscreen_) {
        return;
    }
    if (fullscreen) {
        // Borderless window covering the monitor; no display mode change.
        GetWindowPlacement(handle_, &windowedPlacement_);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromWindow(handle_, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowLongPtrW(handle_, GWL_STYLE, static_cast<LONG_PTR>(fullscreenStyle | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS));
        fullscreen_ = true;
        SetWindowPos(handle_, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                     monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        SetWindowLongPtrW(handle_, GWL_STYLE, static_cast<LONG_PTR>(windowedStyle() | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS));
        fullscreen_ = false;
        SetWindowPlacement(handle_, &windowedPlacement_);
        SetWindowPos(handle_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    updateClientSize();
    events_.push(FullscreenChangedEvent{fullscreen_});
}

void Win32Window::drainEvents(std::vector<WindowEvent>& out)
{
    events_.drainInto(out);
}

void Win32Window::setLiveResizeCallback(std::function<void()> callback)
{
    liveResizeCallback_ = std::move(callback);
}

Result<std::unique_ptr<OpenGLSurface>> Win32Window::createOpenGLSurface(const OpenGLSurfaceDescription& description)
{
    if (hasSurface_) {
        return makeError(ErrorCode::InvalidState, "window already has an OpenGL surface");
    }
    return Win32OpenGLSurface::create(platform_, *this, logger_, description);
}

void Win32Window::updateClientSize()
{
    RECT client{};
    GetClientRect(handle_, &client);
    const Extent2D newSize{client.right - client.left, client.bottom - client.top};
    if (newSize != clientSize_) {
        clientSize_ = newSize;
        if (!newSize.isEmpty()) {
            events_.push(WindowResizedEvent{size(), clientSize_});
        }
    }
}

void Win32Window::updateDpiScale()
{
    UINT dpi = 96;
    if (platform_.apis().getDpiForWindow != nullptr) {
        dpi = platform_.apis().getDpiForWindow(handle_);
    } else if (deviceContext_ != nullptr) {
        dpi = static_cast<UINT>(GetDeviceCaps(deviceContext_, LOGPIXELSX));
    }
    dpiScale_ = dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
}

Vec2 Win32Window::logicalPosition(LPARAM lParam) const noexcept
{
    return {static_cast<float>(GET_X_LPARAM(lParam)) / dpiScale_, static_cast<float>(GET_Y_LPARAM(lParam)) / dpiScale_};
}

void Win32Window::handleKey(WPARAM wParam, LPARAM lParam, bool down)
{
    (void)wParam;
    const auto scanCode = static_cast<std::uint32_t>((lParam >> 16) & 0xFF);
    const bool extended = ((lParam >> 24) & 1) != 0;
    const bool repeat = down && ((lParam >> 30) & 1) != 0;
    const Key key = translateScanCode(scanCode, extended);
    events_.push(KeyEvent{key, down, repeat});

    // Windows sends no key-up for the first Shift when both are held and one is released;
    // when a Shift key goes up, synthesise releases for whichever Shift is no longer down.
    if (!down && (key == Key::LeftShift || key == Key::RightShift)) {
        if ((GetKeyState(VK_LSHIFT) & 0x8000) == 0) {
            events_.push(KeyEvent{Key::LeftShift, false, false});
        }
        if ((GetKeyState(VK_RSHIFT) & 0x8000) == 0) {
            events_.push(KeyEvent{Key::RightShift, false, false});
        }
    }
}

void Win32Window::handleMouseButton(MouseButton button, bool down, LPARAM lParam)
{
    // Capture the mouse while any button is down so the release is seen even outside the window.
    if (down) {
        if (buttonsDown_++ == 0) {
            SetCapture(handle_);
        }
    } else if (buttonsDown_ > 0 && --buttonsDown_ == 0) {
        ReleaseCapture();
    }
    events_.push(MouseButtonEvent{button, down, logicalPosition(lParam)});
}

LRESULT CALLBACK Win32Window::windowProcedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        // The WGL bootstrap window shares this class but has no owner (lpCreateParams == nullptr).
        if (auto* owner = static_cast<Win32Window*>(create->lpCreateParams)) {
            owner->handle_ = handle;
        }
    }
    auto* window = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
    if (window == nullptr) {
        return DefWindowProcW(handle, message, wParam, lParam);
    }
    return window->handleMessage(message, wParam, lParam);
}

LRESULT Win32Window::handleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CLOSE:
        // The application decides; the window is destroyed by the engine, not by DefWindowProc.
        events_.push(CloseRequestedEvent{});
        return 0;

    case WM_SIZE: {
        const bool minimized = wParam == SIZE_MINIMIZED;
        if (minimized != minimized_) {
            minimized_ = minimized;
            events_.push(MinimizedChangedEvent{minimized_});
        }
        if (!minimized_) {
            updateClientSize();
            if (inSizeMove_ && liveResizeCallback_) {
                liveResizeCallback_();
            }
        }
        return 0;
    }

    case WM_ENTERSIZEMOVE:
        // DefWindowProc now runs a modal loop until the drag ends; keep the application
        // rendering through a timer (and WM_SIZE) while it is active.
        inSizeMove_ = true;
        SetTimer(handle_, modalLoopTimer, USER_TIMER_MINIMUM, nullptr);
        return 0;

    case WM_EXITSIZEMOVE:
        inSizeMove_ = false;
        KillTimer(handle_, modalLoopTimer);
        return 0;

    case WM_TIMER:
        if (wParam == modalLoopTimer && inSizeMove_ && liveResizeCallback_) {
            liveResizeCallback_();
        }
        return 0;

    case WM_DPICHANGED: {
        updateDpiScale();
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(handle_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        updateClientSize();
        return 0;
    }

    case WM_SETFOCUS:
        if (!focused_) {
            focused_ = true;
            events_.push(FocusChangedEvent{true});
        }
        return 0;

    case WM_KILLFOCUS:
        if (focused_) {
            focused_ = false;
            events_.push(FocusChangedEvent{false});
        }
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        handleKey(wParam, lParam, true);
        if (message == WM_SYSKEYDOWN && wParam == VK_F4) {
            break; // Let DefWindowProc turn Alt+F4 into WM_CLOSE.
        }
        return 0;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        handleKey(wParam, lParam, false);
        return 0;

    case WM_SYSCHAR:
        return 0; // Suppress the "unhandled Alt+key" beep.

    case WM_SYSCOMMAND:
        // Pressing Alt alone would enter the window menu's modal loop and freeze the game.
        if ((wParam & 0xFFF0) == SC_KEYMENU) {
            return 0;
        }
        break;

    case WM_LBUTTONDOWN: handleMouseButton(MouseButton::Left, true, lParam); return 0;
    case WM_LBUTTONUP: handleMouseButton(MouseButton::Left, false, lParam); return 0;
    case WM_RBUTTONDOWN: handleMouseButton(MouseButton::Right, true, lParam); return 0;
    case WM_RBUTTONUP: handleMouseButton(MouseButton::Right, false, lParam); return 0;
    case WM_MBUTTONDOWN: handleMouseButton(MouseButton::Middle, true, lParam); return 0;
    case WM_MBUTTONUP: handleMouseButton(MouseButton::Middle, false, lParam); return 0;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        handleMouseButton(GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? MouseButton::Extra1 : MouseButton::Extra2,
                          message == WM_XBUTTONDOWN, lParam);
        return TRUE;

    case WM_CAPTURECHANGED:
        buttonsDown_ = 0;
        return 0;

    case WM_MOUSEMOVE:
        events_.push(MouseMovedEvent{logicalPosition(lParam)});
        return 0;

    case WM_MOUSEWHEEL:
        events_.push(MouseWheelEvent{{0.0f, static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA}});
        return 0;

    case WM_MOUSEHWHEEL:
        events_.push(MouseWheelEvent{{static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA, 0.0f}});
        return 0;

    case WM_ERASEBKGND:
        return 1; // OpenGL covers the client area; avoid flicker.

    default:
        break;
    }
    return DefWindowProcW(handle_, message, wParam, lParam);
}

} // namespace schuimm::win32
