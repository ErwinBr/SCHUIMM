#pragma once

// Loads rooms (resource, compiled script, actor resources) once and shares them: every
// RoomInstance of a room uses the same immutable RoomResource, Program and ActorResources.

#include "Core/Common/Error.h"
#include "Resource/AssetLocator.h"
#include "Resource/AssetPath.h"
#include "Navigation/NavigationMap.h"
#include "Resource/ResourceCache.h"
#include "Room/ActorLibrary.h"
#include "Room/ActorResource.h"
#include "Room/ObjectAnimation.h"
#include "Room/RoomResource.h"
#include "Script/Runtime/Program.h"
#include "Script/Runtime/ScriptHost.h"

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

namespace schuimm::room {

// A loaded, validated room: its resource, its compiled script (nullptr if it has none) and the
// actor resource of each actor placement (actors[i] for resource->actors[i]). The script's
// object declarations have been checked against the resource, and every placement's initial
// animation and facing resolve. Cheap to copy.
struct LoadedRoom {
    std::shared_ptr<const RoomResource> resource;
    std::shared_ptr<const script::Program> script;
    std::vector<std::shared_ptr<const ActorResource>> actors;
    // Built once per room resource from its walkable areas; null if it has none.
    std::shared_ptr<const navigation::NavigationMap> navigation;
    // Sizes of the background, prop and object images (from their headers), for placing them.
    std::map<AssetPath, Extent2D> imageSizes;
    // Per object (objectAnimations[i] for resource->objects[i]): its resolved animations, or
    // null for a static or invisible object.
    std::vector<std::shared_ptr<const ObjectAnimationSet>> objectAnimations;
};

// Checks that `room.actors` matches the placements and that each placement's configuration is
// valid for its actor resource. Messages: "actors[i] ('id'): ...".
[[nodiscard]] Status validateRoomActors(const LoadedRoom& room);

// Caches room resources (ResourceCache<RoomResource>) and their programs (by script asset path,
// so rooms that share a script share the program). Programs are linked against `host`'s natives,
// so register the game's natives before loading rooms. `locator` and `host` must outlive the
// library. Unloading drops the library's references only: instances keep what they use.
class RoomLibrary {
public:
    RoomLibrary(const AssetLocator& locator, script::ScriptHost& host);

    RoomLibrary(const RoomLibrary&) = delete;
    RoomLibrary& operator=(const RoomLibrary&) = delete;

    // Reads, validates and verifies the room resource, then loads and validates its script and
    // its actor resources (through actors()).
    [[nodiscard]] Result<LoadedRoom> load(const AssetPath& room);

    void unloadAll();
    [[nodiscard]] std::size_t resourceCount() const noexcept { return resources_.size(); }
    [[nodiscard]] std::size_t scriptCount() const noexcept { return scripts_.size(); }
    // The actor, animation and image-header caches (unloadAll() also clears them).
    [[nodiscard]] ActorLibrary& actors() noexcept { return actors_; }
    [[nodiscard]] const ActorLibrary& actors() const noexcept { return actors_; }

private:
    Status loadActors(const AssetPath& room, LoadedRoom& loaded);
    Status loadImageSizes(const AssetPath& room, LoadedRoom& loaded);
    Status loadObjectAnimations(const AssetPath& room, LoadedRoom& loaded);
    Result<std::shared_ptr<const navigation::NavigationMap>> navigationFor(const AssetPath& room,
                                                                          const RoomResource& resource);

    const AssetLocator& locator_;
    script::ScriptHost& host_;
    ResourceCache<RoomResource> resources_;
    std::map<AssetPath, std::shared_ptr<const script::Program>> scripts_;
    std::map<AssetPath, std::shared_ptr<const navigation::NavigationMap>> navigation_;
    ActorLibrary actors_;
};

} // namespace schuimm::room
