#include "Room/RoomLibrary.h"

#include "Core/Common/StringBuilder.h"
#include "Room/RoomResourceLoader.h"
#include "Room/RoomScript.h"

namespace schuimm::room {

RoomLibrary::RoomLibrary(const AssetLocator& locator, script::ScriptHost& host)
    : locator_(locator)
    , host_(host)
    , resources_(locator, loadRoomResource)
{
}

Result<LoadedRoom> RoomLibrary::load(const AssetPath& room)
{
    auto resource = resources_.load(ResourceId<RoomResource>(room));
    if (!resource) return std::unexpected(resource.error());
    LoadedRoom loaded{*resource, nullptr};
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

void RoomLibrary::unloadAll()
{
    resources_.unloadAll();
    scripts_.clear();
}

} // namespace schuimm::room
