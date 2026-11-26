#pragma once

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

struct ExprInfo {
    Type type = Type::Error;
    bool widenToFloat = false;           // Implicit int -> float conversion of this value.
    std::uint32_t variable = invalidIndex; // Name: the variable within its function.
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

struct VariableInfo {
    std::string name;
    Type type = Type::Error;
    SourceRange declaration;
    bool isParameter = false;
};

struct FunctionSemantics {
    const FunctionDecl* declaration = nullptr;
    // Handlers: index of the owning object in SemanticModel::objects(); invalidIndex for
    // room-level functions. A handler's variable 0 is the hidden parameter "self" (type object),
    // followed by the declared parameters.
    std::uint32_t object = invalidIndex;
    std::vector<VariableInfo> variables; // Parameters first, in order.
    std::vector<Type> parameters;        // The declared parameters (without "self"), resolved.
    Type returnType = Type::None;

    [[nodiscard]] bool isHandler() const noexcept { return object != invalidIndex; }
};

// A handler of an object declaration: the event name and the function implementing it.
struct HandlerSemantics {
    std::string name;
    std::uint32_t function = invalidIndex; // Index into SemanticModel::functions().
};

// An "object" declaration: a named scope of handlers. Handler names live in this scope only,
// never in the room scope of functions and natives.
struct ObjectSemantics {
    const ObjectDecl* declaration = nullptr;
    std::vector<HandlerSemantics> handlers; // In declaration order.
};

struct StatementInfo {
    std::uint32_t variable = invalidIndex; // Assign target or for-loop variable.
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

    // User functions: room-level functions in declaration order (extern declarations excluded),
    // followed by the handlers of every object in declaration order.
    [[nodiscard]] const std::vector<FunctionSemantics>& functions() const noexcept { return functions_; }
    // Object declarations in declaration order.
    [[nodiscard]] const std::vector<ObjectSemantics>& objects() const noexcept { return objects_; }
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
    std::vector<FunctionSemantics> functions_;
    std::vector<ObjectSemantics> objects_;
    std::vector<NativeSignature> natives_;
    std::vector<EnumInfo> enums_;
    std::vector<std::uint32_t> spawnWrappers_;
};

// Resolves names and checks types (Language.md, "Types" and "Scopes"). Reports every error it
// finds; the model is only meaningful for code generation if no error was reported.
//
// The script sees the core interface (coreInterface()) and every interface in `interfaces`:
// their functions can be called and their enums used without declaring them. Two interfaces
// declaring the same function or enum name, and script declarations (functions, extern
// functions, objects, enums) reusing such a name, are errors.
[[nodiscard]] SemanticModel checkTypes(const SourceUnit& unit, Diagnostics& diagnostics,
                                       std::span<const NativeInterface* const> interfaces = {});

} // namespace schuimm::script
