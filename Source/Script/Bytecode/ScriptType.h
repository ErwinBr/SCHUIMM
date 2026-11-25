#pragma once

#include <cstdint>
#include <optional>

namespace schuimm::script {

// Value types shared by the compiler, the bytecode format and the VM. The numeric values are
// part of the bytecode format (function and native signatures) and must not change.
enum class ScriptType : std::uint8_t {
    None = 0,   // "no value": the return type of functions without a result.
    Bool = 1,
    Int = 2,    // Signed 64-bit integer.
    Float = 3,  // IEEE 754 binary64.
    String = 4, // Immutable UTF-8 text.
    Any = 5,    // Native parameters only: accepts any value except None (e.g. print).
    Object = 6, // Opaque handle of an engine object (ObjectRef); format 1.1 and later.
};

[[nodiscard]] const char* toString(ScriptType type) noexcept;

// Decodes a serialised type; nullopt for unknown values.
[[nodiscard]] std::optional<ScriptType> scriptTypeFromByte(std::uint8_t value) noexcept;

} // namespace schuimm::script
