#pragma once

#include "Script/Bytecode/ScriptType.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace schuimm::script {

// Static types as the compiler sees them. Error marks an expression whose type could not be
// determined because of an earlier error; it is compatible with everything, so one mistake
// does not cascade into many diagnostics.
enum class Type : std::uint8_t { Error, None, Bool, Int, Float, String, Any };

[[nodiscard]] const char* toString(Type type) noexcept;
[[nodiscard]] ScriptType toScriptType(Type type) noexcept;
[[nodiscard]] Type fromScriptType(ScriptType type) noexcept;

// Type names usable in annotations: int, float, bool, string (None is a keyword).
[[nodiscard]] std::optional<Type> typeFromName(std::string_view name) noexcept;

[[nodiscard]] inline bool isNumeric(Type type) noexcept { return type == Type::Int || type == Type::Float; }

// Whether a value of type `from` may be stored where `to` is expected: identical types, the
// implicit int -> float widening, anything (but None) into Any, and Error either way.
[[nodiscard]] bool isAssignable(Type from, Type to) noexcept;

} // namespace schuimm::script
