#include "Room/RoomScript.h"

#include "Core/Common/StringBuilder.h"

namespace schuimm::room {

Result<std::shared_ptr<const script::Program>> loadRoomScript(script::ScriptHost& host, const AssetLocator& locator,
                                                             const RoomResource& room, std::string_view sourceName)
{
    if (!room.script) return std::shared_ptr<const script::Program>{};
    auto bytes = locator.readBytes(*room.script);
    if (!bytes) {
        return makeError(bytes.error().code, concat(sourceName, ": script: ", bytes.error().message));
    }
    auto program = host.load(*bytes, room.script->string());
    if (!program) {
        return makeError(program.error().code, concat(sourceName, ": script '", room.script->view(),
                                                      "' cannot be loaded: ", program.error().message));
    }
    if (auto status = validateRoomScript(room, program->get()); !status) {
        return makeError(status.error().code, concat(sourceName, ": ", status.error().message));
    }
    return std::move(*program);
}

Status validateRoomScript(const RoomResource& room, const script::Program* program)
{
    if (program == nullptr) return {};
    const script::Module& module = program->module();
    for (const script::ObjectEntry& object : module.objects) {
        const std::string& name = module.string(object.nameIndex);
        if (room.findObject(name) == nullptr) {
            return makeError(ErrorCode::InvalidData, concat("script '", program->name(), "' declares object '", name,
                                                            "', which room '", room.id, "' does not define"));
        }
    }
    return {};
}

} // namespace schuimm::room
