#include "Screenshot.h"

#include <fstream>

namespace demo {

bool writePpm(const std::string& path, const schuimm::Image& image)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << "P6\n" << image.width() << " " << image.height() << "\n255\n";
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            file.write(reinterpret_cast<const char*>(image.pixel(x, y).data()), 3);
        }
    }
    return static_cast<bool>(file);
}

} // namespace demo
