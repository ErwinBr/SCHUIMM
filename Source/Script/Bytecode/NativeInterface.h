#pragma once

// Native API interfaces: the one authoritative declaration of each engine function scripts can
// call, shared by the compiler (which resolves calls against it) and the runtime (which binds an
// implementation to each declaration, see Runtime/NativeRegistry.h). An interface is pure data:
// names, typed signatures, execution categories and the engine enums they use. It contains no
// implementation and needs no engine subsystem, so the compiler can use it without linking the
// runtime or the adventure layer.
//
// Interfaces:
//   * the core interface (coreInterface()): print, wait, yield, cancel and the vec2 maths
//     functions, available to every script, and the hidden await_operation behind
//     wait(operation);
//   * further interfaces supplied by the engine, e.g. the adventure API (Source/Adventure). The
//     compiler receives them through CompileOptions; the runtime binds them through
//     NativeRegistry::bind.
//
// An engine enum is identified by "<interface>.<enum>" (e.g. "adventure.Direction") together
// with its members; scripts name it by its plain name. The compiler records the identity in the
// bytecode, and linking checks it against the registered enum, so ordinals always mean the same.

#include "Script/Bytecode/ScriptType.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::script {

// A parameter or result type of a native declaration. For Enum, `enumName` names an enum of the
// same interface.
struct NativeType {
    ScriptType kind = ScriptType::None;
    std::string enumName;

    NativeType() = default;
    NativeType(ScriptType type) // NOLINT(google-explicit-constructor): a plain kind.
        : kind(type)
    {
    }
    NativeType(ScriptType type, std::string enumType)
        : kind(type)
        , enumName(std::move(enumType))
    {
    }
    friend bool operator==(const NativeType&, const NativeType&) = default;
};

// How a call behaves for the calling coroutine (part of the native's contract; scripts call all
// three the same way):
//   Immediate   performs an action and returns;
//   Suspending  suspends the caller until the action it started completes (e.g. walk_to);
//   Background  starts an engine operation and returns at once, possibly with an operation
//               reference the script may wait for (e.g. play_animation).
enum class NativeCategory : std::uint8_t { Immediate, Suspending, Background };

[[nodiscard]] const char* toString(NativeCategory category) noexcept;

struct NativeDeclaration {
    std::string name;
    std::vector<NativeType> parameters;
    NativeType returnType = ScriptType::None;
    NativeCategory category = NativeCategory::Immediate;
    bool pure = false; // No side effects: the optimizer may remove unused calls.
    std::string documentation;
    // Not callable by name: the compiler reaches it only through a language construct (the
    // core interface's await_operation implements wait(operation)). Its name is still reserved.
    bool hidden = false;
};

struct EnumDeclaration {
    std::string name;
    std::vector<std::string> members;
    friend bool operator==(const EnumDeclaration&, const EnumDeclaration&) = default;
};

struct NativeInterface {
    std::string name; // Qualifies its enums: "<name>.<enum>".
    std::vector<EnumDeclaration> enums;
    std::vector<NativeDeclaration> functions;

    [[nodiscard]] const EnumDeclaration* findEnum(std::string_view enumName) const noexcept;
    [[nodiscard]] const NativeDeclaration* findFunction(std::string_view functionName) const noexcept;
};

// "adventure.Direction"
[[nodiscard]] std::string qualifiedEnumName(std::string_view interfaceName, std::string_view enumName);

// "wait(float) -> None", with enum names: "set_direction(actor, Direction) -> None".
[[nodiscard]] std::string formatSignature(const NativeDeclaration& declaration);

// Structural check of an interface: valid identifiers, unique names, enums with unique members,
// enum-typed parameters and results naming an enum of the interface, no None parameter and no
// any result. Empty string if valid, otherwise the first problem.
[[nodiscard]] std::string checkInterface(const NativeInterface& interface);

// The interface every script sees.
[[nodiscard]] const NativeInterface& coreInterface();

} // namespace schuimm::script
