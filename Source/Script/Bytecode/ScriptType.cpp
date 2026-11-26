#include "Script/Bytecode/ScriptType.h"

namespace schuimm::script {

const char* toString(ScriptType type) noexcept
{
    switch (type) {
    case ScriptType::None: return "None";
    case ScriptType::Bool: return "bool";
    case ScriptType::Int: return "int";
    case ScriptType::Float: return "float";
    case ScriptType::String: return "string";
    case ScriptType::Any: return "any";
    case ScriptType::Object: return "object";
    case ScriptType::Actor: return "actor";
    case ScriptType::Entity: return "entity";
    case ScriptType::Vec2: return "vec2";
    case ScriptType::Enum: return "enum";
    case ScriptType::Coroutine: return "coroutine";
    case ScriptType::Operation: return "operation";
    }
    return "?";
}

std::optional<ScriptType> scriptTypeFromByte(std::uint8_t value) noexcept
{
    if (value > static_cast<std::uint8_t>(ScriptType::Operation)) {
        return std::nullopt;
    }
    return static_cast<ScriptType>(value);
}

} // namespace schuimm::script
