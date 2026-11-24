#pragma once

#include "Core/Common/Geometry.h"
#include "Graphics/Interface/Texture.h"

#include <optional>
#include <vector>

namespace schuimm {

// Packs rectangles into a fixed-size area in rows ("shelves"), e.g. glyphs into an atlas page.
// Placed rectangles never move. `padding` empty pixels are kept between rectangles and to the
// area's right and bottom edges, so linear filtering does not bleed between neighbours.
class ShelfPacker {
public:
    ShelfPacker(Extent2D size, int padding);

    // Returns the placed rectangle, or nullopt if it does not fit (anymore).
    [[nodiscard]] std::optional<TextureRegion> insert(int width, int height);
    void reset();

    [[nodiscard]] Extent2D size() const noexcept { return size_; }

private:
    struct Shelf {
        int y = 0;
        int height = 0;
        int x = 0; // next free column
    };

    Extent2D size_;
    int padding_;
    int nextShelfY_;
    std::vector<Shelf> shelves_;
};

} // namespace schuimm
