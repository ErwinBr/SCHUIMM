#include "RoomGraphics/RoomRenderer.h"

#include "Core/Common/StringBuilder.h"

namespace schuimm::room {

Sprite makeSprite(const RoomSprite& roomSprite, const RoomTextures& textures, int layer)
{
    Sprite sprite;
    sprite.texture = textures.find(roomSprite.image);
    sprite.destination = roomSprite.destination;
    sprite.source = RectF{static_cast<float>(roomSprite.source.x), static_cast<float>(roomSprite.source.y),
                          static_cast<float>(roomSprite.source.width), static_cast<float>(roomSprite.source.height)};
    sprite.flipX = roomSprite.flipX;
    sprite.layer = layer;
    return sprite;
}

Status drawRoom(Renderer2D& renderer, const RoomDrawList& list, const RoomTextures& textures, int layer)
{
    const auto missing = [&textures](const RoomSprite& sprite) { return textures.find(sprite.image) == nullptr; };
    if (list.background && missing(*list.background)) {
        return makeError(ErrorCode::NotFound, concat("no texture prepared for '", list.background->image.view(), "'"));
    }
    for (const RoomDrawItem& item : list.items) {
        if (missing(item.sprite)) {
            return makeError(ErrorCode::NotFound, concat("no texture prepared for '", item.sprite.image.view(), "'"));
        }
    }
    if (list.background) renderer.drawSprite(makeSprite(*list.background, textures, layer));
    for (const RoomDrawItem& item : list.items) renderer.drawSprite(makeSprite(item.sprite, textures, layer));
    return {};
}

} // namespace schuimm::room
