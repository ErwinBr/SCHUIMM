#include "RoomGraphics/RoomTextures.h"

#include "Core/Common/StringBuilder.h"

namespace schuimm::room {

Result<RoomTextures> RoomTextures::prepare(TextureCache& textures, const LoadedRoom& room, TextureSettings settings)
{
    RoomTextures prepared;
    for (const auto& actor : room.actors) {
        if (!actor) continue;
        for (const auto& [name, animationResource] : actor->animationResources) {
            for (const animation::AnimationClip& clip : animationResource.resource->clips) {
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
                                         concat(animationResource.path.view(), ": clips.", clip.name, ".frames[", i,
                                                "]: source rectangle does not fit the ", size.width, "x", size.height,
                                                " texture of '", frame.image.view(), "'"));
                    }
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
