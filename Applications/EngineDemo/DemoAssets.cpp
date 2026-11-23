#include "DemoAssets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

namespace demo {

using schuimm::Image;

namespace {
std::uint8_t toByte(float value)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}
} // namespace

Image makeCheckerboard(int size, int cells)
{
    Image image;
    image.size = {size, size};
    image.pixels.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4);
    const int cell = std::max(1, size / std::max(1, cells));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool light = ((x / cell) + (y / cell)) % 2 == 0;
            // Warm gradient across the board so orientation is visible in screenshots.
            const float fx = static_cast<float>(x) / static_cast<float>(size - 1);
            const float fy = static_cast<float>(y) / static_cast<float>(size - 1);
            std::uint8_t* p = &image.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4];
            if (light) {
                p[0] = toByte(0.95f);
                p[1] = toByte(0.85f - 0.3f * fy);
                p[2] = toByte(0.35f + 0.4f * fx);
            } else {
                p[0] = toByte(0.20f + 0.2f * fx);
                p[1] = toByte(0.18f);
                p[2] = toByte(0.30f + 0.3f * fy);
            }
            p[3] = 255;
        }
    }
    return image;
}

Image makeAlphaSprite(int size)
{
    // A soft glowing orb: opaque core, smooth alpha falloff, fully transparent corners.
    Image image;
    image.size = {size, size};
    image.pixels.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4);
    const float centre = (static_cast<float>(size) - 1.0f) * 0.5f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = (static_cast<float>(x) - centre) / centre;
            const float dy = (static_cast<float>(y) - centre) / centre;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float alpha = distance >= 1.0f ? 0.0f : std::pow(1.0f - distance, 0.6f);
            std::uint8_t* p = &image.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4];
            p[0] = toByte(0.55f + 0.45f * (1.0f - distance));
            p[1] = toByte(0.85f);
            p[2] = toByte(1.0f);
            p[3] = toByte(alpha);
        }
    }
    return image;
}

bool writePpm(const std::string& path, const Image& image)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << "P6\n" << image.size.width << " " << image.size.height << "\n255\n";
    const std::size_t pixelCount = static_cast<std::size_t>(image.size.width) * static_cast<std::size_t>(image.size.height);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        file.write(reinterpret_cast<const char*>(&image.pixels[i * 4]), 3);
    }
    return static_cast<bool>(file);
}

} // namespace demo
