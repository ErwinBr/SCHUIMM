// validatePixelLayout and validateRegion: the overflow-safe checks every renderer backend runs
// before it reads caller-supplied pixel data.

#include "TestFramework.h"

#include "Graphics/Interface/Texture.h"

#include <climits>
#include <cstdint>
#include <limits>
#include <string>

using namespace schuimm;

namespace {

constexpr std::size_t maxSize = std::numeric_limits<std::size_t>::max();

bool isInvalidArgument(const Status& status)
{
    return !status.has_value() && status.error().code == ErrorCode::InvalidArgument;
}

bool mentions(const Status& status, const char* text)
{
    return !status.has_value() && status.error().message.find(text) != std::string::npos;
}

} // namespace

SCHUIMM_TEST(TextureValidation_PixelLayoutTightAndPadded)
{
    // 3x2 texels: rows of 12 bytes.
    CHECK(validatePixelLayout({3, 2}, 24, 0).has_value());
    CHECK(validatePixelLayout({3, 2}, 24, 12).has_value());
    CHECK(validatePixelLayout({3, 2}, 100, 0).has_value()); // extra data is allowed
    CHECK(isInvalidArgument(validatePixelLayout({3, 2}, 23, 0)));
    // Padded rows: the last row needs no padding, so 16 + 12 bytes is an exact fit.
    CHECK(validatePixelLayout({3, 2}, 28, 16).has_value());
    CHECK(isInvalidArgument(validatePixelLayout({3, 2}, 27, 16)));
    // One row: the stride never matters for the size.
    CHECK(validatePixelLayout({3, 1}, 12, 1000).has_value());
}

SCHUIMM_TEST(TextureValidation_PixelLayoutRejectsBadSizeAndStride)
{
    CHECK(isInvalidArgument(validatePixelLayout({0, 2}, 64, 0)));
    CHECK(isInvalidArgument(validatePixelLayout({2, 0}, 64, 0)));
    CHECK(isInvalidArgument(validatePixelLayout({-1, 2}, 64, 0)));
    CHECK(isInvalidArgument(validatePixelLayout({2, -5}, 64, 0)));
    CHECK(isInvalidArgument(validatePixelLayout({INT_MIN, INT_MIN}, maxSize, 0)));
    const Status small = validatePixelLayout({3, 2}, 64, 11);
    CHECK(isInvalidArgument(small));
    CHECK(mentions(small, "smaller than a row"));
}

SCHUIMM_TEST(TextureValidation_PixelLayoutRejectsOverflowingStride)
{
    // SIZE_MAX stride: stride * (height - 1) wraps.
    const Status huge = validatePixelLayout({2, 2}, 64, maxSize);
    CHECK(isInvalidArgument(huge));
    CHECK(mentions(huge, "row stride too large"));
    CHECK(isInvalidArgument(validatePixelLayout({2, 3}, maxSize, maxSize)));

    // Overflows only when multiplied: stride * 2 wraps to a tiny value, so a buffer of a few
    // bytes would pass an unchecked "stride * (height - 1) + rowBytes <= size" test.
    const std::size_t wrapsToZero = maxSize / 2 + 1;
    CHECK(isInvalidArgument(validatePixelLayout({1, 3}, 4, wrapsToZero)));
    CHECK(mentions(validatePixelLayout({1, 3}, 4, wrapsToZero), "row stride too large"));

    // The multiplication fits but adding the last row does not.
    const std::size_t justFits = maxSize - 3; // stride * 1 fits, + 8 bytes does not
    CHECK(isInvalidArgument(validatePixelLayout({2, 2}, maxSize, justFits)));
    // Exactly SIZE_MAX bytes needed and available.
    CHECK(validatePixelLayout({1, 2}, maxSize, maxSize - 4).has_value());

    // Huge width: width * 4 itself overflows only where size_t is 32 bits wide; INT_MAX texels
    // per row must be handled either way.
    CHECK(isInvalidArgument(validatePixelLayout({INT_MAX, 2}, 16, 0)));
}

SCHUIMM_TEST(TextureValidation_RegionInsideAndEdges)
{
    const Extent2D size{8, 4};
    CHECK(validateRegion(size, {0, 0, 8, 4}).has_value()); // the whole texture
    CHECK(validateRegion(size, {7, 3, 1, 1}).has_value()); // last texel
    CHECK(validateRegion(size, {2, 1, 6, 3}).has_value()); // touches the right and bottom edges
    CHECK(isInvalidArgument(validateRegion(size, {2, 1, 7, 3})));
    CHECK(isInvalidArgument(validateRegion(size, {2, 1, 6, 4})));
    CHECK(isInvalidArgument(validateRegion(size, {8, 0, 1, 1})));
    CHECK(isInvalidArgument(validateRegion(size, {0, 4, 1, 1})));
    CHECK(mentions(validateRegion(size, {8, 0, 1, 1}), "is outside the 8x4 texture"));
}

SCHUIMM_TEST(TextureValidation_RegionRejectsNegativeEmptyAndOverflowing)
{
    const Extent2D size{8, 4};
    CHECK(isInvalidArgument(validateRegion(size, {-1, 0, 1, 1})));
    CHECK(isInvalidArgument(validateRegion(size, {0, -1, 1, 1})));
    CHECK(isInvalidArgument(validateRegion(size, {0, 0, 0, 1})));
    CHECK(isInvalidArgument(validateRegion(size, {0, 0, 1, 0})));
    CHECK(isInvalidArgument(validateRegion(size, {0, 0, -3, 1})));
    CHECK(isInvalidArgument(validateRegion(size, {0, 0, 1, -3})));
    CHECK(isInvalidArgument(validateRegion(size, {INT_MIN, INT_MIN, INT_MAX, INT_MAX})));
    // x + width would be signed overflow (undefined behaviour) in a naive check.
    CHECK(isInvalidArgument(validateRegion(size, {1, 0, INT_MAX, 1})));
    CHECK(isInvalidArgument(validateRegion(size, {0, 1, 1, INT_MAX})));
    CHECK(isInvalidArgument(validateRegion(size, {INT_MAX, INT_MAX, INT_MAX, INT_MAX})));
    CHECK(isInvalidArgument(validateRegion({INT_MAX, INT_MAX}, {1, 1, INT_MAX, INT_MAX})));
    CHECK(validateRegion({INT_MAX, INT_MAX}, {1, 1, INT_MAX - 1, INT_MAX - 1}).has_value());
    // A region against an empty texture never fits.
    CHECK(isInvalidArgument(validateRegion({0, 0}, {0, 0, 1, 1})));
}
