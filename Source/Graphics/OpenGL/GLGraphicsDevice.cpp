#include "Graphics/OpenGL/OpenGLGraphicsDevice.h"

#include "Core/Common/Assert.h"
#include "Core/Common/StringBuilder.h"
#include "Graphics/OpenGL/GLFunctions.h"
#include "Graphics/OpenGL/GLObjects.h"
#include "Graphics/OpenGL/GLRenderer2D.h"

#include <algorithm>
#include <cstring>

namespace schuimm {

namespace {

constexpr int requiredMajorVersion = 3;
constexpr int requiredMinorVersion = 3;

std::string glString(const gl::GLFunctions& gl, gl::GLenum name)
{
    const gl::GLubyte* value = gl.GetString(name);
    return value != nullptr ? std::string(reinterpret_cast<const char*>(value)) : std::string();
}

class GLGraphicsDevice final : public GraphicsDevice {
public:
    GLGraphicsDevice(std::unique_ptr<OpenGLSurface> surface, const GraphicsDeviceSettings& settings, Logger& logger)
        : surface_(std::move(surface))
        , settings_(settings)
        , logger_(logger)
    {
    }

    ~GLGraphicsDevice() override
    {
        // GL objects can only be deleted while their context is current.
        surface_->makeCurrent();
        sceneTarget_.reset();
        renderer_.reset();
        logger_.info("Graphics", "OpenGL device destroyed");
    }

    GLGraphicsDevice(const GLGraphicsDevice&) = delete;
    GLGraphicsDevice& operator=(const GLGraphicsDevice&) = delete;

    Status initialise()
    {
        surface_->makeCurrent();
        if (auto status = gl::loadFunctions(gl_, *surface_); !status) {
            return status;
        }
        if (auto status = detectCapabilities(); !status) {
            return status;
        }
        auto renderer = gl::GLRenderer2D::create(gl_, settings_.presentation.virtualResolution,
                                                 {capabilities_.maxTextureSize, capabilities_.textureSlots});
        if (!renderer) {
            return std::unexpected(renderer.error());
        }
        renderer_ = std::move(*renderer);
        logger_.info("Graphics", concat("Sprite renderer ready: ", capabilities_.textureSlots,
                                        " texture slots per batch (driver reports ", capabilities_.maxTextureImageUnits, " units)"));

        if (auto status = createSceneTarget(); !status) {
            return status;
        }

        // Fixed state for 2D rendering.
        gl_.Disable(gl::DEPTH_TEST);
        gl_.Disable(gl::CULL_FACE);

        const bool vsyncApplied = setVSync(settings_.vsync);
        logger_.info("Graphics", concat("VSync ", settings_.vsync ? "on" : "off",
                                        vsyncApplied ? "" : " requested (swap control unsupported by driver)"));

        if (const gl::GLenum error = gl_.GetError(); error != gl::NO_ERROR_) {
            return makeError(ErrorCode::GraphicsFailure, concat("OpenGL error 0x", std::hex, error, " after initialisation"));
        }
        return {};
    }

    const GraphicsCapabilities& capabilities() const override { return capabilities_; }
    Renderer2D& renderer() override { return *renderer_; }
    const PresentationSettings& presentation() const override { return settings_.presentation; }

    void beginFrame() override
    {
        SCHUIMM_ASSERT(!inFrame_, "beginFrame() called twice");
        inFrame_ = true;
        renderer_->beginFrame(*sceneTarget_);
    }

    void endFrame() override
    {
        SCHUIMM_ASSERT(inFrame_, "endFrame() without beginFrame()");
        inFrame_ = false;
        renderer_->endFrame();

        // Composite the virtual-resolution scene into the window, preserving aspect ratio.
        const Extent2D framebuffer = surface_->framebufferSize();
        if (!framebuffer.isEmpty()) {
            renderer_->beginWindowPass(framebuffer);
            const Color border = settings_.presentation.borderColor;
            renderer_->clear({border.r, border.g, border.b, 1.0f});
            const PixelRect rect = computePresentationRect(
                settings_.presentation.virtualResolution, framebuffer, settings_.presentation.integerScaling);
            renderer_->drawTexture(sceneTarget_->texture(),
                RectF{static_cast<float>(rect.x), static_cast<float>(rect.y), static_cast<float>(rect.width),
                      static_cast<float>(rect.height)});
            renderer_->endWindowPass();

            if (captureRequested_) {
                captureRequested_ = false;
                captured_ = readBackBuffer(framebuffer);
            }
        }

        checkErrors();
    }

    void present() override { surface_->swapBuffers(); }

    bool setVSync(bool enabled) override
    {
        const bool applied = surface_->setSwapInterval(enabled ? 1 : 0);
        if (applied) {
            vsync_ = enabled;
        }
        return applied;
    }

    bool isVSyncEnabled() const override { return vsync_; }

    GraphicsResourceCounts liveResources() const override
    {
        // The scene target is the device's own.
        return {renderer_->liveTextureCount(), renderer_->liveRenderTargetCount() - (sceneTarget_ ? 1 : 0)};
    }

    void requestFrameCapture() override { captureRequested_ = true; }

    std::optional<Image> takeCapturedFrame() override
    {
        std::optional<Image> result = std::move(captured_);
        captured_.reset();
        return result;
    }

private:
    Status detectCapabilities()
    {
        GraphicsCapabilities caps;
        caps.api = "OpenGL";
        caps.version = glString(gl_, gl::VERSION);
        caps.renderer = glString(gl_, gl::RENDERER);
        caps.vendor = glString(gl_, gl::VENDOR);
        caps.shadingLanguage = glString(gl_, gl::SHADING_LANGUAGE_VERSION);
        gl_.GetIntegerv(gl::MAJOR_VERSION, &caps.majorVersion);
        gl_.GetIntegerv(gl::MINOR_VERSION, &caps.minorVersion);
        gl::GLint profileMask = 0;
        gl_.GetIntegerv(gl::CONTEXT_PROFILE_MASK, &profileMask);
        caps.coreProfile = (profileMask & gl::CONTEXT_CORE_PROFILE_BIT) != 0;
        gl_.GetIntegerv(gl::MAX_TEXTURE_SIZE, &caps.maxTextureSize);
        gl_.GetIntegerv(gl::MAX_TEXTURE_IMAGE_UNITS, &caps.maxTextureImageUnits);
        // OpenGL 3.3 guarantees 16; use fewer only if a driver reports fewer.
        caps.textureSlots = std::clamp(caps.maxTextureImageUnits, 1, maxSpriteTextureSlots);
        caps.swapControl = surface_->supportsSwapControl();
        (void)gl_.GetError(); // CONTEXT_PROFILE_MASK may be rejected by non-conforming drivers.

        logger_.info("Graphics", concat("OpenGL version: ", caps.version));
        logger_.info("Graphics", concat("OpenGL renderer: ", caps.renderer, " (", caps.vendor, ")"));
        logger_.info("Graphics", concat("GLSL version: ", caps.shadingLanguage));
        logger_.info("Graphics", concat("Context: ", caps.majorVersion, ".", caps.minorVersion,
                                        caps.coreProfile ? " core profile" : " (no core profile bit)",
                                        ", max texture size ", caps.maxTextureSize));

        const bool versionOk = caps.majorVersion > requiredMajorVersion ||
                               (caps.majorVersion == requiredMajorVersion && caps.minorVersion >= requiredMinorVersion);
        if (!versionOk) {
            return makeError(ErrorCode::Unsupported,
                concat("OpenGL ", requiredMajorVersion, ".", requiredMinorVersion, " core required, context provides ",
                       caps.majorVersion, ".", caps.minorVersion, " (", caps.renderer, ")"));
        }
        if (!caps.coreProfile) {
            return makeError(ErrorCode::Unsupported, "the OpenGL context is not a core profile context");
        }
        capabilities_ = std::move(caps);
        return {};
    }

    Status createSceneTarget()
    {
        const Extent2D size = settings_.presentation.virtualResolution;
        auto target = renderer_->createRenderTarget({size, {settings_.presentation.scalingFilter}});
        if (!target) {
            return makeError(target.error().code, concat("scene target: ", target.error().message));
        }
        sceneTarget_.reset(static_cast<gl::GLRenderTarget*>(target->release()));
        logger_.info("Graphics", concat("Scene render target ", size.width, "x", size.height, " created"));
        return {};
    }

    Image readBackBuffer(Extent2D size)
    {
        auto created = Image::create(size, PixelFormat::RGBA8);
        if (!created) {
            logger_.warning("Graphics", concat("Frame capture skipped: ", created.error().message));
            return {};
        }
        Image image = std::move(*created);
        // Image::create produces a tightly packed RGBA8 image, matching PACK_ALIGNMENT 1.
        const std::size_t rowBytes = image.rowStride();
        gl_.ReadBuffer(gl::BACK);
        gl_.PixelStorei(gl::PACK_ALIGNMENT, 1);
        gl_.ReadPixels(0, 0, size.width, size.height, gl::RGBA, gl::UNSIGNED_BYTE, image.pixels().data());
        // OpenGL returns rows bottom-up; Image stores them top-down.
        std::vector<std::uint8_t> row(rowBytes);
        for (int top = 0, bottom = size.height - 1; top < bottom; ++top, --bottom) {
            std::uint8_t* a = image.row(top).data();
            std::uint8_t* b = image.row(bottom).data();
            std::memcpy(row.data(), a, rowBytes);
            std::memcpy(a, b, rowBytes);
            std::memcpy(b, row.data(), rowBytes);
        }
        return image;
    }

    void checkErrors()
    {
        const gl::GLenum error = gl_.GetError();
        if (error != gl::NO_ERROR_ && reportedErrors_ < 10) {
            ++reportedErrors_;
            logger_.error("Graphics", concat("OpenGL error 0x", std::hex, error, " during frame"));
        }
    }

    // Declaration order matters: the surface (context) must be destroyed last.
    std::unique_ptr<OpenGLSurface> surface_;
    GraphicsDeviceSettings settings_;
    Logger& logger_;
    gl::GLFunctions gl_;
    GraphicsCapabilities capabilities_;
    std::unique_ptr<gl::GLRenderer2D> renderer_;
    std::unique_ptr<gl::GLRenderTarget> sceneTarget_;
    bool vsync_ = false;
    bool inFrame_ = false;
    bool captureRequested_ = false;
    std::optional<Image> captured_;
    int reportedErrors_ = 0;
};

} // namespace

OpenGLSurfaceDescription defaultOpenGLSurfaceDescription()
{
    OpenGLSurfaceDescription description;
    description.majorVersion = requiredMajorVersion;
    description.minorVersion = requiredMinorVersion;
#if !defined(NDEBUG)
    description.debugContext = true;
#endif
    return description;
}

Result<std::unique_ptr<GraphicsDevice>> createOpenGLGraphicsDevice(
    std::unique_ptr<OpenGLSurface> surface, const GraphicsDeviceSettings& settings, Logger& logger)
{
    if (!surface) {
        return makeError(ErrorCode::InvalidArgument, "an OpenGL surface is required");
    }
    auto device = std::make_unique<GLGraphicsDevice>(std::move(surface), settings, logger);
    if (auto status = device->initialise(); !status) {
        return std::unexpected(status.error());
    }
    return std::unique_ptr<GraphicsDevice>(std::move(device));
}

} // namespace schuimm
