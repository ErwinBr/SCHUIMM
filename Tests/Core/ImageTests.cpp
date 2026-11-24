#include "TestFramework.h"

#include "Core/Image/Image.h"

using namespace schuimm;

SCHUIMM_TEST(Image_CreateLayout)
{
    const auto image = Image::create({3, 2});
    REQUIRE(image.has_value());
    CHECK_EQ(image->width(), 3);
    CHECK_EQ(image->height(), 2);
    CHECK(image->format() == PixelFormat::RGBA8);
    CHECK_EQ(bytesPerPixel(PixelFormat::RGBA8), std::size_t{4});
    CHECK_EQ(image->rowStride(), std::size_t{12});
    CHECK_EQ(image->pixels().size(), std::size_t{24});
    CHECK(!image->isEmpty());
    for (const std::uint8_t byte : image->pixels()) {
        CHECK_EQ(int{byte}, 0); // transparent black
    }
}

SCHUIMM_TEST(Image_RowAndPixelAccess)
{
    Image image = Image::create({3, 2}).value();
    auto p = image.pixel(2, 1);
    p[0] = 10;
    p[3] = 200;
    CHECK_EQ(image.row(1).size(), std::size_t{12});
    CHECK_EQ(int{image.row(1)[8]}, 10);
    CHECK_EQ(int{image.pixels()[1 * 12 + 2 * 4 + 3]}, 200);

    const Image copy = image; // deep copy
    image.pixel(2, 1)[0] = 99;
    CHECK_EQ(int{copy.pixel(2, 1)[0]}, 10);
}

SCHUIMM_TEST(Image_RejectsInvalidSizes)
{
    CHECK(!Image::create({0, 4}).has_value());
    CHECK(!Image::create({4, -1}).has_value());
    CHECK(!Image::create({Image::maxDimension + 1, 1}).has_value());
    CHECK(Image::create({Image::maxDimension, 1}).has_value());
    CHECK(Image().isEmpty());
}
