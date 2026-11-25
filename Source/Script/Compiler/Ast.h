#pragma once

// Abstract syntax tree produced by the parser. The tree is purely syntactic: types and name
// resolution are computed by the TypeChecker into a separate SemanticModel.

#include "Script/Compiler/SourceFile.h"
#include "Script/Compiler/Types.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace schuimm::script {

enum class ExprKind : std::uint8_t {
    Error, // Placeholder after a syntax error.
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    BoolLiteral,
    NoneLiteral,
    Name,
    Self, // "self" inside an object handler: the object the handler runs for.
    Unary,
    Binary,
    Call,
};

enum class UnaryOp : std::uint8_t { Negate, Not };

enum class BinaryOp : std::uint8_t {
    Add, Subtract, Multiply, Divide, FloorDivide, Modulo,
    Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    And, Or,
};

[[nodiscard]] const char* toString(UnaryOp op) noexcept;
[[nodiscard]] const char* toString(BinaryOp op) noexcept; // The source spelling, e.g. "//".
[[nodiscard]] bool isComparison(BinaryOp op) noexcept;

struct Expr {
    ExprKind kind = ExprKind::Error;
    SourceRange range;
    std::int64_t intValue = 0;
    double floatValue = 0.0;
    bool boolValue = false;
    std::string text; // StringLiteral value, Name or Call callee name.
    SourceRange nameRange; // Call: the callee name.
    UnaryOp unaryOp = UnaryOp::Negate;
    BinaryOp binaryOp = BinaryOp::Add;
    std::unique_ptr<Expr> left;  // Unary operand or left operand.
    std::unique_ptr<Expr> right;
    std::vector<std::unique_ptr<Expr>> arguments;
};

struct Stmt;

struct Block {
    SourceRange range;
    std::vector<std::unique_ptr<Stmt>> statements;
};

enum class StmtKind : std::uint8_t { Expression, Assign, If, While, For, Return, Pass };

struct IfBranch {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Block> body;
};

struct Stmt {
    StmtKind kind = StmtKind::Pass;
    SourceRange range;
    // Assign / For: the variable.
    std::string name;
    SourceRange nameRange;
    std::optional<Type> annotation; // Assign: "name: type = value".
    SourceRange annotationRange;
    // Expression, Assign, Return (may be null), While (condition).
    std::unique_ptr<Expr> value;
    // If: the "if" branch followed by every "elif".
    std::vector<IfBranch> branches;
    std::unique_ptr<Block> elseBlock;
    // While / For body.
    std::unique_ptr<Block> body;
    // For: the range(...) arguments (1 to 3).
    std::vector<std::unique_ptr<Expr>> rangeArguments;
};

struct Parameter {
    std::string name;
    SourceRange nameRange;
    Type type = Type::Error;
    SourceRange typeRange;
};

struct FunctionDecl {
    SourceRange range;
    std::string name;
    SourceRange nameRange;
    std::vector<Parameter> parameters;
    Type returnType = Type::None;
    SourceRange returnTypeRange;
    bool isExtern = false;        // "extern func": a native function implemented by the engine.
    bool isHandler = false;       // Declared inside an "object" block (see ObjectDecl).
    std::unique_ptr<Block> body;  // Null for extern declarations.
};

// "object name:" followed by an indented list of handler functions. A named scope, not a class:
// it creates no runtime object. The name identifies an object of the room resource; the engine
// invokes a handler by object name and handler (event) name, passing the object as "self".
struct ObjectDecl {
    SourceRange range;
    std::string name;
    SourceRange nameRange;
    std::vector<std::unique_ptr<FunctionDecl>> handlers; // isHandler is set on each.
};

// The root: one source file.
struct SourceUnit {
    std::vector<std::unique_ptr<FunctionDecl>> functions; // Room-level functions and extern declarations.
    std::vector<std::unique_ptr<ObjectDecl>> objects;     // In declaration order.
};

} // namespace schuimm::script
