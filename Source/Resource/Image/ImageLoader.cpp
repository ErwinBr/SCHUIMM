#include "Resource/Image/ImageLoader.h"

#include "Resource/Image/ImageDecoder.h"

namespace schuimm {

Result<Image> loadImage(const AssetLocator& locator, const AssetPath& asset)
{
    auto bytes = locator.readBytes(asset);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return decodeImage(*bytes, asset.view());
}

} // namespace schuimm
