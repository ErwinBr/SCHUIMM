#pragma once

#include "Script/Bytecode/Module.h"
#include "Script/Bytecode/NativeInterface.h"
#include "Script/Compiler/Ast.h"
#include "Script/Compiler/Diagnostics.h"
#include "Script/Compiler/Types.h"

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace schuimm::script {

inline constexpr std::uint32_t invalidIndex = 0xFFFFFFFFu;

// Built-in functions that compile to instructions, not calls: the conversions int(), float(),
// str() and the vector constructor vec2(x, y).
enum class Intrinsic : std::uint8_t { ToInt, ToFloat, ToString, MakeVec2 };

enum class CallKind : std::uint8_t { Function, Native, Intrinsic };

// A native (engine) function visible to scripts: a function of a native interface (the core
// interface and those in CompileOptions::interfaces) or an "extern func" declaration. Calls to it
// are linked by name and signature at load time.
struct NativeSignature {
    std::string name;
    std::vector<Type> parameters;
    Type returnType = Type::None;
    bool pure = false;         // No side effects: the optimizer may remove unused calls.
    std::string interfaceName; // Empty for extern declarations.
    bool hidden = false;       // Not callable by name (await_operation, behind wait(operation)).
};

// An enum type: declared by the script ("enum Name:") or by a native interface (an engine
// enum, whose bytecode identity is "<qualifier>.<name>").
struct EnumInfo {
    std::string name;
    std::string qualifier; // The interface name of an engine enum; empty for script enums.
    std::vector<std::string> members;
    SourceRange declaration; // Line 0 for engine enums.

    [[nodiscard]] bool isEngine() const noexcept { return !qualifier.empty(); }
};

// Where a variable lives: in the function's frame, in the behaviour instance the function runs
// for, or in the game session.
enum class VariableStorage : std::uint8_t {
    Local,    // Parameters and "var" inside functions: index into FunctionSemantics::variables.
    Instance, // "var" in a behaviour: index into BehaviourSemantics::variables.
    Global,   // "var" at the top level: index into SemanticModel::globals().
};

struct ExprInfo {
    Type type = Type::Error;
    bool widenToFloat = false;           // Implicit int -> float conversion of this value.
    std::uint32_t variable = invalidIndex; // Name: the variable, in `storage`.
    VariableStorage storage = VariableStorage::Local;
    CallKind callKind = CallKind::Function;
    std::uint32_t callee = invalidIndex;   // Function index (user functions) or native index.
    Intrinsic intrinsic = Intrinsic::ToInt;
    // Member: an enum member (`type` is the enum, enumMember its ordinal), or a vec2 component
    // (component 0 = x, 1 = y).
    bool isEnumMember = false;
    std::uint32_t enumMember = 0;
    std::uint8_t component = 0;
    // Binary "scalar * vector": the operands are swapped for the vector instruction.
    bool swapOperands = false;
    // Spawn: `callee` is the function to start (a script function or a spawn wrapper).
};

// A global or instance variable: one storage location per game session or per instance.
struct PersistentVariable {
    std::string name;
    Type type = Type::Error;
    SourceRange declaration;
    std::uint32_t source = 0;
    ConstantValue initial;
};

struct VariableInfo {
    std::string name;
    Type type = Type::Error;
    SourceRange declaration;
    bool isParameter = false;
};

struct FunctionSemantics {
    const FunctionDecl* declaration = nullptr;
    std::uint32_t source = 0; // Index into SemanticModel::sources().
    // Handlers: index of the owning behaviour in SemanticModel::behaviours(); invalidIndex for
    // global functions. An object handler's variable 0 is the hidden parameter "self" (type
    // object), followed by the declared parameters.
    std::uint32_t behaviour = invalidIndex;
    std::vector<VariableInfo> variables; // Parameters first, in order.
    std::vector<Type> parameters;        // The declared parameters (without "self"), resolved.
    Type returnType = Type::None;

    [[nodiscard]] bool isHandler() const noexcept { return behaviour != invalidIndex; }
};

// A handler of a behaviour: the event name and the function implementing it.
struct HandlerSemantics {
    std::string name;
    std::uint32_t function = invalidIndex; // Index into SemanticModel::functions().
};

// A behaviour declaration ("object name:"): a named scope of handlers. Handler names live in
// this scope only, never in the game-wide scope of functions and natives.
struct BehaviourSemantics {
    BehaviourKind kind = BehaviourKind::Object;
    std::uint32_t source = 0;               // Index into SemanticModel::sources().
    const ObjectDecl* declaration = nullptr; // Object behaviours.
    std::string name;                        // The object id; empty for room and actor behaviours.
    std::vector<HandlerSemantics> handlers;  // In declaration order.
    std::vector<PersistentVariable> variables; // Its instance variables, in declaration order.
};

// A source file of the program: its name (path) and role.
struct SourceSemantics {
    std::string name;
    ScriptRole role = ScriptRole::Room;
    std::uint32_t file = 0; // SourceRange::file of its tokens.
};

struct StatementInfo {
    std::uint32_t variable = invalidIndex; // Assign target or for-loop variable, in `storage`.
    VariableStorage storage = VariableStorage::Local;
    bool declares = false;                 // Assign: introduces the variable (otherwise assigns).
    bool component = false;                // Assign: sets component `componentIndex` of a vec2.
    std::uint8_t componentIndex = 0;
};

// Results of semantic analysis, keyed by AST node. The AST itself stays unchanged.
class SemanticModel {
public:
    [[nodiscard]] const ExprInfo& expression(const Expr& expression) const;
    [[nodiscard]] const ExprInfo* findExpression(const Expr& expression) const;
    [[nodiscard]] const StatementInfo& statement(const Stmt& statement) const;

    // The source files, in the order they were checked.
    [[nodiscard]] const std::vector<SourceSemantics>& sources() const noexcept { return sources_; }
    // User functions: the global functions of every source in declaration order (extern
    // declarations excluded), followed by the handlers of every behaviour in declaration order.
    [[nodiscard]] const std::vector<FunctionSemantics>& functions() const noexcept { return functions_; }
    // Global variables of every source, in source and declaration order.
    [[nodiscard]] const std::vector<PersistentVariable>& globals() const noexcept { return globals_; }
    // Behaviour declarations, by source and declaration order.
    [[nodiscard]] const std::vector<BehaviourSemantics>& behaviours() const noexcept { return behaviours_; }
    // Interface functions (core first) followed by extern declarations.
    [[nodiscard]] const std::vector<NativeSignature>& natives() const noexcept { return natives_; }
    // Natives the script spawns ("spawn walk_to(...)"), as indices into natives(). Each gets a
    // compiler-generated wrapper function "spawn:<name>" that calls it; the wrapper of
    // spawnWrappers()[i] is function functions().size() + i.
    [[nodiscard]] const std::vector<std::uint32_t>& spawnWrappers() const noexcept { return spawnWrappers_; }
    // Enum types: engine enums of the interfaces, then the script's enums. Type enumType(i)
    // refers to enums()[i].
    [[nodiscard]] const std::vector<EnumInfo>& enums() const noexcept { return enums_; }
    // The source spelling of a type, with enum names.
    [[nodiscard]] std::string typeName(Type type) const;

private:
    friend class TypeCheckerImpl;
    std::unordered_map<const Expr*, ExprInfo> expressions_;
    std::unordered_map<const Stmt*, StatementInfo> statements_;
    std::vector<SourceSemantics> sources_;
    std::vector<FunctionSemantics> functions_;
    std::vector<BehaviourSemantics> behaviours_;
    std::vector<PersistentVariable> globals_;
    std::vector<NativeSignature> natives_;
    std::vector<EnumInfo> enums_;
    std::vector<std::uint32_t> spawnWrappers_;
};

// One parsed source file of a program.
struct SourceUnitInput {
    const SourceUnit* unit = nullptr;
    std::string name;  // The source path, for messages that refer to another file.
    ScriptRole role = ScriptRole::Room;
    std::uint32_t file = 0; // SourceRange::file of its tokens (SourceFile::id).
};

// Resolves names and checks types (Language.md, "Types" and "Scopes"). Reports every error it
// finds; the model is only meaningful for code generation if no error was reported.
//
// All units form one program: their enums, functions and natives share one game-wide namespace,
// independent of the order of the units (declarations are collected before any body is
// checked). The units are processed, and their functions numbered, in the given order.
//
// The scripts see the core interface (coreInterface()) and every interface in `interfaces`:
// their functions can be called and their enums used without declaring them. Two interfaces
// declaring the same function or enum name, and script declarations (functions, extern
// functions, enums) reusing such a name, are errors. Identical extern declarations in several
// units declare one native; conflicting ones are an error.
[[nodiscard]] SemanticModel checkTypes(std::span<const SourceUnitInput> units, Diagnostics& diagnostics,
                                       std::span<const NativeInterface* const> interfaces = {});
// A single room script (file id 0).
[[nodiscard]] SemanticModel checkTypes(const SourceUnit& unit, Diagnostics& diagnostics,
                                       std::span<const NativeInterface* const> interfaces = {});

} // namespace schuimm::script
