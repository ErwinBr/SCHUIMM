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
inline constexpr std::uint16_t bytecodeFormatMinor = 0;
inline constexpr std::uint16_t languageVersionMajor = 1;
inline constexpr std::uint16_t languageVersionMinor = 0;
inline constexpr std::uint32_t compilerVersion = 0x000100; // 0.1.0 as (major << 16 | minor << 8 | patch)

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

// In-memory form of a compiled script. Produced by the compiler, serialised by
// BytecodeWriter, re-created and validated by BytecodeReader.
struct Module {
    std::uint16_t languageMajor = languageVersionMajor;
    std::uint16_t languageMinor = languageVersionMinor;
    std::uint32_t compilerVersion = script::compilerVersion;
    std::uint32_t sourceNameIndex = 0; // Debug information: the source file name.
    std::vector<std::string> strings;
    std::vector<Constant> constants;
    std::vector<NativeImport> imports;
    std::vector<Function> functions;

    [[nodiscard]] const std::string& string(std::uint32_t index) const { return strings[index]; }
    [[nodiscard]] const std::string& sourceName() const { return strings[sourceNameIndex]; }
};

} // namespace schuimm::script
