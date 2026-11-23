#pragma once

// Internal to the X11 backend.

#include "Core/Input/InputTypes.h"
#include "Core/Logging/Logger.h"
#include "Platform/Interface/Platform.h"

#include <array>
#include <vector>

#include "Platform/Linux/X11/X11Headers.h"

namespace schuimm::x11 {

class X11Window;

struct Atoms {
    Atom wmProtocols = 0;
    Atom wmDeleteWindow = 0;
    Atom wmState = 0;
    Atom netWmState = 0;
    Atom netWmStateFullscreen = 0;
    Atom netWmStateHidden = 0;
    Atom netWmName = 0;
    Atom netSupported = 0;
    Atom utf8String = 0;
};

class X11Platform final : public Platform {
public:
    X11Platform(Logger& logger, Display* display, GLXFBConfig framebufferConfig, XVisualInfo* visual);
    ~X11Platform() override;

    X11Platform(const X11Platform&) = delete;
    X11Platform& operator=(const X11Platform&) = delete;

    [[nodiscard]] std::string_view name() const override { return "X11"; }
    [[nodiscard]] Result<std::unique_ptr<schuimm::Window>> createWindow(const WindowDescription& description) override;
    void pollEvents() override;
    void waitEvents(double timeoutSeconds) override;

    [[nodiscard]] Display* display() const noexcept { return display_; }
    [[nodiscard]] const Atoms& atoms() const noexcept { return atoms_; }
    [[nodiscard]] GLXFBConfig framebufferConfig() const noexcept { return framebufferConfig_; }
    [[nodiscard]] XVisualInfo* visual() const noexcept { return visual_; }
    [[nodiscard]] Key translateKeyCode(unsigned int keyCode) const noexcept;
    [[nodiscard]] bool supportsNetWmFullscreen() const noexcept { return netWmFullscreenSupported_; }

    void registerWindow(X11Window* window);
    void unregisterWindow(X11Window* window);

private:
    void buildKeyMap();
    void detectWindowManagerFeatures();

    Logger& logger_;
    Display* display_ = nullptr;
    GLXFBConfig framebufferConfig_ = nullptr;
    XVisualInfo* visual_ = nullptr;
    Atoms atoms_;
    std::array<Key, 256> keyMap_{};
    bool netWmFullscreenSupported_ = false;
    std::vector<X11Window*> windows_; // Non-owning; windows unregister themselves.
};

} // namespace schuimm::x11
