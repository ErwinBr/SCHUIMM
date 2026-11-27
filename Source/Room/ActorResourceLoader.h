#pragma once

// Actor resources (*.actor.json), format version 1:
//
//   { "version": 1,
//     "id": "hero",
//     "script": "Actors/Hero/Hero.sch",                           (optional)
//     "animation_resources": { "main": "Actors/Hero/hero.anim.json" },
//     "animations": {
//       "idle": { "south": { "resource": "main", "clip": "idle_south" },
//                 "west":  { "resource": "main", "clip": "idle_west" },
//                 "east":  { "resource": "main", "clip": "idle_west", "flip_x": true },
//                 "fallback": "south" },
//       "glow": { "any": { "resource": "main", "clip": "glow" } } },
//     "default_animation": "idle",
//     "default_facing": "south",
//     "default_scale": 1.0 }                                      (optional, default 1)
//
//   * id, resource and animation names: identifiers ([A-Za-z_][A-Za-z0-9_]*).
//   * animation_resources: at least one; each is loaded and its clips referenced, not copied.
//   * animations: at least one. Each has either "any" alone, or one or more of the eight facing
//     names plus an optional "fallback" naming one of its mapped facings.
//   * A mapping: { "resource", "clip", "flip_x" (optional, default false) }; the clip must
//     exist in that resource.
//   * script: the actor's script source (.sch): the identity of its behaviour in the game's
//     compiled program (the build compiles it; the runtime needs no source file).
//   * default_animation must resolve with default_facing.
//   * default_scale: positive, finite, at most maxActorScale, positive as a float.
//
// Unknown properties, duplicate keys and other versions are errors. Errors: InvalidData
// "<source>: <path>: <reason>"; a missing referenced asset keeps its code (NotFound).
// Documentation/Architecture/Actors.md describes the format.

#include "Animation/AnimationResource.h"
#include "Core/Common/Error.h"
#include "Resource/AssetLocator.h"
#include "Resource/AssetPath.h"
#include "Room/ActorResource.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace schuimm::room {

using AnimationResolver =
    std::function<Result<std::shared_ptr<const animation::AnimationResource>>(const AssetPath& resource)>;
[[nodiscard]] Result<ActorResource> decodeActorResource(std::span<const std::byte> data, std::string_view sourceName,
                                                        const AnimationResolver& animations);

// Reads `asset` (at most 16 MiB), resolving animation resources with `animations`.
[[nodiscard]] Result<ActorResource> loadActorResource(const AssetLocator& locator, const AssetPath& asset,
                                                      const AnimationResolver& animations);

// NotFound unless `asset` exists and is a regular file. Messages: "<source>: <path>: ...".
[[nodiscard]] Status checkAssetExists(const AssetLocator& locator, const AssetPath& asset, std::string_view sourceName,
                                      std::string_view path);

} // namespace schuimm::room
