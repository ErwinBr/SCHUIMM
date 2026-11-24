#pragma once

#include "Script/Compiler/Ast.h"
#include "Script/Compiler/Diagnostics.h"
#include "Script/Compiler/Types.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace schuimm::script {

inline constexpr std::uint32_t invalidIndex = 0xFFFFFFFFu;

// Built-in conversion functions; they compile to instructions, not calls.
enum class Intrinsic : std::uint8_t { ToInt, ToFloat, ToString };

enum class CallKind : std::uint8_t { Function, Native, Intrinsic };

// A native (engine) function visible to scripts: a prelude function (print, wait, yield) or an
// "extern func" declaration. Calls to it are linked by name and signature at load time.
struct NativeSignature {
    std::string name;
    std::vector<Type> parameters;
    Type returnType = Type::None;
    bool pure = false; // No side effects: the optimizer may remove unused calls. None are, yet.
};

// The prelude every script sees: print(any), wait(float), yield().
[[nodiscard]] const std::vector<NativeSignature>& preludeNatives();

struct ExprInfo {
    Type type = Type::Error;
    bool widenToFloat = false;           // Implicit int -> float conversion of this value.
    std::uint32_t variable = invalidIndex; // Name: the variable within its function.
    CallKind callKind = CallKind::Function;
    std::uint32_t callee = invalidIndex;   // Function index (user functions) or native index.
    Intrinsic intrinsic = Intrinsic::ToInt;
};

struct VariableInfo {
    std::string name;
    Type type = Type::Error;
    SourceRange declaration;
    bool isParameter = false;
};

struct FunctionSemantics {
    const FunctionDecl* declaration = nullptr;
    std::vector<VariableInfo> variables; // Parameters first, in order.
};

struct StatementInfo {
    std::uint32_t variable = invalidIndex; // Assign target or for-loop variable.
    bool declares = false;                 // Assign: introduces the variable (otherwise assigns).
};

// Results of semantic analysis, keyed by AST node. The AST itself stays unchanged.
class SemanticModel {
public:
    [[nodiscard]] const ExprInfo& expression(const Expr& expression) const;
    [[nodiscard]] const ExprInfo* findExpression(const Expr& expression) const;
    [[nodiscard]] const StatementInfo& statement(const Stmt& statement) const;

    // User functions in declaration order (extern declarations excluded).
    [[nodiscard]] const std::vector<FunctionSemantics>& functions() const noexcept { return functions_; }
    // Prelude natives followed by extern declarations.
    [[nodiscard]] const std::vector<NativeSignature>& natives() const noexcept { return natives_; }

private:
    friend class TypeCheckerImpl;
    std::unordered_map<const Expr*, ExprInfo> expressions_;
    std::unordered_map<const Stmt*, StatementInfo> statements_;
    std::vector<FunctionSemantics> functions_;
    std::vector<NativeSignature> natives_;
};

// Resolves names and checks types (Language.md, "Types" and "Scopes"). Reports every error it
// finds; the model is only meaningful for code generation if no error was reported.
[[nodiscard]] SemanticModel checkTypes(const SourceUnit& unit, Diagnostics& diagnostics);

} // namespace schuimm::script
