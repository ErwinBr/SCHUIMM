#pragma once

#include "Script/Bytecode/Opcode.h"
#include "Script/Bytecode/ScriptType.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::script {

// Versions. The bytecode *format* version describes the binary layout and instruction set; the
// *language* version describes the source language the compiler accepts. They change
// independently (see Bytecode.md).
// Format 3.0: one module holds the scripts of a whole game (the Sources section, a source per
// function) and its behaviours (room, actor and object declarations). Language 3.0: game-wide
// declarations across source files. Earlier formats are rejected (recompile the sources).
inline constexpr std::uint16_t bytecodeFormatMajor = 3;
inline constexpr std::uint16_t bytecodeFormatMinor = 0;
inline constexpr std::uint16_t languageVersionMajor = 3;
inline constexpr std::uint16_t languageVersionMinor = 0;
inline constexpr std::uint32_t compilerVersion = 0x000400; // 0.4.0 as (major << 16 | minor << 8 | patch)

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
    std::vector<TypeRef> parameters;
    TypeRef returnType = ScriptType::None;
};

inline constexpr std::uint32_t noString = 0xFFFFFFFFu;
// Function::source of a compiler-generated function that belongs to no source file.
inline constexpr std::uint32_t noSource = 0xFFFFFFFFu;

// What a source file of a game is: it decides which declarations the file may contain.
//   Global  game-wide declarations only (var, func, extern func, enum);
//   Room    also the room's behaviour (room:) and its objects' behaviours (object NAME:);
//   Actor   also the behaviour of the actors that use it (actor:).
// A script compiled on its own is a Room script.
enum class ScriptRole : std::uint8_t { Global, Room, Actor };
inline constexpr std::uint8_t scriptRoleCount = 3;
[[nodiscard]] const char* toString(ScriptRole role) noexcept;

// A source file the module was compiled from: its path (the identity resources refer to it by,
// e.g. "Rooms/Kitchen/Kitchen.sch") and its role.
struct SourceEntry {
    std::uint32_t pathIndex = 0;
    ScriptRole role = ScriptRole::Room;
};

struct Function {
    std::uint32_t nameIndex = 0;
    std::uint32_t source = noSource; // Index into Module::sources (debug information, errors).
    std::vector<TypeRef> parameters; // Arguments arrive in registers 0..n-1.
    TypeRef returnType = ScriptType::None;
    // The static type of every register (registers 0..n-1 have the parameter types). Each
    // register holds values of one type for the whole function; the validator checks every
    // instruction against these types.
    std::vector<TypeRef> registerTypes;
    std::vector<Instruction> code;
    std::vector<SourcePosition> positions; // One per instruction (debug information).

    [[nodiscard]] std::uint16_t registerCount() const noexcept { return static_cast<std::uint16_t>(registerTypes.size()); }
};

// A scoped enum the module uses: declared by the script (qualifierIndex == noString), or defined
// by the engine in a native interface (qualifierIndex names the interface, e.g. "adventure").
// Engine enums are identified by qualifier, name and members when the module is linked.
// Values are ordinals 0..members.size()-1.
struct EnumEntry {
    std::uint32_t nameIndex = 0;
    std::uint32_t qualifierIndex = noString;
    std::vector<std::uint32_t> members; // String indices, in declaration order.

    [[nodiscard]] bool isEngine() const noexcept { return qualifierIndex != noString; }
};

// A handler of a behaviour: the event name and the function implementing it. An object
// handler's parameter 0 is the hidden "self" (ScriptType::Object, a room object).
struct HandlerEntry {
    std::uint32_t nameIndex = 0;
    std::uint32_t function = 0; // Index into Module::functions.
};

enum class BehaviourKind : std::uint8_t { Room, Actor, Object };
inline constexpr std::uint8_t behaviourKindCount = 3;
[[nodiscard]] const char* toString(BehaviourKind kind) noexcept;

// A behaviour declaration: the handlers of a room ("room:"), of the actors using an actor script
// ("actor:") or of one room object ("object NAME:", NAME being the object's id in the room
// resource). Not a runtime object; only a lookup table for dispatch. Its functions are listed
// nowhere else: functions no behaviour refers to are the game-wide (global) functions.
struct BehaviourEntry {
    BehaviourKind kind = BehaviourKind::Object;
    std::uint32_t source = 0;          // Index into Module::sources.
    std::uint32_t nameIndex = noString; // The object id; noString for room and actor behaviours.
    std::vector<HandlerEntry> handlers;
};

// In-memory form of a compiled script. Produced by the compiler, serialised by
// BytecodeWriter, re-created and validated by BytecodeReader.
struct Module {
    // The format minor version the module was read with (BytecodeReader) or targets. Written
    // files are always the current version.
    std::uint16_t formatMinor = bytecodeFormatMinor;
    std::uint16_t languageMajor = languageVersionMajor;
    std::uint16_t languageMinor = languageVersionMinor;
    std::uint32_t compilerVersion = script::compilerVersion;
    std::vector<std::string> strings;
    std::vector<SourceEntry> sources; // At least one.
    std::vector<Constant> constants;
    std::vector<NativeImport> imports;
    std::vector<Function> functions;
    std::vector<BehaviourEntry> behaviours;
    std::vector<EnumEntry> enums;

    [[nodiscard]] const std::string& string(std::uint32_t index) const { return strings[index]; }
    [[nodiscard]] const std::string& sourcePath(std::uint32_t source) const { return strings[sources[source].pathIndex]; }
    // The path of the first source: the name of a module compiled from one file.
    [[nodiscard]] const std::string& sourceName() const { return sourcePath(0); }
    // The source file of function `function`; empty for compiler-generated functions.
    [[nodiscard]] std::string_view functionSource(std::size_t function) const
    {
        const std::uint32_t source = functions[function].source;
        return source < sources.size() ? std::string_view(sourcePath(source)) : std::string_view();
    }
};

} // namespace schuimm::script
