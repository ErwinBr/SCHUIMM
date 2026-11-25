#pragma once

// The GPU textures a room needs to draw its actors: one per image used by any clip of the room's
// actor animation resources, made through the engine's TextureCache (so images shared by several
// actors, clips or rooms get one texture). Prepared once when the room is loaded, never during
// playback: changing frames only changes which texture region is drawn.
//
// After preparing, the decoded images in the TextureCache's image cache are no longer needed and
// may be unloaded. The textures are shared: destroying a RoomTextures (or the room) never
// invalidates a texture still used elsewhere. Like every texture, they must be released before
// the GraphicsDevice.

#include "Core/Common/Error.h"
#include "Graphics/Interface/Texture.h"
#include "GraphicsResources/TextureCache.h"
#include "Resource/AssetPath.h"
#include "Room/RoomLibrary.h"

#include <cstddef>
#include <map>
#include <memory>

namespace schuimm::room {

class RoomTextures {
public:
    RoomTextures() = default;

    // Loads every texture `room`'s actors can show and checks that each frame's source rectangle
    // lies inside its texture (the image may have changed since its header was read).
    [[nodiscard]] static Result<RoomTextures> prepare(TextureCache& textures, const LoadedRoom& room,
                                                      TextureSettings settings = {});

    // nullptr if `image` was not prepared.
    [[nodiscard]] const Texture* find(const AssetPath& image) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return textures_.size(); }

private:
    std::map<AssetPath, std::shared_ptr<const Texture>> textures_;
};

} // namespace schuimm::room
