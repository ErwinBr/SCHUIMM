#include "Room/RoomLibrary.h"

#include "Core/Common/StringBuilder.h"
#include "Room/ActorInstance.h"
#include "Room/RoomResourceLoader.h"
#include "Room/RoomScript.h"

namespace schuimm::room {

RoomLibrary::RoomLibrary(const AssetLocator& locator, script::ScriptHost& host)
    : locator_(locator)
    , host_(host)
    , resources_(locator, loadRoomResource)
    , actors_(locator)
{
}

Status validateRoomActors(const LoadedRoom& room)
{
    if (!room.resource) return makeError(ErrorCode::InvalidArgument, "no room resource");
    const auto& placements = room.resource->actors;
    if (room.actors.size() != placements.size()) {
        return makeError(ErrorCode::InvalidArgument, concat(placements.size(), " actor placements but ",
                                                            room.actors.size(), " actor resources"));
    }
    for (std::size_t i = 0; i < placements.size(); ++i) {
        const auto fail = [&](const Error& error) {
            return makeError(error.code, concat("actors[", i, "] ('", placements[i].id, "'): ", error.message));
        };
        if (!room.actors[i]) return fail(Error{ErrorCode::InvalidArgument, "no actor resource"});
        const ActorConfiguration configuration = placementConfiguration(placements[i], *room.actors[i]);
        if (auto clip = room.actors[i]->resolve(configuration.animation, configuration.facing); !clip) {
            return fail(clip.error());
        }
    }
    return {};
}

Result<LoadedRoom> RoomLibrary::load(const AssetPath& room)
{
    auto resource = resources_.load(ResourceId<RoomResource>(room));
    if (!resource) return std::unexpected(resource.error());
    LoadedRoom loaded{*resource, nullptr, {}, nullptr, {}, {}};
    if (auto status = loadImageSizes(room, loaded); !status) return std::unexpected(status.error());
    if (auto status = loadObjectAnimations(room, loaded); !status) return std::unexpected(status.error());
    if (auto status = loadActors(room, loaded); !status) return std::unexpected(status.error());
    if (auto navigation = navigationFor(room, *loaded.resource); !navigation) {
        return std::unexpected(navigation.error());
    } else {
        loaded.navigation = std::move(*navigation);
    }
    if (!loaded.resource->script) return loaded;

    const AssetPath& scriptPath = *loaded.resource->script;
    if (const auto cached = scripts_.find(scriptPath); cached != scripts_.end()) {
        // Shared by several rooms: each room's objects are checked against it.
        if (auto status = validateRoomScript(*loaded.resource, cached->second.get()); !status) {
            return makeError(status.error().code, concat(room.view(), ": ", status.error().message));
        }
        loaded.script = cached->second;
        return loaded;
    }
    auto program = loadRoomScript(host_, locator_, *loaded.resource, room.view());
    if (!program) return std::unexpected(program.error());
    loaded.script = *program;
    scripts_.emplace(scriptPath, std::move(*program));
    return loaded;
}

Status RoomLibrary::loadActors(const AssetPath& room, LoadedRoom& loaded)
{
    const auto& placements = loaded.resource->actors;
    loaded.actors.reserve(placements.size());
    for (std::size_t i = 0; i < placements.size(); ++i) {
        auto actor = actors_.load(placements[i].actor);
        if (!actor) {
            return makeError(actor.error().code, concat(room.view(), ": actors[", i, "] ('", placements[i].id,
                                                        "'): ", actor.error().message));
        }
        loaded.actors.push_back(std::move(*actor));
    }
    if (auto status = validateRoomActors(loaded); !status) {
        return makeError(status.error().code, concat(room.view(), ": ", status.error().message));
    }
    return {};
}

Status RoomLibrary::loadImageSizes(const AssetPath& room, LoadedRoom& loaded)
{
    const RoomResource& resource = *loaded.resource;
    const auto add = [&](const AssetPath& image, std::string_view path) -> Status {
        if (loaded.imageSizes.contains(image)) return {};
        auto size = actors_.imageSize(image);
        if (!size) return makeError(size.error().code, concat(room.view(), ": ", path, ": ", size.error().message));
        loaded.imageSizes.emplace(image, *size);
        return {};
    };
    if (resource.background) {
        if (auto status = add(*resource.background, "background.image"); !status) return status;
    }
    for (std::size_t i = 0; i < resource.props.size(); ++i) {
        if (auto status = add(resource.props[i].sprite, concat("props[", i, "].sprite")); !status) return status;
    }
    for (std::size_t i = 0; i < resource.objects.size(); ++i) {
        if (!resource.objects[i].sprite) continue;
        if (auto status = add(*resource.objects[i].sprite, concat("objects[", i, "].sprite")); !status) return status;
    }
    return {};
}

Status RoomLibrary::loadObjectAnimations(const AssetPath& room, LoadedRoom& loaded)
{
    const auto& objects = loaded.resource->objects;
    loaded.objectAnimations.assign(objects.size(), nullptr);
    const AnimationLoader load = [this](const AssetPath& path) { return actors_.loadAnimation(path); };
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (!objects[i].animation) continue;
        auto set = resolveObjectAnimations(*objects[i].animation, load);
        if (!set) {
            return makeError(set.error().code, concat(room.view(), ": objects[", i, "] ('", objects[i].id, "'): ",
                                                      set.error().message));
        }
        loaded.objectAnimations[i] = std::move(*set);
    }
    return {};
}

Result<std::shared_ptr<const navigation::NavigationMap>> RoomLibrary::navigationFor(const AssetPath& room,
                                                                                   const RoomResource& resource)
{
    if (resource.walkableAreas.empty()) return std::shared_ptr<const navigation::NavigationMap>();
    if (const auto cached = navigation_.find(room); cached != navigation_.end()) return cached->second;
    std::vector<std::vector<Vec2>> polygons;
    polygons.reserve(resource.walkableAreas.size());
    for (const WalkableArea& area : resource.walkableAreas) polygons.push_back(area.points);
    auto map = navigation::NavigationMap::create(std::move(polygons));
    if (!map) return makeError(map.error().code, concat(room.view(), ": navigation: ", map.error().message));
    navigation_.emplace(room, *map);
    return std::move(*map);
}

void RoomLibrary::unloadAll()
{
    resources_.unloadAll();
    scripts_.clear();
    navigation_.clear();
    actors_.unloadAll();
}

} // namespace schuimm::room
