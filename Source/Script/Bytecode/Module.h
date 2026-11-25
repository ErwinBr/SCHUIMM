#pragma once

#include "Script/Bytecode/Opcode.h"
#include "Script/Bytecode/ScriptType.h"

#include <cstdint>
#include <string>
#include <vector>

namespace schuimm::script {

// Versions. The bytecode *format* version describes the binary layout and instruction set; the
// *language* version describes the source language the compiler accepts. They change
// independently (see Bytecode.md).
inline constexpr std::uint16_t bytecodeFormatMajor = 1;
// Format 1.1 added the Objects section, the object type and EqObject/NeObject; language 1.1 added
// object declarations and self.
inline constexpr std::uint16_t bytecodeFormatMinor = 1;
inline constexpr std::uint16_t languageVersionMajor = 1;
inline constexpr std::uint16_t languageVersionMinor = 1;
inline constexpr std::uint32_t compilerVersion = 0x000200; // 0.2.0 as (major << 16 | minor << 8 | patch)

struct Instruction {
    Opcode opcode = Opcode::Nop;
    std::uint16_t a = 0;
    std::uint16_t b = 0;
    std::uint16_t c = 0;

    [[nodiscard]] std::uint32_t target() const noexcept
    {
        return static_cast<std::uint32_t>(b) | (static_cast<std::uint32_t>(c) << 16);
    }
    void setTarget(std::uint32_t target) noexcept
    {
        b = static_cast<std::uint16_t>(target & 0xFFFFu);
        c = static_cast<std::uint16_t>(target >> 16);
    }
    friend bool operator==(const Instruction&, const Instruction&) = default;
};

// Constant pool entry: an Int, Float or String (index into Module::strings).
struct Constant {
    ScriptType type = ScriptType::Int;
    std::int64_t intValue = 0;
    double floatValue = 0.0;
    std::uint32_t stringIndex = 0;
};

struct SourcePosition {
    std::uint32_t line = 0; // 1-based; 0 = unknown.
    std::uint32_t column = 0;
    friend bool operator==(const SourcePosition&, const SourcePosition&) = default;
};

// A native (engine) function the module calls. Resolved by name and checked against the
// signature when the module is linked (see Runtime/Program.h).
struct NativeImport {
    std::uint32_t nameIndex = 0;
    std::vector<ScriptType> parameters;
    ScriptType returnType = ScriptType::None;
};

struct Function {
    std::uint32_t nameIndex = 0;
    std::vector<ScriptType> parameters; // Arguments arrive in registers 0..n-1.
    ScriptType returnType = ScriptType::None;
    std::uint16_t registerCount = 0;
    std::vector<Instruction> code;
    std::vector<SourcePosition> positions; // One per instruction (debug information).
};

// A handler of an object declaration: the event name and the function implementing it. The
// function's parameter 0 is the hidden "self" (ScriptType::Object).
struct HandlerEntry {
    std::uint32_t nameIndex = 0;
    std::uint32_t function = 0; // Index into Module::functions.
};

// An object declaration of the script: the object identifier (as used by the room resource)
// and its handlers. Not a runtime object; only a lookup table for dispatch.
struct ObjectEntry {
    std::uint32_t nameIndex = 0;
    std::vector<HandlerEntry> handlers;
};

// In-memory form of a compiled script. Produced by the compiler, serialised by
// BytecodeWriter, re-created and validated by BytecodeReader.
struct Module {
    // The format minor version the module was read with (BytecodeReader) or targets. A 1.0
    // module may not use features of 1.1 (validateModule checks). Written files are always the
    // current version.
    std::uint16_t formatMinor = bytecodeFormatMinor;
    std::uint16_t languageMajor = languageVersionMajor;
    std::uint16_t languageMinor = languageVersionMinor;
    std::uint32_t compilerVersion = script::compilerVersion;
    std::uint32_t sourceNameIndex = 0; // Debug information: the source file name.
    std::vector<std::string> strings;
    std::vector<Constant> constants;
    std::vector<NativeImport> imports;
    std::vector<Function> functions;
    std::vector<ObjectEntry> objects; // Format 1.1+.

    [[nodiscard]] const std::string& string(std::uint32_t index) const { return strings[index]; }
    [[nodiscard]] const std::string& sourceName() const { return strings[sourceNameIndex]; }
};

} // namespace schuimm::script
