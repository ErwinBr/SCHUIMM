#include "Script/Compiler/TypeChecker.h"

#include "Core/Common/StringBuilder.h"

#include <optional>
#include <string_view>

namespace schuimm::script {

const std::vector<NativeSignature>& preludeNatives()
{
    static const std::vector<NativeSignature> prelude{
        {"print", {Type::Any}, Type::None, false},
        {"wait", {Type::Float}, Type::None, false},
        {"yield", {}, Type::None, false},
    };
    return prelude;
}

const ExprInfo& SemanticModel::expression(const Expr& expression) const
{
    static const ExprInfo unknown;
    const auto found = expressions_.find(&expression);
    return found != expressions_.end() ? found->second : unknown;
}

const ExprInfo* SemanticModel::findExpression(const Expr& expression) const
{
    const auto found = expressions_.find(&expression);
    return found != expressions_.end() ? &found->second : nullptr;
}

const StatementInfo& SemanticModel::statement(const Stmt& statement) const
{
    static const StatementInfo unknown;
    const auto found = statements_.find(&statement);
    return found != statements_.end() ? found->second : unknown;
}

namespace {

struct FunctionSymbol {
    CallKind kind = CallKind::Function;
    std::uint32_t index = 0;
    SourceRange declaration;
};

std::optional<Intrinsic> intrinsicFromName(std::string_view name) noexcept
{
    if (name == "int") return Intrinsic::ToInt;
    if (name == "float") return Intrinsic::ToFloat;
    if (name == "str") return Intrinsic::ToString;
    return std::nullopt;
}

std::string quoted(Type type) { return concat("'", toString(type), "'"); }

} // namespace

class TypeCheckerImpl {
public:
    TypeCheckerImpl(const SourceUnit& unit, Diagnostics& diagnostics)
        : unit_(unit)
        , diagnostics_(diagnostics)
    {
    }

    SemanticModel run()
    {
        model_.natives_ = preludeNatives();
        declareFunctions();
        std::size_t functionIndex = 0;
        for (const auto& function : unit_.functions) {
            if (!function->isExtern) {
                checkFunction(*function, functionIndex++);
            }
        }
        return std::move(model_);
    }

private:
    void error(const SourceRange& range, std::string message, std::vector<std::string> notes = {})
    {
        diagnostics_.error(DiagnosticPhase::Semantic, range, std::move(message), std::move(notes));
    }

    [[nodiscard]] bool isReservedName(const std::string& name) const
    {
        return intrinsicFromName(name) || typeFromName(name) || name == "range";
    }

    // --- Pass 1: function and extern signatures ----------------------------------------------
    void declareFunctions()
    {
        for (std::uint32_t i = 0; i < model_.natives_.size(); ++i) {
            symbols_[model_.natives_[i].name] = {CallKind::Native, i, {}};
        }
        std::uint32_t functionIndex = 0;
        for (const auto& function : unit_.functions) {
            const std::string& name = function->name;
            bool valid = true;
            if (isReservedName(name)) {
                error(function->nameRange, concat("'", name, "' is a built-in name and cannot be redefined."));
                valid = false;
            } else if (const auto existing = symbols_.find(name); existing != symbols_.end()) {
                if (existing->second.kind == CallKind::Native && existing->second.declaration.begin.line == 0) {
                    error(function->nameRange, concat("'", name, "' is a built-in function and cannot be redefined."));
                } else {
                    error(function->nameRange, concat("Duplicate declaration of function '", name, "'."),
                          {concat("'", name, "' was first declared on line ", existing->second.declaration.begin.line, ".")});
                }
                valid = false;
            }
            for (std::size_t p = 0; p < function->parameters.size(); ++p) {
                for (std::size_t q = 0; q < p; ++q) {
                    if (function->parameters[p].name == function->parameters[q].name) {
                        error(function->parameters[p].nameRange,
                              concat("Duplicate parameter '", function->parameters[p].name, "'."));
                    }
                }
            }
            if (function->isExtern) {
                NativeSignature native;
                native.name = name;
                for (const Parameter& parameter : function->parameters) {
                    native.parameters.push_back(parameter.type);
                }
                native.returnType = function->returnType;
                const auto index = static_cast<std::uint32_t>(model_.natives_.size());
                model_.natives_.push_back(std::move(native));
                if (valid) {
                    symbols_[name] = {CallKind::Native, index, function->nameRange};
                }
            } else {
                FunctionSemantics semantics;
                semantics.declaration = function.get();
                model_.functions_.push_back(std::move(semantics));
                if (valid) {
                    symbols_[name] = {CallKind::Function, functionIndex, function->nameRange};
                }
                ++functionIndex;
            }
        }
    }

    // --- Pass 2: function bodies -------------------------------------------------------------
    void checkFunction(const FunctionDecl& function, std::size_t index)
    {
        current_ = &model_.functions_[index];
        scopes_.clear();
        scopes_.emplace_back();
        for (const Parameter& parameter : function.parameters) {
            if (scopes_.back().count(parameter.name) == 0) {
                declareVariable(parameter.name, parameter.type, parameter.nameRange, true);
            }
        }
        if (function.body) {
            checkBlock(*function.body, function);
            if (function.returnType != Type::None && function.returnType != Type::Error &&
                !alwaysReturns(*function.body)) {
                error(function.nameRange,
                      concat("Function '", function.name, "' must return a value of type ", quoted(function.returnType),
                             " on every path."),
                      {"Add a 'return' statement at the end of the function."});
            }
        }
        current_ = nullptr;
    }

    std::uint32_t declareVariable(const std::string& name, Type type, SourceRange range, bool isParameter)
    {
        if (symbols_.count(name) != 0 || isReservedName(name)) {
            error(range, concat("'", name, "' is the name of a function and cannot be used as a variable name."));
        }
        const auto index = static_cast<std::uint32_t>(current_->variables.size());
        current_->variables.push_back({name, type, range, isParameter});
        scopes_.back()[name] = index;
        return index;
    }

    [[nodiscard]] std::optional<std::uint32_t> lookupVariable(const std::string& name) const
    {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            if (const auto found = scope->find(name); found != scope->end()) {
                return found->second;
            }
        }
        return std::nullopt;
    }

    void checkBlock(const Block& block, const FunctionDecl& function)
    {
        scopes_.emplace_back();
        for (const auto& statement : block.statements) {
            checkStatement(*statement, function);
        }
        scopes_.pop_back();
    }

    void requireBool(const Expr& condition, const char* what)
    {
        const Type type = checkExpression(condition);
        if (type != Type::Bool && type != Type::Error) {
            error(condition.range, concat("The ", what, " must be of type 'bool', got ", quoted(type), "."),
                  {"There is no implicit conversion to bool; compare explicitly, e.g. 'count > 0'."});
        }
    }

    // Checks that `value` (already checked, of type `from`) fits `to`; marks int -> float widening.
    bool convertTo(const Expr& value, Type from, Type to)
    {
        if (!isAssignable(from, to)) {
            return false;
        }
        if (from == Type::Int && to == Type::Float) {
            model_.expressions_[&value].widenToFloat = true;
        }
        return true;
    }

    void checkStatement(const Stmt& statement, const FunctionDecl& function)
    {
        switch (statement.kind) {
        case StmtKind::Pass: return;
        case StmtKind::Expression: {
            const Expr& value = *statement.value;
            checkExpression(value);
            if (value.kind != ExprKind::Call && value.kind != ExprKind::Error) {
                std::vector<std::string> notes;
                if (value.kind == ExprKind::Binary && value.binaryOp == BinaryOp::Equal && value.left->kind == ExprKind::Name) {
                    notes.push_back("Use '=' to assign a value; '==' compares.");
                }
                error(value.range, "This expression has no effect; only function calls can be used as statements.",
                      std::move(notes));
            }
            return;
        }
        case StmtKind::Assign: checkAssignment(statement); return;
        case StmtKind::If:
            for (const IfBranch& branch : statement.branches) {
                requireBool(*branch.condition, "condition");
                checkBlock(*branch.body, function);
            }
            if (statement.elseBlock) {
                checkBlock(*statement.elseBlock, function);
            }
            return;
        case StmtKind::While:
            requireBool(*statement.value, "loop condition");
            checkBlock(*statement.body, function);
            return;
        case StmtKind::For: {
            for (const auto& argument : statement.rangeArguments) {
                const Type type = checkExpression(*argument);
                if (type != Type::Int && type != Type::Error) {
                    error(argument->range, concat("range() arguments must be of type 'int', got ", quoted(type), "."));
                }
            }
            scopes_.emplace_back();
            StatementInfo info;
            if (const auto existing = lookupVariable(statement.name)) {
                error(statement.nameRange, concat("Loop variable '", statement.name, "' shadows an existing variable."),
                      {concat("'", statement.name, "' was declared on line ",
                              current_->variables[*existing].declaration.begin.line, "; choose another name.")});
            }
            info.variable = declareVariable(statement.name, Type::Int, statement.nameRange, false);
            info.declares = true;
            model_.statements_[&statement] = info;
            checkBlock(*statement.body, function);
            scopes_.pop_back();
            return;
        }
        case StmtKind::Return: {
            if (!statement.value) {
                if (function.returnType != Type::None && function.returnType != Type::Error) {
                    error(statement.range, concat("Function '", function.name, "' must return a value of type ",
                                                  quoted(function.returnType), "."));
                }
                return;
            }
            const Type type = checkExpression(*statement.value);
            if (function.returnType == Type::None) {
                if (type != Type::None && type != Type::Error) {
                    error(statement.value->range, concat("Function '", function.name, "' has no return type and cannot return a value."),
                          {concat("Declare the return type: 'func ", function.name, "(...) -> ", toString(type), ":'.")});
                }
                return;
            }
            if (!convertTo(*statement.value, type, function.returnType)) {
                error(statement.value->range, concat("Cannot return a value of type ", quoted(type), " from function '",
                                                     function.name, "', which returns ", quoted(function.returnType), "."));
            }
            return;
        }
        }
    }

    void checkAssignment(const Stmt& statement)
    {
        const Type type = checkExpression(*statement.value);
        StatementInfo info;
        const auto existing = lookupVariable(statement.name);
        if (statement.annotation) {
            if (existing) {
                const VariableInfo& previous = current_->variables[*existing];
                error(statement.nameRange,
                      concat(previous.isParameter ? "Parameter '" : "Variable '", statement.name, "' is already declared."),
                      {concat("It was declared on line ", previous.declaration.begin.line,
                              "; assign without a type annotation to change it, or choose another name.")});
                return;
            }
            const Type declared = *statement.annotation;
            if (type == Type::None) {
                error(statement.value->range, "Cannot assign a value of type 'None': the expression has no value.");
            } else if (!convertTo(*statement.value, type, declared)) {
                error(statement.value->range, concat("Cannot assign a value of type ", quoted(type), " to variable '",
                                                     statement.name, "' of type ", quoted(declared), "."));
            }
            info.variable = declareVariable(statement.name, declared, statement.nameRange, false);
            info.declares = true;
            model_.statements_[&statement] = info;
            return;
        }
        if (existing) {
            const VariableInfo& variable = current_->variables[*existing];
            if (type == Type::None) {
                error(statement.value->range, "Cannot assign a value of type 'None': the expression has no value.");
            } else if (!convertTo(*statement.value, type, variable.type)) {
                error(statement.value->range, concat("Cannot assign a value of type ", quoted(type), " to variable '",
                                                     statement.name, "' of type ", quoted(variable.type), "."),
                      {concat("'", statement.name, "' was declared as ", quoted(variable.type), " on line ",
                              variable.declaration.begin.line, ".")});
            }
            info.variable = *existing;
            model_.statements_[&statement] = info;
            return;
        }
        Type declared = type;
        if (type == Type::None) {
            error(statement.value->range, "Cannot assign a value of type 'None': the expression has no value.");
            declared = Type::Error;
        }
        info.variable = declareVariable(statement.name, declared, statement.nameRange, false);
        info.declares = true;
        model_.statements_[&statement] = info;
    }

    // --- Expressions --------------------------------------------------------------------------
    Type record(const Expr& expression, ExprInfo info)
    {
        const Type type = info.type;
        ExprInfo& slot = model_.expressions_[&expression];
        const bool widen = slot.widenToFloat;
        slot = info;
        slot.widenToFloat = widen;
        return type;
    }

    Type checkExpression(const Expr& expression)
    {
        ExprInfo info;
        switch (expression.kind) {
        case ExprKind::Error: info.type = Type::Error; break;
        case ExprKind::IntLiteral: info.type = Type::Int; break;
        case ExprKind::FloatLiteral: info.type = Type::Float; break;
        case ExprKind::StringLiteral: info.type = Type::String; break;
        case ExprKind::BoolLiteral: info.type = Type::Bool; break;
        case ExprKind::NoneLiteral: info.type = Type::None; break;
        case ExprKind::Name: return checkName(expression);
        case ExprKind::Unary: return checkUnary(expression);
        case ExprKind::Binary: return checkBinary(expression);
        case ExprKind::Call: return checkCall(expression);
        }
        return record(expression, info);
    }

    Type checkName(const Expr& expression)
    {
        ExprInfo info;
        if (const auto variable = lookupVariable(expression.text)) {
            info.variable = *variable;
            info.type = current_->variables[*variable].type;
            return record(expression, info);
        }
        std::vector<std::string> notes;
        const std::string& name = expression.text;
        if (name == "true") notes.push_back("Did you mean 'True'?");
        if (name == "false") notes.push_back("Did you mean 'False'?");
        if (name == "none" || name == "null" || name == "nil") notes.push_back("Did you mean 'None'?");
        if (symbols_.count(name) != 0 || intrinsicFromName(name)) {
            error(expression.range, concat("'", name, "' is a function; call it with ", name, "(...)."));
        } else {
            error(expression.range, concat("Undefined variable '", name, "'."), std::move(notes));
        }
        info.type = Type::Error;
        return record(expression, info);
    }

    Type checkUnary(const Expr& expression)
    {
        const Type operand = checkExpression(*expression.left);
        ExprInfo info;
        if (operand == Type::Error) {
            info.type = Type::Error;
        } else if (expression.unaryOp == UnaryOp::Not) {
            info.type = Type::Bool;
            if (operand != Type::Bool) {
                error(expression.range, concat("Operator 'not' requires a 'bool' operand, got ", quoted(operand), "."));
            }
        } else if (isNumeric(operand)) {
            info.type = operand;
        } else {
            error(expression.range, concat("Operator '-' cannot be applied to ", quoted(operand), "."));
            info.type = Type::Error;
        }
        return record(expression, info);
    }

    // Both operands numeric: the common type, widening an int operand when the other is float.
    Type unifyNumeric(const Expr& expression, Type left, Type right)
    {
        if (left == Type::Int && right == Type::Int) {
            return Type::Int;
        }
        if (left == Type::Int) model_.expressions_[expression.left.get()].widenToFloat = true;
        if (right == Type::Int) model_.expressions_[expression.right.get()].widenToFloat = true;
        return Type::Float;
    }

    Type checkBinary(const Expr& expression)
    {
        const Type left = checkExpression(*expression.left);
        const Type right = checkExpression(*expression.right);
        ExprInfo info;
        const BinaryOp op = expression.binaryOp;
        const bool comparison = isComparison(op);
        if (left == Type::Error || right == Type::Error) {
            info.type = (comparison || op == BinaryOp::And || op == BinaryOp::Or) ? Type::Bool : Type::Error;
            return record(expression, info);
        }
        const auto incompatible = [&](std::vector<std::string> notes = {}) {
            error(expression.range, concat("Operator '", toString(op), "' cannot be applied to ", quoted(left), " and ",
                                           quoted(right), "."),
                  std::move(notes));
            return Type::Error;
        };
        switch (op) {
        case BinaryOp::And:
        case BinaryOp::Or:
            info.type = Type::Bool;
            if (left != Type::Bool || right != Type::Bool) {
                error(expression.range, concat("Operator '", toString(op), "' requires 'bool' operands, got ", quoted(left),
                                               " and ", quoted(right), "."));
            }
            break;
        case BinaryOp::Add:
            if (left == Type::String && right == Type::String) {
                info.type = Type::String;
            } else if (isNumeric(left) && isNumeric(right)) {
                info.type = unifyNumeric(expression, left, right);
            } else if (left == Type::String || right == Type::String) {
                info.type = incompatible({"Convert the other operand with str(...) to concatenate."});
            } else {
                info.type = incompatible();
            }
            break;
        case BinaryOp::Subtract:
        case BinaryOp::Multiply:
        case BinaryOp::FloorDivide:
        case BinaryOp::Modulo:
            info.type = (isNumeric(left) && isNumeric(right)) ? unifyNumeric(expression, left, right) : incompatible();
            break;
        case BinaryOp::Divide:
            if (isNumeric(left) && isNumeric(right)) {
                if (left == Type::Int) model_.expressions_[expression.left.get()].widenToFloat = true;
                if (right == Type::Int) model_.expressions_[expression.right.get()].widenToFloat = true;
                info.type = Type::Float;
            } else {
                info.type = incompatible({"'/' always divides as float; use '//' for integer division."});
            }
            break;
        case BinaryOp::Equal:
        case BinaryOp::NotEqual:
            info.type = Type::Bool;
            if (isNumeric(left) && isNumeric(right)) {
                unifyNumeric(expression, left, right);
            } else if (left != right || left == Type::None) {
                incompatible();
            }
            break;
        case BinaryOp::Less:
        case BinaryOp::LessEqual:
        case BinaryOp::Greater:
        case BinaryOp::GreaterEqual:
            info.type = Type::Bool;
            if (isNumeric(left) && isNumeric(right)) {
                unifyNumeric(expression, left, right);
            } else if (!(left == Type::String && right == Type::String)) {
                incompatible();
            }
            break;
        }
        return record(expression, info);
    }

    Type checkCall(const Expr& call)
    {
        ExprInfo info;
        std::vector<Type> arguments;
        for (const auto& argument : call.arguments) {
            arguments.push_back(checkExpression(*argument));
        }
        const std::string& name = call.text;
        if (const auto intrinsic = intrinsicFromName(name)) {
            return checkIntrinsic(call, *intrinsic, arguments);
        }
        if (name == "range") {
            error(call.nameRange, "range() can only be used in a for loop: 'for i in range(...):'.");
            info.type = Type::Error;
            return record(call, info);
        }
        const auto symbol = symbols_.find(name);
        if (symbol == symbols_.end()) {
            if (lookupVariable(name)) {
                error(call.nameRange, concat("'", name, "' is a variable, not a function."));
            } else {
                error(call.nameRange, concat("Undefined function '", name, "'."),
                      {"Declare engine functions with 'extern func name(...)'."});
            }
            info.type = Type::Error;
            return record(call, info);
        }
        const std::vector<Type>* parameters = nullptr;
        std::vector<Type> userParameters;
        if (symbol->second.kind == CallKind::Native) {
            const NativeSignature& native = model_.natives_[symbol->second.index];
            parameters = &native.parameters;
            info.type = native.returnType;
        } else {
            const FunctionDecl& function = *model_.functions_[symbol->second.index].declaration;
            for (const Parameter& parameter : function.parameters) {
                userParameters.push_back(parameter.type);
            }
            parameters = &userParameters;
            info.type = function.returnType;
        }
        info.callKind = symbol->second.kind;
        info.callee = symbol->second.index;
        if (arguments.size() != parameters->size()) {
            error(call.range, concat("Function '", name, "' takes ", parameters->size(),
                                     parameters->size() == 1 ? " argument" : " arguments", ", got ", arguments.size(), "."));
        } else {
            for (std::size_t i = 0; i < arguments.size(); ++i) {
                const Expr& argument = *call.arguments[i];
                const Type expected = (*parameters)[i];
                if (arguments[i] == Type::None) {
                    error(argument.range, concat("Argument ", i + 1, " of '", name, "' has no value (type 'None')."));
                } else if (!convertTo(argument, arguments[i], expected)) {
                    error(argument.range, concat("Argument ", i + 1, " of '", name, "' must be of type ", quoted(expected),
                                                 ", got ", quoted(arguments[i]), "."));
                }
            }
        }
        return record(call, info);
    }

    Type checkIntrinsic(const Expr& call, Intrinsic intrinsic, const std::vector<Type>& arguments)
    {
        ExprInfo info;
        info.callKind = CallKind::Intrinsic;
        info.intrinsic = intrinsic;
        info.type = intrinsic == Intrinsic::ToInt ? Type::Int : (intrinsic == Intrinsic::ToFloat ? Type::Float : Type::String);
        if (arguments.size() != 1) {
            error(call.range, concat(call.text, "() takes exactly 1 argument, got ", arguments.size(), "."));
            return record(call, info);
        }
        const Type argument = arguments[0];
        bool valid = argument == Type::Error;
        switch (intrinsic) {
        case Intrinsic::ToInt: valid = valid || argument == Type::Int || argument == Type::Float || argument == Type::Bool; break;
        case Intrinsic::ToFloat: valid = valid || argument == Type::Int || argument == Type::Float; break;
        case Intrinsic::ToString:
            valid = valid || argument == Type::Int || argument == Type::Float || argument == Type::Bool || argument == Type::String;
            break;
        }
        if (!valid) {
            error(call.arguments[0]->range, concat(call.text, "() cannot convert a value of type ", quoted(argument), "."));
        }
        return record(call, info);
    }

    // True if control never reaches the end of the block (every path returns or loops forever).
    static bool alwaysReturns(const Block& block)
    {
        for (const auto& statement : block.statements) {
            if (alwaysReturns(*statement)) {
                return true;
            }
        }
        return false;
    }

    static bool alwaysReturns(const Stmt& statement)
    {
        switch (statement.kind) {
        case StmtKind::Return: return true;
        case StmtKind::If:
            if (!statement.elseBlock || !alwaysReturns(*statement.elseBlock)) return false;
            for (const IfBranch& branch : statement.branches) {
                if (!alwaysReturns(*branch.body)) return false;
            }
            return true;
        case StmtKind::While:
            // "while True:" without break (there is no break) never falls through.
            return statement.value && statement.value->kind == ExprKind::BoolLiteral && statement.value->boolValue;
        default: return false;
        }
    }

    const SourceUnit& unit_;
    Diagnostics& diagnostics_;
    SemanticModel model_;
    std::unordered_map<std::string, FunctionSymbol> symbols_;
    std::vector<std::unordered_map<std::string, std::uint32_t>> scopes_;
    FunctionSemantics* current_ = nullptr;
};

SemanticModel checkTypes(const SourceUnit& unit, Diagnostics& diagnostics)
{
    return TypeCheckerImpl(unit, diagnostics).run();
}

} // namespace schuimm::script
