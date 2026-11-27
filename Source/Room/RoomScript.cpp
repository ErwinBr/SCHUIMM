#include "Room/RoomScript.h"

#include "Core/Common/StringBuilder.h"

namespace schuimm::room {

namespace {

Status requireSource(const AssetPath& path, script::ScriptRole role, const script::Program* program, const char* what)
{
    if (program == nullptr) {
        return makeError(ErrorCode::InvalidState, concat(what, " '", path.view(), "' needs the game's program, and none is loaded"));
    }
    const script::Module& module = program->module();
    for (const script::SourceEntry& source : module.sources) {
        if (module.string(source.pathIndex) != path.view()) continue;
        if (source.role != role) {
            const auto article = [](script::ScriptRole r) { return r == script::ScriptRole::Actor ? "an " : "a "; };
            return makeError(ErrorCode::InvalidData, concat(what, " '", path.view(), "' is compiled into '", program->name(),
                                                            "' as ", article(source.role), toString(source.role),
                                                            " script, not as ", article(role), toString(role), " script"));
        }
        return {};
    }
    return makeError(ErrorCode::NotFound, concat(what, " '", path.view(), "' is not part of the program '", program->name(),
                                                 "'; recompile the game"));
}

} // namespace

Status validateRoomScript(const RoomResource& room, const script::Program* program)
{
    if (!room.script) return {};
    if (auto status = requireSource(*room.script, script::ScriptRole::Room, program, "script"); !status) return status;
    const script::Module& module = program->module();
    for (const script::BehaviourEntry& behaviour : module.behaviours) {
        if (behaviour.kind != script::BehaviourKind::Object || module.sourcePath(behaviour.source) != room.script->view()) {
            continue;
        }
        const std::string& name = module.string(behaviour.nameIndex);
        if (room.findObject(name) == nullptr) {
            return makeError(ErrorCode::InvalidData, concat("script '", room.script->view(), "' declares object '", name,
                                                            "', which room '", room.id, "' does not define"));
        }
    }
    return {};
}

Status validateActorScript(const ActorResource& actor, const script::Program* program)
{
    if (!actor.script) return {};
    return requireSource(*actor.script, script::ScriptRole::Actor, program, "script");
}

} // namespace schuimm::room
