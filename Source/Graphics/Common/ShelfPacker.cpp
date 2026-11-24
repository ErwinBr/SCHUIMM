#include "Graphics/Common/ShelfPacker.h"

namespace schuimm {

ShelfPacker::ShelfPacker(Extent2D size, int padding)
    : size_(size)
    , padding_(padding < 0 ? 0 : padding)
    , nextShelfY_(padding_)
{
}

void ShelfPacker::reset()
{
    shelves_.clear();
    nextShelfY_ = padding_;
}

std::optional<TextureRegion> ShelfPacker::insert(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    const int paddedWidth = width + padding_;
    // Best fit among existing shelves: the lowest shelf that is tall enough, to limit waste.
    Shelf* best = nullptr;
    for (Shelf& shelf : shelves_) {
        if (height <= shelf.height && shelf.x + paddedWidth <= size_.width && (best == nullptr || shelf.height < best->height)) {
            best = &shelf;
        }
    }
    if (best == nullptr) {
        if (nextShelfY_ + height + padding_ > size_.height || padding_ + paddedWidth > size_.width) {
            return std::nullopt;
        }
        shelves_.push_back({nextShelfY_, height, padding_});
        nextShelfY_ += height + padding_;
        best = &shelves_.back();
    }
    const TextureRegion region{best->x, best->y, width, height};
    best->x += paddedWidth;
    return region;
}

} // namespace schuimm
