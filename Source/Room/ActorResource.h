#pragma once

// Immutable, reusable actor definitions (*.actor.json): what an actor is and how it looks. Any
// number of actor instances, in any rooms, share one ActorResource; their state is their own
// (ActorInstance.h).
//
// The resource identifier (`id`) names the definition ("guard"). It is not an instance
// identifier: a room names each placement itself ("guard_01", "guard_02").
//
// Logical animations ("idle", "walk", ...) are names chosen by the designer; nothing in the
// engine knows any of them. Each maps facings to clips of the actor's animation resources (the
// clips are referenced, not copied):
//
//   * Directional: one mapping per facing that has artwork, plus an optional `fallback` facing
//     whose mapping is used for facings without one.
//   * Direction-independent: one mapping (`any`) for every facing.
//
// A mapping may mirror its clip horizontally (flip_x). Resolution of (animation, facing):
//   1. the explicit mapping for that facing;
//   2. the direction-independent mapping;
//   3. the mapping of the fallback facing;
//   4. otherwise an error: nothing else is ever chosen silently.

#include "Animation/AnimationResource.h"
#include "Core/Common/Error.h"
#include "Resource/AssetPath.h"
#include "Room/Facing.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::room {

inline constexpr int actorResourceVersion = 1;
// Base scales (default_scale, placement scale, setBaseScale). With a scale map's [0.001, 1000]
// the effective scale stays within [1e-6, 1e6]: finite, and never zero.
inline constexpr double minActorScale = 0.001;
inline constexpr double maxActorScale = 1000.0;
// Walking speeds, room pixels per second at perspective scale 1.
inline constexpr double minWalkSpeed = 0.001;
inline constexpr double maxWalkSpeed = 100000.0;

// How an actor walks (absent for decorative actors, which then cannot walk). Animation names are
// the designer's: nothing in the engine knows "walk" or "idle".
struct ActorMovementConfig {
    float speed = 100.0f;      // Room pixels per second at perspective scale 1.
    std::string walkAnimation; // Played while walking, in the walking direction.
    std::string idleAnimation; // Selected when a walk completes or is cancelled.
    bool perspectiveSpeed = true; // Speed times the room's perspective scale at the actor's feet.

    friend bool operator==(const ActorMovementConfig&, const ActorMovementConfig&) = default;
};

struct ActorClipMapping {
    std::string resource; // Name in ActorResource::animationResources.
    std::string clipName;
    bool flipX = false;
    animation::AnimationClipRef clip; // Resolved when the resource is loaded.
};

struct ActorAnimation {
    std::array<std::optional<ActorClipMapping>, facingCount> directions;
    std::optional<ActorClipMapping> any;
    std::optional<Facing> fallback; // Has an explicit mapping in `directions`.
};

struct ActorAnimationResource {
    AssetPath path;
    std::shared_ptr<const animation::AnimationResource> resource;
};

struct ResolvedActorClip {
    animation::AnimationClipRef clip;
    bool flipX = false;
};

struct ActorResource {
    std::string id;
    // The actor's script source (.sch): its "actor:" declaration is the behaviour of every actor
    // using this resource, found in the game program by this path.
    std::optional<AssetPath> script;
    std::map<std::string, ActorAnimationResource, std::less<>> animationResources;
    std::map<std::string, ActorAnimation, std::less<>> animations;
    std::string defaultAnimation;
    Facing defaultFacing = Facing::South;
    float defaultScale = 1.0f; // Base scale.
    // Whether the room's scale map scales the actor (its effective scale is base x perspective).
    bool perspectiveScale = true;
    std::optional<ActorMovementConfig> movement;

    [[nodiscard]] bool hasAnimation(std::string_view name) const { return animations.find(name) != animations.end(); }

    // The clip for `animation` facing `facing` (the resolution order above). NotFound names the
    // actor, the animation and the facing.
    [[nodiscard]] Result<ResolvedActorClip> resolve(std::string_view animation, Facing facing) const;
};

} // namespace schuimm::room
