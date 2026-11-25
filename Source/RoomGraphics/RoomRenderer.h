#pragma once

// Draws a room's draw list (RoomInstance::drawList(): visible actors, back to front) through
// the Renderer2D interface: one sprite per item, submitted in list order on one layer, so the
// order is deterministic. Read-only: drawing never advances animation. Coordinates are room
// coordinates; set the view (Renderer2D::setView, Camera2D) to show the room.

#include "Core/Common/Error.h"
#include "Graphics/Interface/Renderer2D.h"
#include "Graphics/Interface/Sprite.h"
#include "Room/ActorRenderState.h"
#include "RoomGraphics/RoomTextures.h"

namespace schuimm::room {

// The sprite for one actor frame (texture: nullptr if `textures` lacks the image).
[[nodiscard]] Sprite makeActorSprite(const ActorRenderState& state, const RoomTextures& textures, int layer = 0);

// Submits every item. NotFound (nothing submitted) if an image was not prepared.
[[nodiscard]] Status drawRoom(Renderer2D& renderer, const RoomDrawList& list, const RoomTextures& textures,
                              int layer = 0);

} // namespace schuimm::room
