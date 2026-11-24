#pragma once

#include "Core/Common/Geometry.h"
#include "Core/Image/Image.h"
#include "Graphics/Interface/Presentation.h"
#include "Graphics/Interface/Renderer2D.h"

#include <optional>
#include <string>

namespace schuimm {

struct GraphicsCapabilities {
    std::string api;            // "OpenGL"
    std::string version;        // GL_VERSION string
    std::string renderer;       // GL_RENDERER
    std::string vendor;         // GL_VENDOR
    std::string shadingLanguage;
    int majorVersion = 0;
    int minorVersion = 0;
    bool coreProfile = false;
    int maxTextureSize = 0;
    int maxTextureImageUnits = 0; // fragment shader texture units reported by the driver
    int textureSlots = 0;         // texture units the sprite batch uses (<= 16)
    bool swapControl = false;   // VSync can be toggled.
};

// Textures and render targets created through the renderer and not yet destroyed
// (the device's internal ones are not included). Both must be 0 when the device is destroyed.
struct GraphicsResourceCounts {
    int textures = 0;
    int renderTargets = 0;
};

struct GraphicsDeviceSettings {
    PresentationSettings presentation;
    bool vsync = true;
};

// Owns the graphics context and the per-frame protocol:
//
//   beginFrame()  -> start rendering into the virtual-resolution target
//   renderer()    -> draw
//   endFrame()    -> scale the target into the window (letterboxed)
//   present()     -> swap buffers (waits for vblank when VSync is on)
//
// All calls must happen on the thread that created the device.
class GraphicsDevice {
public:
    virtual ~GraphicsDevice() = default;

    [[nodiscard]] virtual const GraphicsCapabilities& capabilities() const = 0;
    [[nodiscard]] virtual Renderer2D& renderer() = 0;

    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void present() = 0;

    // Returns false if the driver does not allow changing the swap interval.
    virtual bool setVSync(bool enabled) = 0;
    [[nodiscard]] virtual bool isVSyncEnabled() const = 0;

    [[nodiscard]] virtual const PresentationSettings& presentation() const = 0;

    [[nodiscard]] virtual GraphicsResourceCounts liveResources() const = 0;

    // Requests a read-back of the next frame as presented (after endFrame()). Used by automated
    // integration tests; costs a GPU stall, so do not use it every frame.
    virtual void requestFrameCapture() = 0;
    [[nodiscard]] virtual std::optional<Image> takeCapturedFrame() = 0;
};

} // namespace schuimm
