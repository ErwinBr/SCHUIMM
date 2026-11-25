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
    }
    return "?";
}

std::optional<ScriptType> scriptTypeFromByte(std::uint8_t value) noexcept
{
    if (value > static_cast<std::uint8_t>(ScriptType::Object)) {
        return std::nullopt;
    }
    return static_cast<ScriptType>(value);
}

} // namespace schuimm::script
