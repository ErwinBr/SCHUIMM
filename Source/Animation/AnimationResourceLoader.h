#pragma once

// Animation resources (*.anim.json), format version 1:
//
//   { "version": 1,
//     "clips": {
//       "idle_south": { "loop": true, "frames": [
//         { "image": "Actors/Hero/idle.png", "source": [0, 0, 128, 256], "pivot": [64, 250], "duration": 12 } ] } } }
//
//   * clips: object, at least one clip; names are identifiers ([A-Za-z_][A-Za-z0-9_]*).
//   * loop: required boolean.
//   * frames: 1 to maxFramesPerClip frames.
//   * image: asset path of the frame's image (PNG).
//   * source: optional [x, y, width, height] in image pixels, inside the image; omitted, the
//     whole image.
//   * pivot: optional [x, y] relative to the source rectangle, default [0, 0]; finite, within
//     2^20 pixels; may lie outside the frame.
//   * duration: integer ticks, 1 to maxFrameDuration; a clip's total at most maxClipDuration.
//
// Unknown properties, duplicate keys and other versions are errors (InvalidData, "<source>:
// <path>: <reason>"). Documentation/Architecture/Animation.md describes the format.

#include "Animation/AnimationResource.h"
#include "Core/Common/Error.h"
#include "Core/Common/Geometry.h"
#include "Resource/AssetLocator.h"
#include "Resource/AssetPath.h"
#include "Resource/Image/ImageDecoder.h"
#include "Resource/ResourceCache.h"

#include <cstddef>
#include <functional>
#include <span>
#include <string_view>

namespace schuimm::animation {

// The size of an image, for checking source rectangles and completing omitted ones.
using ImageSizeFunction = std::function<Result<Extent2D>(const AssetPath& image)>;

// Parses and validates a resource. `imageSize` is asked once per distinct image; its errors are
// returned with the frame's property path.
[[nodiscard]] Result<AnimationResource> decodeAnimationResource(std::span<const std::byte> data,
                                                                std::string_view sourceName,
                                                                const ImageSizeFunction& imageSize);

// Reads `asset` (at most 16 MiB) and decodes it, taking image sizes from `images` (image headers
// only: loadImageInfo). No graphics context is involved.
[[nodiscard]] Result<AnimationResource> loadAnimationResource(const AssetLocator& locator, const AssetPath& asset,
                                                              ResourceCache<ImageInfo>& images);

} // namespace schuimm::animation
