#include "RoomGraphics/RoomRenderer.h"

#include "Core/Common/StringBuilder.h"

namespace schuimm::room {

Sprite makeActorSprite(const ActorRenderState& state, const RoomTextures& textures, int layer)
{
    Sprite sprite;
    sprite.texture = textures.find(state.image);
    sprite.destination = state.destination;
    sprite.source = RectF{static_cast<float>(state.source.x), static_cast<float>(state.source.y),
                          static_cast<float>(state.source.width), static_cast<float>(state.source.height)};
    sprite.flipX = state.flipX;
    sprite.layer = layer;
    return sprite;
}

Status drawRoom(Renderer2D& renderer, const RoomDrawList& list, const RoomTextures& textures, int layer)
{
    for (const RoomDrawItem& item : list) {
        if (textures.find(item.sprite.image) == nullptr) {
            return makeError(ErrorCode::NotFound,
                             concat("no texture prepared for '", item.sprite.image.view(), "'"));
        }
    }
    for (const RoomDrawItem& item : list) renderer.drawSprite(makeActorSprite(item.sprite, textures, layer));
    return {};
}

} // namespace schuimm::room
