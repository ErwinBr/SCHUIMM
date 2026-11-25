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
inline constexpr double maxActorScale = 1000.0;

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
    // Compiled actor script, checked to exist. Not loaded or executed yet (actor scripting is a
    // later milestone); kept so scripting can be connected without changing the format.
    std::optional<AssetPath> script;
    std::map<std::string, ActorAnimationResource, std::less<>> animationResources;
    std::map<std::string, ActorAnimation, std::less<>> animations;
    std::string defaultAnimation;
    Facing defaultFacing = Facing::South;
    float defaultScale = 1.0f;

    [[nodiscard]] bool hasAnimation(std::string_view name) const { return animations.find(name) != animations.end(); }

    // The clip for `animation` facing `facing` (the resolution order above). NotFound names the
    // actor, the animation and the facing.
    [[nodiscard]] Result<ResolvedActorClip> resolve(std::string_view animation, Facing facing) const;
};

} // namespace schuimm::room
