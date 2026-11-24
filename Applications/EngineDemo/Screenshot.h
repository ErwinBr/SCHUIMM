#pragma once

#include "Core/Image/Image.h"

#include <string>

namespace demo {

// Writes RGB of an RGBA image as binary PPM (P6). Returns false on I/O failure.
[[nodiscard]] bool writePpm(const std::string& path, const schuimm::Image& image);

} // namespace demo
