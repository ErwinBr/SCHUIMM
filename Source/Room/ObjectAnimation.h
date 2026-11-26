#pragma once

// The resolved animations of an animated room object: each logical animation's clip, from
// animation resources loaded through the same caches as actors' (ActorLibrary). Immutable and
// shared by every instance of the room; each RoomObject plays it with its own AnimationPlayer.

#include "Animation/AnimationResource.h"
#include "Core/Common/Error.h"
#include "Resource/AssetPath.h"
#include "Room/RoomResource.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace schuimm::room {

struct ObjectAnimationSet {
    std::map<std::string, animation::AnimationClipRef, std::less<>> clips; // Logical name -> clip.
    std::string defaultAnimation;
    // The animation resources, for preparing textures (RoomTextures).
    std::vector<std::pair<AssetPath, std::shared_ptr<const animation::AnimationResource>>> resources;

    // nullptr if there is no such animation.
    [[nodiscard]] const animation::AnimationClipRef* find(std::string_view name) const noexcept
    {
        const auto found = clips.find(name);
        return found == clips.end() ? nullptr : &found->second;
    }
};

using AnimationLoader = std::function<Result<std::shared_ptr<const animation::AnimationResource>>(const AssetPath&)>;

// Loads the configuration's animation resources with `load` and resolves every mapping's clip.
// Errors name the mapping: "animations.spinning.clip: no clip 'spin' in 'Objects/Fan/fan.anim.json'".
[[nodiscard]] Result<std::shared_ptr<const ObjectAnimationSet>> resolveObjectAnimations(const ObjectAnimationConfig& config,
                                                                                       const AnimationLoader& load);

} // namespace schuimm::room
