#pragma once

#include "Script/Bytecode/ScriptType.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace schuimm::script {

// Static types as the compiler sees them. Error marks an expression whose type could not be
// determined because of an earlier error; it is compatible with everything, so one mistake
// does not cascade into many diagnostics.
//
// Enum types are Type::EnumBase + i, where i indexes SemanticModel::enums() (and
// IrModule::enums). Named is only produced by the parser: a type written as a name that is not
// a built-in type, which the type checker resolves to an enum (or reports).
enum class Type : std::uint32_t {
    Error,
    None,
    Bool,
    Int,
    Float,
    String,
    Any,
    Object,    // A room object (the type of "self").
    Actor,
    Entity,    // An actor or a room object.
    Vec2,
    Coroutine,
    Operation,
    Named,
    EnumBase = 0x100,
};

inline constexpr std::uint32_t maxEnumTypes = 0x10000;

[[nodiscard]] constexpr bool isEnum(Type type) noexcept { return type >= Type::EnumBase; }
[[nodiscard]] constexpr std::uint32_t enumIndexOf(Type type) noexcept
{
    return static_cast<std::uint32_t>(type) - static_cast<std::uint32_t>(Type::EnumBase);
}
[[nodiscard]] constexpr Type enumType(std::uint32_t index) noexcept
{
    return static_cast<Type>(static_cast<std::uint32_t>(Type::EnumBase) + index);
}
[[nodiscard]] constexpr bool isReference(Type type) noexcept
{
    return type == Type::Object || type == Type::Actor || type == Type::Entity;
}

// The spelling of a built-in type; "enum" for enum types (callers that know the enum names
// print those instead).
[[nodiscard]] const char* toString(Type type) noexcept;
// Enum types keep their (compiler) index; the code generator renumbers them.
[[nodiscard]] TypeRef toTypeRef(Type type) noexcept;

// Built-in type names usable in annotations: int, float, bool, string, entity, actor, vec2,
// coroutine, operation (None and object are keywords; enums are resolved by the type checker).
[[nodiscard]] std::optional<Type> typeFromName(std::string_view name) noexcept;

[[nodiscard]] inline bool isNumeric(Type type) noexcept { return type == Type::Int || type == Type::Float; }

// Whether a value of type `from` may be stored where `to` is expected: identical types, the
// implicit int -> float and actor/object -> entity widenings, anything (but None) into Any, and
// Error either way.
[[nodiscard]] bool isAssignable(Type from, Type to) noexcept;

} // namespace schuimm::script
