#pragma once

// What a room instance draws, in drawing order, without any graphics API (RoomGraphics turns it
// into sprites). Built by RoomInstance::drawList(), which only reads the instance.
//
//   * The background, if the room has one, at the room origin, behind everything.
//   * Every prop, every visible room object with a sprite, and every visible actor, sorted back to
//     front by depth (their ground y: lower on screen is in front) and, for equal depths, by
//     sequence: props, then objects, then actors, each in the order of the room resource.
//
// Props and objects are placed with their pivot on their position (Rooms.md); actors with their
// current frame's pivot on their ground point, at their effective scale (ActorRenderState.h).

#include "Core/Common/Geometry.h"
#include "Resource/AssetPath.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace schuimm::room {

// One image region drawn into a room-space rectangle.
struct RoomSprite {
    AssetPath image;
    RectI source;      // Image pixels.
    RectF destination; // Room coordinates.
    bool flipX = false;
};

enum class RoomDrawKind { Prop, Object, Actor };

struct RoomDrawItem {
    RoomDrawKind kind = RoomDrawKind::Prop;
    float depth = 0.0f;       // Ground y.
    std::size_t sequence = 0; // Tie-breaker (see above).
    std::size_t index = 0;    // Into the room's props, objects or actors.
    RoomSprite sprite;
};

struct RoomDrawList {
    std::optional<RoomSprite> background;
    std::vector<RoomDrawItem> items; // Back to front.
};

} // namespace schuimm::room
