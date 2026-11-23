#include "Platform/Windows/Win32OpenGLSurface.h"

#include "Core/Common/StringBuilder.h"
#include "Platform/Windows/Win32Platform.h"
#include "Platform/Windows/Win32Window.h"

#include <string_view>

namespace schuimm::win32 {

namespace {

// WGL_ARB_pixel_format / WGL_ARB_create_context / WGL_ARB_create_context_profile tokens.
constexpr int WGL_DRAW_TO_WINDOW_ARB = 0x2001;
constexpr int WGL_ACCELERATION_ARB = 0x2003;
constexpr int WGL_SUPPORT_OPENGL_ARB = 0x2010;
constexpr int WGL_DOUBLE_BUFFER_ARB = 0x2011;
constexpr int WGL_PIXEL_TYPE_ARB = 0x2013;
constexpr int WGL_COLOR_BITS_ARB = 0x2014;
constexpr int WGL_ALPHA_BITS_ARB = 0x201B;
constexpr int WGL_DEPTH_BITS_ARB = 0x2022;
constexpr int WGL_STENCIL_BITS_ARB = 0x2023;
constexpr int WGL_FULL_ACCELERATION_ARB = 0x2027;
constexpr int WGL_TYPE_RGBA_ARB = 0x202B;
constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
constexpr int WGL_CONTEXT_FLAGS_ARB = 0x2094;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126;
constexpr int WGL_CONTEXT_DEBUG_BIT_ARB = 0x0001;
constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x0001;
constexpr DWORD ERROR_INVALID_VERSION_ARB = 0x2095;
constexpr DWORD ERROR_INVALID_PROFILE_ARB = 0x2096;

using GetExtensionsStringArbFn = const char*(WINAPI*)(HDC);
using ChoosePixelFormatArbFn = BOOL(WINAPI*)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
using CreateContextAttribsArbFn = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using SwapIntervalExtFn = BOOL(WINAPI*)(int);

struct WglExtensions {
    ChoosePixelFormatArbFn choosePixelFormat = nullptr;
    CreateContextAttribsArbFn createContextAttribs = nullptr;
    SwapIntervalExtFn swapInterval = nullptr;
    bool createContextProfile = false;
};

bool hasExtension(const char* extensions, std::string_view name)
{
    if (extensions == nullptr) {
        return false;
    }
    std::string_view list(extensions);
    while (!list.empty()) {
        const std::size_t end = list.find(' ');
        if (list.substr(0, end) == name) {
            return true;
        }
        if (end == std::string_view::npos) {
            break;
        }
        list.remove_prefix(end + 1);
    }
    return false;
}

template <typename Function>
Function wglFunction(const char* name)
{
    return reinterpret_cast<Function>(reinterpret_cast<void*>(wglGetProcAddress(name)));
}

// Step 1 of modern context creation: a throw-away legacy context on a hidden window.
Result<WglExtensions> loadExtensions(Win32Platform& platform)
{
    HWND dummyWindow = CreateWindowExW(0, platform.windowClassName(), L"SchuimmWglDummy", WS_OVERLAPPEDWINDOW, 0, 0, 1,
                                       1, nullptr, nullptr, platform.instance(), nullptr);
    if (dummyWindow == nullptr) {
        return makeError(ErrorCode::PlatformFailure, "cannot create the WGL bootstrap window: " + lastErrorMessage());
    }
    HDC dummyDc = GetDC(dummyWindow);

    PIXELFORMATDESCRIPTOR descriptor{};
    descriptor.nSize = sizeof(descriptor);
    descriptor.nVersion = 1;
    descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    descriptor.iPixelType = PFD_TYPE_RGBA;
    descriptor.cColorBits = 32;
    descriptor.cAlphaBits = 8;
    descriptor.cDepthBits = 24;
    descriptor.cStencilBits = 8;
    descriptor.iLayerType = PFD_MAIN_PLANE;

    WglExtensions extensions;
    std::string failure;
    HGLRC dummyContext = nullptr;
    const int format = ChoosePixelFormat(dummyDc, &descriptor);
    if (format == 0 || !SetPixelFormat(dummyDc, format, &descriptor)) {
        failure = "cannot set a legacy pixel format: " + lastErrorMessage();
    } else if ((dummyContext = wglCreateContext(dummyDc)) == nullptr || !wglMakeCurrent(dummyDc, dummyContext)) {
        failure = "cannot create the bootstrap OpenGL context: " + lastErrorMessage();
    } else {
        const auto getExtensionsString = wglFunction<GetExtensionsStringArbFn>("wglGetExtensionsStringARB");
        const char* list = getExtensionsString != nullptr ? getExtensionsString(dummyDc) : nullptr;
        if (hasExtension(list, "WGL_ARB_pixel_format")) {
            extensions.choosePixelFormat = wglFunction<ChoosePixelFormatArbFn>("wglChoosePixelFormatARB");
        }
        if (hasExtension(list, "WGL_ARB_create_context")) {
            extensions.createContextAttribs = wglFunction<CreateContextAttribsArbFn>("wglCreateContextAttribsARB");
        }
        extensions.createContextProfile = hasExtension(list, "WGL_ARB_create_context_profile");
        if (hasExtension(list, "WGL_EXT_swap_control")) {
            extensions.swapInterval = wglFunction<SwapIntervalExtFn>("wglSwapIntervalEXT");
        }
    }

    // Step 2: tear the bootstrap down completely.
    wglMakeCurrent(nullptr, nullptr);
    if (dummyContext != nullptr) {
        wglDeleteContext(dummyContext);
    }
    ReleaseDC(dummyWindow, dummyDc);
    DestroyWindow(dummyWindow);

    if (!failure.empty()) {
        return makeError(ErrorCode::GraphicsFailure, failure);
    }
    if (extensions.choosePixelFormat == nullptr || extensions.createContextAttribs == nullptr ||
        !extensions.createContextProfile) {
        return makeError(ErrorCode::Unsupported,
                         "the OpenGL driver lacks WGL_ARB_pixel_format / WGL_ARB_create_context_profile "
                         "(is a GPU driver installed? The Microsoft GDI renderer only supports OpenGL 1.1)");
    }
    return extensions;
}

} // namespace

Result<std::unique_ptr<OpenGLSurface>> Win32OpenGLSurface::create(Win32Platform& platform, Win32Window& window,
                                                                  Logger& logger,
                                                                  const OpenGLSurfaceDescription& description)
{
    auto extensions = loadExtensions(platform);
    if (!extensions) {
        return std::unexpected(extensions.error());
    }

    // Step 3: the real pixel format, chosen through the extension.
    const HDC dc = window.deviceContext();
    const int pixelAttributes[] = {
        WGL_DRAW_TO_WINDOW_ARB, TRUE,
        WGL_SUPPORT_OPENGL_ARB, TRUE,
        WGL_DOUBLE_BUFFER_ARB, TRUE,
        WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB, 32,
        WGL_ALPHA_BITS_ARB, 8,
        WGL_DEPTH_BITS_ARB, description.depthBits,
        WGL_STENCIL_BITS_ARB, description.stencilBits,
        0,
    };
    int format = 0;
    UINT formatCount = 0;
    if (!extensions->choosePixelFormat(dc, pixelAttributes, nullptr, 1, &format, &formatCount) || formatCount == 0) {
        return makeError(ErrorCode::GraphicsFailure, "wglChoosePixelFormatARB found no accelerated RGBA8 D24S8 format");
    }
    PIXELFORMATDESCRIPTOR descriptor{};
    DescribePixelFormat(dc, format, sizeof(descriptor), &descriptor);
    if (!SetPixelFormat(dc, format, &descriptor)) {
        return makeError(ErrorCode::GraphicsFailure, "SetPixelFormat failed: " + lastErrorMessage());
    }

    const int contextAttributes[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, description.majorVersion,
        WGL_CONTEXT_MINOR_VERSION_ARB, description.minorVersion,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        WGL_CONTEXT_FLAGS_ARB, description.debugContext ? WGL_CONTEXT_DEBUG_BIT_ARB : 0,
        0,
    };
    HGLRC context = extensions->createContextAttribs(dc, nullptr, contextAttributes);
    if (context == nullptr) {
        const DWORD error = GetLastError() & 0xFFFF;
        const char* reason = error == ERROR_INVALID_VERSION_ARB   ? "version not supported"
                             : error == ERROR_INVALID_PROFILE_ARB ? "profile not supported"
                                                                  : "driver error";
        return makeError(ErrorCode::GraphicsFailure, concat("wglCreateContextAttribsARB failed for OpenGL ",
                                                            description.majorVersion, ".", description.minorVersion,
                                                            " core: ", reason));
    }
    if (!wglMakeCurrent(dc, context)) {
        const std::string message = lastErrorMessage();
        wglDeleteContext(context);
        return makeError(ErrorCode::GraphicsFailure, "wglMakeCurrent failed: " + message);
    }

    // OpenGL 1.1 functions are exported by opengl32.dll, not returned by wglGetProcAddress.
    HMODULE library = LoadLibraryW(L"opengl32.dll");
    auto surface = std::make_unique<Win32OpenGLSurface>(window, context, library);
    surface->swapInterval_ = extensions->swapInterval;
    logger.info("Platform", concat("Created WGL context (pixel format ", format, ", swap control: ",
                                   surface->swapInterval_ != nullptr ? "WGL_EXT_swap_control" : "none", ")"));
    return std::unique_ptr<OpenGLSurface>(std::move(surface));
}

Win32OpenGLSurface::Win32OpenGLSurface(Win32Window& window, HGLRC context, HMODULE openGLLibrary)
    : window_(window)
    , context_(context)
    , openGLLibrary_(openGLLibrary)
{
    window_.setSurfaceAttached(true);
}

Win32OpenGLSurface::~Win32OpenGLSurface()
{
    if (wglGetCurrentContext() == context_) {
        wglMakeCurrent(nullptr, nullptr);
    }
    wglDeleteContext(context_);
    if (openGLLibrary_ != nullptr) {
        FreeLibrary(openGLLibrary_);
    }
    window_.setSurfaceAttached(false);
}

void Win32OpenGLSurface::makeCurrent()
{
    wglMakeCurrent(window_.deviceContext(), context_);
}

void Win32OpenGLSurface::swapBuffers()
{
    SwapBuffers(window_.deviceContext());
}

bool Win32OpenGLSurface::setSwapInterval(int interval)
{
    return swapInterval_ != nullptr && swapInterval_(interval) == TRUE;
}

OpenGLProc Win32OpenGLSurface::getProcAddress(const char* name) const
{
    PROC procedure = wglGetProcAddress(name);
    // Some drivers return small sentinel values instead of nullptr for unknown names.
    const auto value = reinterpret_cast<std::intptr_t>(procedure);
    if (value == 0 || value == 1 || value == 2 || value == 3 || value == -1) {
        procedure = openGLLibrary_ != nullptr ? GetProcAddress(openGLLibrary_, name) : nullptr;
    }
    return reinterpret_cast<OpenGLProc>(reinterpret_cast<void*>(procedure));
}

Extent2D Win32OpenGLSurface::framebufferSize() const
{
    return window_.framebufferSize();
}

} // namespace schuimm::win32
