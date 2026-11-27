#include "Script/Bytecode/Module.h"

namespace schuimm::script {

const char* toString(ScriptRole role) noexcept
{
    switch (role) {
    case ScriptRole::Global: return "global";
    case ScriptRole::Room: return "room";
    case ScriptRole::Actor: return "actor";
    }
    return "?";
}

const char* toString(BehaviourKind kind) noexcept
{
    switch (kind) {
    case BehaviourKind::Room: return "room";
    case BehaviourKind::Actor: return "actor";
    case BehaviourKind::Object: return "object";
    }
    return "?";
}

} // namespace schuimm::script
