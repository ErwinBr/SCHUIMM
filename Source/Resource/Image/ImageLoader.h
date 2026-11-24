#pragma once

#include "Core/Common/Error.h"
#include "Core/Image/Image.h"
#include "Resource/AssetLocator.h"
#include "Resource/AssetPath.h"

namespace schuimm {

// Reads an image asset and decodes it on the CPU. This is the loader for ResourceCache<Image>:
//
//   ResourceCache<Image> images(locator, loadImage);
//   auto hero = images.load(ResourceId<Image>::parse("sprites/hero.png").value());
//
// Errors are those of AssetLocator::readBytes (NotFound, PermissionDenied, IoFailure) and of
// decodeImage (InvalidData).
[[nodiscard]] Result<Image> loadImage(const AssetLocator& locator, const AssetPath& asset);

} // namespace schuimm
