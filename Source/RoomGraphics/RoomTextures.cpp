#include "RoomGraphics/RoomTextures.h"

#include "Core/Common/StringBuilder.h"

#include <utility>
#include <vector>

namespace schuimm::room {

Result<RoomTextures> RoomTextures::prepare(TextureCache& textures, const LoadedRoom& room, TextureSettings settings)
{
    RoomTextures prepared;
    // Background, props and objects: their size must match the header size they are placed with.
    for (const auto& [image, size] : room.imageSizes) {
        auto texture = textures.load(ResourceId<Image>(image), settings);
        if (!texture) return std::unexpected(texture.error());
        if ((*texture)->size() != size) {
            return makeError(ErrorCode::InvalidData, concat("texture of '", image.view(), "' is ", (*texture)->size().width,
                                                            "x", (*texture)->size().height, ", but its header said ",
                                                            size.width, "x", size.height));
        }
        prepared.textures_.emplace(image, std::move(*texture));
    }
    // Every clip of every animation resource an actor or animated object uses.
    std::vector<std::pair<const AssetPath*, const animation::AnimationResource*>> resources;
    for (const auto& actor : room.actors) {
        if (!actor) continue;
        for (const auto& [name, animationResource] : actor->animationResources) {
            resources.emplace_back(&animationResource.path, animationResource.resource.get());
        }
    }
    for (const auto& set : room.objectAnimations) {
        if (!set) continue;
        for (const auto& [path, resource] : set->resources) resources.emplace_back(&path, resource.get());
    }
    for (const auto& [path, resource] : resources) {
        for (const animation::AnimationClip& clip : resource->clips) {
            for (std::size_t i = 0; i < clip.frames.size(); ++i) {
                const animation::AnimationFrame& frame = clip.frames[i];
                auto& slot = prepared.textures_[frame.image];
                if (!slot) {
                    auto texture = textures.load(ResourceId<Image>(frame.image), settings);
                    if (!texture) return std::unexpected(texture.error());
                    slot = std::move(*texture);
                }
                const Extent2D size = slot->size();
                const RectI& source = frame.source;
                if (source.x + source.width > size.width || source.y + source.height > size.height) {
                    return makeError(ErrorCode::InvalidData,
                                     concat(path->view(), ": clips.", clip.name, ".frames[", i,
                                            "]: source rectangle does not fit the ", size.width, "x", size.height,
                                            " texture of '", frame.image.view(), "'"));
                }
            }
        }
    }
    return prepared;
}

const Texture* RoomTextures::find(const AssetPath& image) const noexcept
{
    const auto found = textures_.find(image);
    return found == textures_.end() ? nullptr : found->second.get();
}

} // namespace schuimm::room
