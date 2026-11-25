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
    LoadedRoom loaded{*resource, nullptr, {}};
    if (auto status = loadActors(room, loaded); !status) return std::unexpected(status.error());
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

void RoomLibrary::unloadAll()
{
    resources_.unloadAll();
    scripts_.clear();
    actors_.unloadAll();
}

} // namespace schuimm::room
