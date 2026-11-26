#pragma once

// Loads and caches actor resources together with what they reference: animation resources and
// the headers of their images (sizes, for checking source rectangles). Everything is immutable
// and shared: several actors may use one animation resource, several animation resources one
// image. No graphics context is involved; textures are made separately (RoomGraphics).
//
// The caches form a chain actor -> animation -> image header, so loading cannot recurse.
// Resources stay cached until unloadAll(); live actors keep what they use alive regardless.

#include "Animation/AnimationResource.h"
#include "Core/Common/Error.h"
#include "Resource/AssetLocator.h"
#include "Resource/AssetPath.h"
#include "Resource/Image/ImageDecoder.h"
#include "Resource/ResourceCache.h"
#include "Room/ActorResource.h"

#include <cstddef>
#include <memory>

namespace schuimm::room {

class ActorLibrary {
public:
    // `locator` must outlive the library.
    explicit ActorLibrary(const AssetLocator& locator);
    ActorLibrary(const ActorLibrary&) = delete;
    ActorLibrary& operator=(const ActorLibrary&) = delete;

    [[nodiscard]] Result<std::shared_ptr<const ActorResource>> load(const AssetPath& actor);
    [[nodiscard]] Result<std::shared_ptr<const animation::AnimationResource>> loadAnimation(const AssetPath& resource);

    // An image's size from its header, through the image-header cache (also used by rooms).
    [[nodiscard]] Result<Extent2D> imageSize(const AssetPath& image);

    void unloadAll();

    [[nodiscard]] std::size_t actorCount() const noexcept { return actors_.size(); }
    [[nodiscard]] std::size_t animationCount() const noexcept { return animations_.size(); }
    [[nodiscard]] std::size_t imageCount() const noexcept { return images_.size(); }

private:
    ResourceCache<ImageInfo> images_;
    ResourceCache<animation::AnimationResource> animations_;
    ResourceCache<ActorResource> actors_;
};

} // namespace schuimm::room
