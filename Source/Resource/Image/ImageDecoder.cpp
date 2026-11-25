#include "Resource/Image/ImageDecoder.h"

#include "Core/Common/StringBuilder.h"
#include "Resource/Image/PngDecoder.h"

#include <array>
#include <cstring>

namespace schuimm {

ImageFileFormat detectImageFileFormat(std::span<const std::byte> data) noexcept
{
    static constexpr std::array<unsigned char, 8> pngSignature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (data.size() >= pngSignature.size() && std::memcmp(data.data(), pngSignature.data(), pngSignature.size()) == 0) {
        return ImageFileFormat::Png;
    }
    return ImageFileFormat::Unknown;
}

Result<Image> decodeImage(std::span<const std::byte> data, std::string_view sourceName)
{
    if (data.empty()) {
        return makeError(ErrorCode::InvalidData, concat("cannot decode image '", sourceName, "': the data is empty"));
    }
    switch (detectImageFileFormat(data)) {
    case ImageFileFormat::Png: return detail::decodePng(data, sourceName);
    case ImageFileFormat::Unknown: break;
    }
    return makeError(ErrorCode::InvalidData,
                     concat("cannot decode image '", sourceName, "': unrecognised file format (supported: PNG)"));
}

Result<ImageInfo> readImageInfo(std::span<const std::byte> data, std::string_view sourceName)
{
    if (data.empty()) {
        return makeError(ErrorCode::InvalidData, concat("cannot read image '", sourceName, "': the data is empty"));
    }
    switch (detectImageFileFormat(data)) {
    case ImageFileFormat::Png: {
        auto size = detail::readPngSize(data, sourceName);
        if (!size) return std::unexpected(size.error());
        return ImageInfo{ImageFileFormat::Png, *size};
    }
    case ImageFileFormat::Unknown: break;
    }
    return makeError(ErrorCode::InvalidData,
                     concat("cannot read image '", sourceName, "': unrecognised file format (supported: PNG)"));
}

} // namespace schuimm
