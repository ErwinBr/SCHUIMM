#pragma once

// RoomResource: the immutable, authored description of a room, as read from a room resource file
// (*.room.json, format described in Documentation/Architecture/Rooms.md). Plain data: no
// mutable gameplay state, no loaded images, no script program. A RoomInstance (RoomInstance.h)
// adds the live state; the same RoomResource is shared by every instance of the room.
//
// Coordinates are room-space pixels: origin at the top-left corner of the room, x to the right,
// y down (the engine's convention, see Graphics.md). A room is `size.width` x `size.height`
// pixels.

#include "Core/Common/Geometry.h"
#include "Resource/AssetPath.h"
#include "Room/Facing.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::room {

// The newest room resource format version; this engine reads versions 1 to this one. Version 2
// added actor placements.
inline constexpr int roomResourceVersion = 2;

// A non-interactive image placed in the room. `position` is where `pivot` (in sprite pixels,
// from the sprite's top-left corner) lands in room space.
struct RoomProp {
    std::string id;
    AssetPath sprite;
    Vec2 position;
    Vec2 pivot;

    friend bool operator==(const RoomProp&, const RoomProp&) = default;
};

// An interactive object: what a script's "object <id>:" declaration refers to. Without a sprite
// it is an invisible object (a hotspot; its interaction area is a later milestone).
struct RoomObjectDefinition {
    std::string id;
    std::optional<AssetPath> sprite;
    Vec2 position;
    Vec2 pivot;

    friend bool operator==(const RoomObjectDefinition&, const RoomObjectDefinition&) = default;
};

struct ScalePoint {
    float y = 0.0f;
    float scale = 1.0f;

    friend bool operator==(ScalePoint, ScalePoint) = default;
};

// Character scale by ground position: at least two points with strictly increasing y. Only
// stored here; evaluating it is a later milestone.
struct ScaleMap {
    enum class Type { Linear };
    Type type = Type::Linear;
    std::vector<ScalePoint> points;

    friend bool operator==(const ScaleMap&, const ScaleMap&) = default;
};

// A walkable polygon (at least three points, no consecutive duplicates, non-zero area).
struct WalkableArea {
    std::string id;
    std::vector<Vec2> points;

    friend bool operator==(const WalkableArea&, const WalkableArea&) = default;
};

// Named geometry for future scripts and runtime systems; it declares no behaviour.
struct RoomRegion {
    enum class Shape { Rectangle, Polygon };
    std::string id;
    Shape shape = Shape::Rectangle;
    RectF rectangle;          // Shape::Rectangle: top-left corner and size.
    std::vector<Vec2> points; // Shape::Polygon.

    friend bool operator==(const RoomRegion&, const RoomRegion&) = default;
};

// Where and how an actor appears in this room: the initial state of the room's actor instance,
// applied every time the room is instantiated. `id` is room-local (another room may use the same
// one); `actor` names the reusable actor resource. Unset values come from the actor resource's
// defaults (visible: true).
struct ActorPlacement {
    std::string id;
    AssetPath actor;
    Vec2 position; // Ground point.
    std::optional<Facing> facing;
    std::optional<std::string> animation;
    std::optional<float> scale;
    std::optional<bool> visible;

    friend bool operator==(const ActorPlacement&, const ActorPlacement&) = default;
};

struct RoomResource {
    std::string id;
    Extent2D size;
    std::optional<AssetPath> script;     // Compiled room script (.schb), if any.
    std::optional<AssetPath> background; // Background image, if any.
    std::vector<RoomProp> props;
    std::vector<RoomObjectDefinition> objects;
    std::optional<ScaleMap> scaleMap;
    std::vector<WalkableArea> walkableAreas;
    std::vector<RoomRegion> regions;
    std::vector<ActorPlacement> actors; // Version 2.

    [[nodiscard]] const RoomObjectDefinition* findObject(std::string_view objectId) const noexcept
    {
        for (const RoomObjectDefinition& object : objects) {
            if (object.id == objectId) return &object;
        }
        return nullptr;
    }

    friend bool operator==(const RoomResource&, const RoomResource&) = default;
};

} // namespace schuimm::room
