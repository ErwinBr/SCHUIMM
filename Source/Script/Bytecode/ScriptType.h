#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace schuimm::script {

// Value types shared by the compiler, the bytecode format and the VM. The numeric values are
// part of the bytecode format (signatures, register types) and must not change within a format
// major version.
enum class ScriptType : std::uint8_t {
    None = 0,      // "no value": the return type of functions without a result.
    Bool = 1,
    Int = 2,       // Signed 64-bit integer.
    Float = 3,     // IEEE 754 binary64.
    String = 4,    // Immutable UTF-8 text.
    Any = 5,       // Native parameters only: accepts any value except None (e.g. print).
    Object = 6,    // Reference to a live interactive room object (ObjectRef).
    Actor = 7,     // Reference to a live actor (ObjectRef).
    Entity = 8,    // Declared type only: an actor or an object reference (values are Actor or Object).
    Vec2 = 9,      // Two floats (x, y), a value type.
    Enum = 10,     // A value of a scoped enum; the enum is identified by TypeRef::enumIndex.
    Coroutine = 11, // Reference to one coroutine invocation.
    Operation = 12, // Reference to one engine operation (e.g. an animation playback).
};

[[nodiscard]] const char* toString(ScriptType type) noexcept;

// Decodes a serialised type; nullopt for unknown values.
[[nodiscard]] std::optional<ScriptType> scriptTypeFromByte(std::uint8_t value) noexcept;

// References to engine entities: actor and object values, and the entity type both widen to.
[[nodiscard]] constexpr bool isEntityReference(ScriptType type) noexcept
{
    return type == ScriptType::Object || type == ScriptType::Actor || type == ScriptType::Entity;
}

// A type as it appears in signatures and register tables: the kind, and for Enum the index of
// the enum in the module's enum table (Module::enums).
struct TypeRef {
    ScriptType kind = ScriptType::None;
    std::uint16_t enumIndex = 0;

    constexpr TypeRef() noexcept = default;
    constexpr TypeRef(ScriptType type) noexcept // NOLINT(google-explicit-constructor): a plain kind.
        : kind(type)
    {
    }
    constexpr TypeRef(ScriptType type, std::uint16_t index) noexcept
        : kind(type)
        , enumIndex(type == ScriptType::Enum ? index : 0)
    {
    }

    [[nodiscard]] static constexpr TypeRef enumType(std::uint16_t index) noexcept { return {ScriptType::Enum, index}; }
    friend constexpr bool operator==(const TypeRef&, const TypeRef&) noexcept = default;
};

// Whether a value statically typed `from` may be stored where `to` is declared, without any
// conversion instruction: identical types, an actor or object reference into entity, and
// anything but None into Any. (int -> float is an explicit IntToFloat in bytecode.)
// Types whose values include null: entity references, coroutines and operations.
[[nodiscard]] constexpr bool isNullableType(ScriptType type) noexcept
{
    return isEntityReference(type) || type == ScriptType::Coroutine || type == ScriptType::Operation;
}

[[nodiscard]] constexpr bool isAssignableType(TypeRef from, TypeRef to) noexcept
{
    if (from == to) return true;
    if (to.kind == ScriptType::Entity) return from.kind == ScriptType::Actor || from.kind == ScriptType::Object;
    if (to.kind == ScriptType::Any) return from.kind != ScriptType::None;
    return false;
}

} // namespace schuimm::script
