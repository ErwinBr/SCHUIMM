#include "TestFramework.h"

#include "Resource/Image/ImageDecoder.h"
#include "Resource/Image/ImageLoader.h"
#include "TestAssets.h"
#include "TestPng.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

using namespace schuimm;
using namespace schuimm::test;

// The expected values below are the ones written by Tests/Assets/generate_test_images.py.

namespace {

using Rgba = std::array<int, 4>;

Rgba pixelAt(const Image& image, int x, int y)
{
    const auto p = image.pixel(x, y);
    return {p[0], p[1], p[2], p[3]};
}

int alphaAt(const Image& image, int x, int y)
{
    return image.pixel(x, y)[3];
}

std::string toText(const Rgba& p)
{
    return concat("(", p[0], ",", p[1], ",", p[2], ",", p[3], ")");
}

// Exact comparison, or within `tolerance` per channel (for values that go through conversion).
void checkPixel(const Image& image, int x, int y, Rgba expected, int tolerance = 0)
{
    const Rgba actual = pixelAt(image, x, y);
    bool ok = true;
    for (std::size_t i = 0; i < 4; ++i) {
        ok = ok && std::abs(actual[i] - expected[i]) <= tolerance;
    }
    if (!ok) {
        SCHUIMM_TEST_FAIL_(concat("pixel (", x, ",", y, ") is ", toText(actual), ", expected ", toText(expected)),
                           false);
    }
}

Result<Image> decodeAsset(std::string_view name)
{
    return loadImage(testAssetLocator(), assetPath(concat("Images/", name)));
}

} // namespace

SCHUIMM_TEST(ImageDecoder_DetectsFormatBySignature)
{
    const auto png = testAssetLocator().readBytes(assetPath("Images/rgba_4x2.png"));
    REQUIRE(png.has_value());
    CHECK(detectImageFileFormat(*png) == ImageFileFormat::Png);
    const auto text = testAssetLocator().readBytes(assetPath("Images/not_an_image.png"));
    REQUIRE(text.has_value());
    CHECK(detectImageFileFormat(*text) == ImageFileFormat::Unknown);
    CHECK(detectImageFileFormat({}) == ImageFileFormat::Unknown);
}

SCHUIMM_TEST(ImageDecoder_RgbaDimensionsFormatAndPixels)
{
    const auto image = decodeAsset("rgba_4x2.png");
    REQUIRE(image.has_value());
    CHECK_EQ(image->width(), 4);
    CHECK_EQ(image->height(), 2);
    CHECK(image->format() == PixelFormat::RGBA8);
    CHECK_EQ(image->rowStride(), std::size_t{16});
    CHECK_EQ(image->pixels().size(), std::size_t{32});
    // Row 0 is the top row of the file.
    checkPixel(*image, 0, 0, {255, 0, 0, 255});
    checkPixel(*image, 1, 0, {0, 255, 0, 128});
    checkPixel(*image, 3, 0, {255, 255, 255, 255});
    checkPixel(*image, 0, 1, {0, 0, 0, 255});
    checkPixel(*image, 1, 1, {10, 20, 30, 40});
    checkPixel(*image, 2, 1, {200, 100, 50, 255});
}

SCHUIMM_TEST(ImageDecoder_PreservesAlpha)
{
    const auto image = decodeAsset("rgba_4x2.png");
    REQUIRE(image.has_value());
    // Alpha values pass through unchanged, including partial and zero alpha, and colours are
    // not premultiplied: a fully transparent pixel keeps its colour.
    CHECK_EQ(alphaAt(*image, 1, 0), 128);
    CHECK_EQ(alphaAt(*image, 1, 1), 40);
    checkPixel(*image, 2, 0, {0, 0, 255, 0});
    checkPixel(*image, 3, 1, {1, 2, 3, 0});
}

SCHUIMM_TEST(ImageDecoder_OpaqueRgbGetsFullAlpha)
{
    const auto image = decodeAsset("rgb_3x1.png");
    REQUIRE(image.has_value());
    CHECK_EQ(image->width(), 3);
    CHECK_EQ(image->height(), 1);
    checkPixel(*image, 0, 0, {255, 0, 0, 255});
    checkPixel(*image, 1, 0, {0, 128, 0, 255});
    checkPixel(*image, 2, 0, {12, 34, 56, 255});
}

SCHUIMM_TEST(ImageDecoder_PaletteWithTransparency)
{
    const auto image = decodeAsset("palette_trns_2x2.png");
    REQUIRE(image.has_value());
    CHECK(image->format() == PixelFormat::RGBA8);
    checkPixel(*image, 0, 0, {255, 0, 0, 255});
    checkPixel(*image, 1, 0, {0, 255, 0, 128});
    CHECK_EQ(alphaAt(*image, 0, 1), 0); // fully transparent palette entry
    checkPixel(*image, 1, 1, {255, 0, 0, 255});
}

SCHUIMM_TEST(ImageDecoder_GreyscaleWithAlphaAndOneBit)
{
    const auto greyAlpha = decodeAsset("gray_alpha_2x1.png");
    REQUIRE(greyAlpha.has_value());
    checkPixel(*greyAlpha, 0, 0, {200, 200, 200, 100});
    checkPixel(*greyAlpha, 1, 0, {50, 50, 50, 255});

    const auto oneBit = decodeAsset("gray1_8x1.png");
    REQUIRE(oneBit.has_value());
    CHECK_EQ(oneBit->width(), 8);
    const int expected[8] = {255, 0, 255, 255, 0, 0, 255, 0}; // bits 10110010
    for (int x = 0; x < 8; ++x) {
        checkPixel(*oneBit, x, 0, {expected[x], expected[x], expected[x], 255});
    }
}

SCHUIMM_TEST(ImageDecoder_SixteenBitIsReducedToEightBit)
{
    const auto image = decodeAsset("rgba16_2x1.png");
    REQUIRE(image.has_value());
    CHECK(image->format() == PixelFormat::RGBA8);
    checkPixel(*image, 0, 0, {255, 0, 128, 255}, 1);
    checkPixel(*image, 1, 0, {0, 255, 0, 128}, 1);
}

SCHUIMM_TEST(ImageDecoder_Interlaced)
{
    const auto image = decodeAsset("interlaced_rgba_5x5.png");
    REQUIRE(image.has_value());
    CHECK_EQ(image->width(), 5);
    CHECK_EQ(image->height(), 5);
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            checkPixel(*image, x, y, {x * 50, y * 50, (x + y) * 25, 255 - x * 10 - y});
        }
    }
}

SCHUIMM_TEST(ImageDecoder_InvalidFiles)
{
    struct Case {
        const char* file;
        const char* why;
    };
    for (const Case& c : {Case{"not_an_image.png", "unrecognised"}, Case{"truncated.png", "truncated"},
                          Case{"corrupt_crc.png", "CRC"}}) {
        const auto image = decodeAsset(c.file);
        if (image.has_value()) {
            SCHUIMM_TEST_FAIL_(concat(c.file, " (", c.why, ") decoded unexpectedly"), false);
            continue;
        }
        CHECK(image.error().code == ErrorCode::InvalidData);
        // The message names the asset.
        CHECK(image.error().message.find(c.file) != std::string::npos);
    }

    const auto empty = decodeImage({}, "empty");
    REQUIRE(!empty.has_value());
    CHECK(empty.error().code == ErrorCode::InvalidData);

    // Only the PNG signature: valid start, nothing else.
    const std::array<std::byte, 8> signature = {std::byte{0x89}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'},
                                                std::byte{'\r'}, std::byte{'\n'}, std::byte{0x1A}, std::byte{'\n'}};
    const auto headerOnly = decodeImage(signature, "signature-only");
    REQUIRE(!headerOnly.has_value());
    CHECK(headerOnly.error().code == ErrorCode::InvalidData);
}

SCHUIMM_TEST(ImageDecoder_MissingAsset)
{
    const auto image = decodeAsset("does_not_exist.png");
    REQUIRE(!image.has_value());
    CHECK(image.error().code == ErrorCode::NotFound);
}

// --- Metadata (readImageInfo / loadImageInfo) ---

SCHUIMM_TEST(ImageInfo_MatchesDecodedSize)
{
    for (const char* file : {"rgba_4x2.png", "rgb_3x1.png", "palette_trns_2x2.png", "gray1_8x1.png",
                             "gray_alpha_2x1.png", "rgba16_2x1.png", "interlaced_rgba_5x5.png"}) {
        const AssetPath asset = assetPath(concat("Images/", file));
        const auto info = loadImageInfo(testAssetLocator(), asset);
        const auto image = loadImage(testAssetLocator(), asset);
        REQUIRE(info.has_value());
        REQUIRE(image.has_value());
        CHECK(info->format == ImageFileFormat::Png);
        if (info->size != image->size()) {
            SCHUIMM_TEST_FAIL_(concat(file, ": header size differs from the decoded size"), false);
        }
    }
}

SCHUIMM_TEST(ImageInfo_DoesNotNeedPixelData)
{
    // A complete header with garbage pixel data: the size is known, decoding fails.
    const auto header = pngHeader(640, 480);
    const auto info = readImageInfo(header, "header-only");
    REQUIRE(info.has_value());
    CHECK(info->size == (Extent2D{640, 480}));
    CHECK(!decodeImage(header, "header-only").has_value());
}

SCHUIMM_TEST(ImageInfo_InvalidHeaders)
{
    const auto checkInvalid = [](std::span<const std::byte> data, std::string_view name) {
        const auto info = readImageInfo(data, name);
        if (info.has_value()) {
            SCHUIMM_TEST_FAIL_(concat(name, " was accepted"), false);
            return;
        }
        CHECK(info.error().code == ErrorCode::InvalidData);
        CHECK(info.error().message.find(name) != std::string::npos);
    };
    checkInvalid({}, "empty");
    const auto notImage = testAssetLocator().readBytes(assetPath("Images/not_an_image.png"));
    REQUIRE(notImage.has_value());
    checkInvalid(*notImage, "not-an-image");

    const auto valid = pngHeader(4, 4);
    checkInvalid(std::span(valid).first(20), "truncated-header");
    auto badCrc = valid;
    badCrc[20] ^= std::byte{0x01};
    checkInvalid(badCrc, "bad-crc");
    checkInvalid(pngHeader(0, 4), "zero-width");
    const auto oversized = pngHeader(static_cast<std::uint32_t>(Image::maxDimension) + 1, 4);
    checkInvalid(oversized, "oversized");

    const auto missing = loadImageInfo(testAssetLocator(), assetPath("Images/does_not_exist.png"));
    REQUIRE(!missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
}
