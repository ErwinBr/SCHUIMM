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

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::room {

// The room resource format version this engine reads (the only one: formats are provisional
// before release, see Documentation/Architecture/ResourceFormats.md).
inline constexpr int roomResourceVersion = 1;

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
// A logical animation of a room object: one clip of one of its animation resources. Objects
// have no facings, so there are no directional variants.
struct ObjectClipMapping {
    std::string resource; // Name in ObjectAnimationConfig::resources.
    std::string clip;

    friend bool operator==(const ObjectClipMapping&, const ObjectClipMapping&) = default;
};

// The animation configuration of an animated room object, as authored. The clips are resolved
// when the room is loaded (LoadedRoom::objectAnimations).
struct ObjectAnimationConfig {
    std::map<std::string, AssetPath, std::less<>> resources;         // Name -> .anim.json.
    std::map<std::string, ObjectClipMapping, std::less<>> animations; // Logical name -> clip.
    std::string defaultAnimation;                                     // Plays when the room starts.

    friend bool operator==(const ObjectAnimationConfig&, const ObjectAnimationConfig&) = default;
};

// A room object's visual is one of three, chosen by the resource:
//   * a static sprite (`sprite`, placed with `pivot`);
//   * an animation configuration (`animation`; each frame carries its own pivot);
//   * nothing: an invisible hotspot.
// A definition never has both a sprite and an animation configuration; the loader rejects it.
// This constrains the definition, not what scripts may show at run time.
struct RoomObjectDefinition {
    std::string id;
    std::optional<AssetPath> sprite;
    std::optional<ObjectAnimationConfig> animation;
    Vec2 position;
    Vec2 pivot;

    friend bool operator==(const RoomObjectDefinition&, const RoomObjectDefinition&) = default;
};

struct ScalePoint {
    float y = 0.0f;
    float scale = 1.0f;

    friend bool operator==(ScalePoint, ScalePoint) = default;
};

// Scales of a scale map's points.
inline constexpr double minScaleMapScale = 0.001;
inline constexpr double maxScaleMapScale = 1000.0;

// Perspective scale by ground position: at least two points with strictly increasing y (as
// stored), each scale in [minScaleMapScale, maxScaleMapScale]. Evaluated by evaluateScaleMap
// (Room/Perspective.h): piecewise linear, clamped beyond the first and last point.
struct ScaleMap {
    enum class Type { Linear };
    Type type = Type::Linear;
    std::vector<ScalePoint> points;

    friend bool operator==(const ScaleMap&, const ScaleMap&) = default;
};

// A walkable polygon: simple (Navigation/Polygon.h) and inside the room.
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
    std::optional<float> scale;     // Base scale.
    std::optional<bool> visible;
    std::optional<float> walkSpeed; // Overrides the actor resource's movement speed.

    friend bool operator==(const ActorPlacement&, const ActorPlacement&) = default;
};

struct RoomResource {
    std::string id;
    Extent2D size;
    std::optional<AssetPath> script;     // The room's script source (.sch), if any (a source of the game program).
    std::optional<AssetPath> background; // Background image, if any.
    std::vector<RoomProp> props;
    std::vector<RoomObjectDefinition> objects;
    std::optional<ScaleMap> scaleMap;
    std::vector<WalkableArea> walkableAreas;
    std::vector<RoomRegion> regions;
    std::vector<ActorPlacement> actors;

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
