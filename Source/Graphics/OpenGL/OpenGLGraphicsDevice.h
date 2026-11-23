#pragma once

#include "Core/Common/Error.h"
#include "Core/Logging/Logger.h"
#include "Graphics/Interface/GraphicsDevice.h"
#include "Platform/Interface/OpenGLSurface.h"

#include <memory>

namespace schuimm {

// Creates the OpenGL 3.3 core graphics device on top of a native OpenGL surface.
// The device takes ownership of the surface. The window that created the surface and the
// logger must outlive the device.
[[nodiscard]] Result<std::unique_ptr<GraphicsDevice>> createOpenGLGraphicsDevice(
    std::unique_ptr<OpenGLSurface> surface, const GraphicsDeviceSettings& settings, Logger& logger);

// The surface description the OpenGL device expects (3.3 core).
[[nodiscard]] OpenGLSurfaceDescription defaultOpenGLSurfaceDescription();

} // namespace schuimm
