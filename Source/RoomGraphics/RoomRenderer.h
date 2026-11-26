#pragma once

// Draws a room's draw list (RoomInstance::drawList()) through the Renderer2D interface: the
// background first, then one sprite per item in list order (back to front), all on one layer, so
// the order is deterministic. Read-only: drawing never advances movement or animation.
// Coordinates are room coordinates; set the view (Renderer2D::setView, Camera2D) to show the room.

#include "Core/Common/Error.h"
#include "Graphics/Interface/Renderer2D.h"
#include "Graphics/Interface/Sprite.h"
#include "Room/RoomDrawList.h"
#include "RoomGraphics/RoomTextures.h"

namespace schuimm::room {

// The sprite for one room sprite (texture: nullptr if `textures` lacks the image).
[[nodiscard]] Sprite makeSprite(const RoomSprite& sprite, const RoomTextures& textures, int layer = 0);

// Submits the background and every item. NotFound (nothing submitted) if an image was not prepared.
[[nodiscard]] Status drawRoom(Renderer2D& renderer, const RoomDrawList& list, const RoomTextures& textures,
                              int layer = 0);

} // namespace schuimm::room
