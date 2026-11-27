#pragma once

// Loads rooms (resource, actor resources, navigation) once and shares them: every RoomInstance
// of a room uses the same immutable RoomResource, game Program and ActorResources.

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

// A loaded, validated room: its resource, the game program holding its scripts (nullptr if
// neither the room nor any of its actors has a script) and the actor resource of each actor
// placement (actors[i] for resource->actors[i]). The room's and actors' scripts are sources of
// the program in the right role, the room script's object declarations have been checked
// against the resource, and every placement's initial animation and facing resolve. Cheap to
// copy.
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

// Caches room resources (ResourceCache<RoomResource>), actor resources and navigation maps. The
// rooms' and actors' scripts are behaviours of `program`, the game's one compiled program
// (loaded and linked by the caller, e.g. AdventureRuntime); a library without a program loads
// only rooms and actors without scripts. `locator` must outlive the library. Unloading drops the
// library's references only: instances keep what they use.
class RoomLibrary {
public:
    explicit RoomLibrary(const AssetLocator& locator, std::shared_ptr<const script::Program> program = {});

    RoomLibrary(const RoomLibrary&) = delete;
    RoomLibrary& operator=(const RoomLibrary&) = delete;

    // Reads, validates and verifies the room resource, loads its actor resources (through
    // actors()) and checks the room's and actors' scripts against the program.
    [[nodiscard]] Result<LoadedRoom> load(const AssetPath& room);

    void unloadAll();
    [[nodiscard]] std::size_t resourceCount() const noexcept { return resources_.size(); }
    [[nodiscard]] const std::shared_ptr<const script::Program>& program() const noexcept { return program_; }
    // The actor, animation and image-header caches (unloadAll() also clears them).
    [[nodiscard]] ActorLibrary& actors() noexcept { return actors_; }
    [[nodiscard]] const ActorLibrary& actors() const noexcept { return actors_; }

private:
    Status loadActors(const AssetPath& room, LoadedRoom& loaded);
    Status loadImageSizes(const AssetPath& room, LoadedRoom& loaded);
    Status loadObjectAnimations(const AssetPath& room, LoadedRoom& loaded);
    Result<std::shared_ptr<const navigation::NavigationMap>> navigationFor(const AssetPath& room,
                                                                          const RoomResource& resource);

    std::shared_ptr<const script::Program> program_;
    ResourceCache<RoomResource> resources_;
    std::map<AssetPath, std::shared_ptr<const navigation::NavigationMap>> navigation_;
    ActorLibrary actors_;
};

} // namespace schuimm::room
