#pragma once

#include "Graphics/Interface/Image.h"

#include <string>

namespace demo {

// Procedurally generated images, so the demo needs no image decoder (a later milestone).
[[nodiscard]] schuimm::Image makeCheckerboard(int size, int cells);
[[nodiscard]] schuimm::Image makeAlphaSprite(int size);

// Writes RGB of an RGBA image as binary PPM (P6). Returns false on I/O failure.
[[nodiscard]] bool writePpm(const std::string& path, const schuimm::Image& image);

} // namespace demo
