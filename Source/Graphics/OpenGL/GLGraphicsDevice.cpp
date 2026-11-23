#include "Graphics/OpenGL/OpenGLGraphicsDevice.h"

#include "Core/Common/Assert.h"
#include "Core/Common/StringBuilder.h"
#include "Graphics/OpenGL/GLFunctions.h"
#include "Graphics/OpenGL/GLObjects.h"
#include "Graphics/OpenGL/GLRenderer2D.h"

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
        sceneFramebuffer_.reset();
        sceneTexture_.reset();
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
        auto renderer = gl::GLRenderer2D::create(gl_, settings_.presentation.virtualResolution);
        if (!renderer) {
            return std::unexpected(renderer.error());
        }
        renderer_ = std::move(*renderer);
        logger_.info("Graphics", "Shaders compiled and linked");

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
        const Extent2D virtualSize = settings_.presentation.virtualResolution;
        gl_.BindFramebuffer(gl::FRAMEBUFFER, sceneFramebuffer_.id());
        gl_.Viewport(0, 0, virtualSize.width, virtualSize.height);
        gl_.Enable(gl::BLEND);
        // Straight alpha for colour; alpha accumulates "over" so the target stays meaningful.
        gl_.BlendFuncSeparate(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
        renderer_->setTargetSize(virtualSize);
    }

    void endFrame() override
    {
        SCHUIMM_ASSERT(inFrame_, "endFrame() without beginFrame()");
        inFrame_ = false;

        // Composite the virtual-resolution scene into the window, preserving aspect ratio.
        const Extent2D framebuffer = surface_->framebufferSize();
        gl_.BindFramebuffer(gl::FRAMEBUFFER, 0);
        gl_.Disable(gl::BLEND);
        if (!framebuffer.isEmpty()) {
            gl_.Viewport(0, 0, framebuffer.width, framebuffer.height);
            const Color border = settings_.presentation.borderColor;
            gl_.ClearColor(border.r, border.g, border.b, 1.0f);
            gl_.Clear(gl::COLOR_BUFFER_BIT);

            const PixelRect rect = computePresentationRect(
                settings_.presentation.virtualResolution, framebuffer, settings_.presentation.integerScaling);
            renderer_->setTargetSize(framebuffer);
            // The scene texture was rendered with y pointing down in clip space, i.e. its top row
            // is at t = 1. Flip v so the image appears upright.
            renderer_->drawTextureId(sceneTexture_.id(),
                RectF{static_cast<float>(rect.x), static_cast<float>(rect.y), static_cast<float>(rect.width),
                      static_cast<float>(rect.height)},
                RectF{0.0f, 1.0f, 1.0f, -1.0f}, Color::white());

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
        if (size.isEmpty() || size.width > capabilities_.maxTextureSize || size.height > capabilities_.maxTextureSize) {
            return makeError(ErrorCode::InvalidArgument,
                concat("invalid virtual resolution ", size.width, "x", size.height));
        }
        sceneTexture_ = gl::createTextureObject(gl_);
        const gl::GLint filter = settings_.presentation.scalingFilter == TextureFilter::Nearest
                                     ? static_cast<gl::GLint>(gl::NEAREST)
                                     : static_cast<gl::GLint>(gl::LINEAR);
        gl_.BindTexture(gl::TEXTURE_2D, sceneTexture_.id());
        gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, filter);
        gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, filter);
        gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
        gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
        gl_.TexImage2D(gl::TEXTURE_2D, 0, static_cast<gl::GLint>(gl::RGBA8), size.width, size.height, 0, gl::RGBA,
                       gl::UNSIGNED_BYTE, nullptr);
        gl_.BindTexture(gl::TEXTURE_2D, 0);

        sceneFramebuffer_ = gl::createFramebuffer(gl_);
        gl_.BindFramebuffer(gl::FRAMEBUFFER, sceneFramebuffer_.id());
        gl_.FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, sceneTexture_.id(), 0);
        const gl::GLenum status = gl_.CheckFramebufferStatus(gl::FRAMEBUFFER);
        gl_.BindFramebuffer(gl::FRAMEBUFFER, 0);
        if (status != gl::FRAMEBUFFER_COMPLETE) {
            return makeError(ErrorCode::GraphicsFailure, concat("scene framebuffer incomplete (0x", std::hex, status, ")"));
        }
        logger_.info("Graphics", concat("Scene framebuffer ", size.width, "x", size.height, " created"));
        return {};
    }

    Image readBackBuffer(Extent2D size)
    {
        Image image;
        image.size = size;
        const std::size_t rowBytes = static_cast<std::size_t>(size.width) * 4;
        image.pixels.resize(rowBytes * static_cast<std::size_t>(size.height));
        gl_.ReadBuffer(gl::BACK);
        gl_.PixelStorei(gl::PACK_ALIGNMENT, 1);
        gl_.ReadPixels(0, 0, size.width, size.height, gl::RGBA, gl::UNSIGNED_BYTE, image.pixels.data());
        // OpenGL returns rows bottom-up; Image stores them top-down.
        std::vector<std::uint8_t> row(rowBytes);
        for (int top = 0, bottom = size.height - 1; top < bottom; ++top, --bottom) {
            std::uint8_t* a = image.pixels.data() + static_cast<std::size_t>(top) * rowBytes;
            std::uint8_t* b = image.pixels.data() + static_cast<std::size_t>(bottom) * rowBytes;
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
    gl::TextureHandle sceneTexture_;
    gl::FramebufferHandle sceneFramebuffer_;
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
