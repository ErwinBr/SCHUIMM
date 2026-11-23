#pragma once

#include "Core/Common/Error.h"
#include "Core/Logging/Logger.h"
#include "Platform/Interface/Platform.h"

#include <memory>

namespace schuimm {

// Creates the native platform backend selected at configure time
// (Win32 on Windows, Cocoa on macOS, X11 on Linux). The logger must outlive the platform.
[[nodiscard]] Result<std::unique_ptr<Platform>> createNativePlatform(Logger& logger);

} // namespace schuimm
