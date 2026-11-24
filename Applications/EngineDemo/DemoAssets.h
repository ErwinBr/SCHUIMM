#pragma once

#include "Core/Image/Image.h"

#include <string>

namespace demo {

// Procedurally generated images (the demo ships no image assets; PNG decoding lives in the
// resource layer, see Source/Resource).
[[nodiscard]] schuimm::Image makeCheckerboard(int size, int cells);
[[nodiscard]] schuimm::Image makeAlphaSprite(int size);

// Writes RGB of an RGBA image as binary PPM (P6). Returns false on I/O failure.
[[nodiscard]] bool writePpm(const std::string& path, const schuimm::Image& image);

} // namespace demo
