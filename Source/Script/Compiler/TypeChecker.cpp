#include "Script/Compiler/TypeChecker.h"

#include "Core/Common/StringBuilder.h"

#include <algorithm>
#include <optional>
#include <string_view>

namespace schuimm::script {

std::string SemanticModel::typeName(Type type) const
{
    if (isEnum(type) && enumIndexOf(type) < enums_.size()) {
        return enums_[enumIndexOf(type)].name;
    }
    return toString(type);
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
    if (name == "vec2") return Intrinsic::MakeVec2;
    return std::nullopt;
}

SourceRange noRange()
{
    SourceRange range;
    range.begin = {0, 1, 1};
    return range;
}

bool isEqualityType(Type type) noexcept
{
    return type == Type::Bool || type == Type::String || type == Type::Vec2 || isEnum(type) || isReference(type);
}

} // namespace

class TypeCheckerImpl {
public:
    TypeCheckerImpl(const SourceUnit& unit, Diagnostics& diagnostics, std::span<const NativeInterface* const> interfaces)
        : unit_(unit)
        , diagnostics_(diagnostics)
    {
        interfaces_.push_back(&coreInterface());
        for (const NativeInterface* interface : interfaces) {
            if (interface != nullptr && interface != &coreInterface()) {
                interfaces_.push_back(interface);
            }
        }
    }

    SemanticModel run()
    {
        declareInterfaces();
        declareEnums();
        declareFunctions();
        declareObjects();
        for (std::size_t i = 0; i < model_.functions_.size(); ++i) {
            checkFunction(*model_.functions_[i].declaration, i);
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

    [[nodiscard]] std::string quoted(Type type) const { return concat("'", model_.typeName(type), "'"); }

    // --- Pass 0: interfaces and enums ---------------------------------------------------------
    // Interface functions and engine enums. They have no source position (line 0), which the
    // messages below use to say "built-in".
    void declareInterfaces()
    {
        std::unordered_map<std::string, std::string> interfaceNames;
        for (const NativeInterface* interface : interfaces_) {
            if (const std::string problem = checkInterface(*interface); !problem.empty()) {
                error(noRange(), concat("Native interface '", interface->name, "' is invalid: ", problem, "."));
                continue;
            }
            if (!interfaceNames.emplace(interface->name, interface->name).second) {
                error(noRange(), concat("Native interface '", interface->name, "' is supplied twice."));
                continue;
            }
            std::unordered_map<std::string, std::uint32_t> localEnums;
            for (const EnumDeclaration& declaration : interface->enums) {
                if (const auto existing = enumNames_.find(declaration.name); existing != enumNames_.end()) {
                    error(noRange(), concat("Enum '", declaration.name, "' is declared by both native interface '",
                                            model_.enums_[existing->second].qualifier, "' and '", interface->name, "'."));
                    continue;
                }
                const auto index = static_cast<std::uint32_t>(model_.enums_.size());
                model_.enums_.push_back({declaration.name, interface->name, declaration.members, {}});
                enumNames_.emplace(declaration.name, index);
                localEnums.emplace(declaration.name, index);
            }
            const auto convert = [&](const NativeType& type) {
                if (type.kind == ScriptType::Enum) {
                    const auto found = localEnums.find(type.enumName);
                    return found != localEnums.end() ? enumType(found->second) : Type::Error;
                }
                return fromNativeKind(type.kind);
            };
            for (const NativeDeclaration& declaration : interface->functions) {
                if (const auto existing = symbols_.find(declaration.name); existing != symbols_.end()) {
                    error(noRange(), concat("Function '", declaration.name, "' is declared by both native interface '",
                                            model_.natives_[existing->second.index].interfaceName, "' and '",
                                            interface->name, "'."));
                    continue;
                }
                NativeSignature native;
                native.name = declaration.name;
                for (const NativeType& type : declaration.parameters) {
                    native.parameters.push_back(convert(type));
                }
                native.returnType = convert(declaration.returnType);
                native.pure = declaration.pure;
                native.interfaceName = interface->name;
                native.hidden = declaration.hidden;
                const auto index = static_cast<std::uint32_t>(model_.natives_.size());
                model_.natives_.push_back(std::move(native));
                symbols_[declaration.name] = {CallKind::Native, index, {}};
            }
        }
    }

    static Type fromNativeKind(ScriptType kind) noexcept
    {
        switch (kind) {
        case ScriptType::None: return Type::None;
        case ScriptType::Bool: return Type::Bool;
        case ScriptType::Int: return Type::Int;
        case ScriptType::Float: return Type::Float;
        case ScriptType::String: return Type::String;
        case ScriptType::Any: return Type::Any;
        case ScriptType::Object: return Type::Object;
        case ScriptType::Actor: return Type::Actor;
        case ScriptType::Entity: return Type::Entity;
        case ScriptType::Vec2: return Type::Vec2;
        case ScriptType::Coroutine: return Type::Coroutine;
        case ScriptType::Operation: return Type::Operation;
        case ScriptType::Enum: return Type::Error;
        }
        return Type::Error;
    }

    // Where a name is already used at the top level, for "X has the same name as ..." notes;
    // empty if it is free. Functions and natives are in symbols_, enums in enumNames_.
    [[nodiscard]] std::string describeExisting(const std::string& name) const
    {
        if (const auto symbol = symbols_.find(name); symbol != symbols_.end()) {
            if (symbol->second.declaration.begin.line == 0) return concat("'", name, "' is a built-in function.");
            return concat("A function '", name, "' was declared on line ", symbol->second.declaration.begin.line, ".");
        }
        if (const auto found = enumNames_.find(name); found != enumNames_.end()) {
            const EnumInfo& info = model_.enums_[found->second];
            if (info.isEngine()) return concat("'", name, "' is a built-in enum.");
            return concat("An enum '", name, "' was declared on line ", info.declaration.begin.line, ".");
        }
        return {};
    }

    void declareEnums()
    {
        for (const auto& declaration : unit_.enums) {
            const std::string& name = declaration->name;
            if (isReservedName(name)) {
                error(declaration->nameRange, concat("'", name, "' is a built-in name and cannot be used as an enum name."));
                continue;
            }
            if (const std::string existing = describeExisting(name); !existing.empty()) {
                error(declaration->nameRange, concat("Duplicate declaration of '", name, "'."), {existing});
                continue;
            }
            if (declaration->members.empty()) {
                error(declaration->nameRange, concat("Enum '", name, "' has no members."));
            }
            EnumInfo info;
            info.name = name;
            info.declaration = declaration->nameRange;
            std::unordered_map<std::string, SourceRange> members;
            for (const EnumMember& member : declaration->members) {
                if (const auto existing = members.find(member.name); existing != members.end()) {
                    error(member.range, concat("Duplicate member '", member.name, "' in enum '", name, "'."),
                          {concat("'", member.name, "' was first declared on line ", existing->second.begin.line, ".")});
                    continue;
                }
                members.emplace(member.name, member.range);
                info.members.push_back(member.name);
            }
            if (model_.enums_.size() >= maxEnumTypes) {
                error(declaration->nameRange, "Too many enums.");
                continue;
            }
            enumNames_.emplace(name, static_cast<std::uint32_t>(model_.enums_.size()));
            model_.enums_.push_back(std::move(info));
        }
    }

    // A type annotation: built-in types as written, names as enums.
    Type resolve(const TypeSyntax& syntax)
    {
        if (syntax.type != Type::Named) {
            return syntax.type;
        }
        if (const auto found = enumNames_.find(syntax.name); found != enumNames_.end()) {
            return enumType(found->second);
        }
        error(syntax.range, concat("Unknown type '", syntax.name, "'."),
              {"The types are int, float, bool, string, vec2, entity, actor, object, coroutine, operation and enums."});
        return Type::Error;
    }

    // --- Pass 1: function and extern signatures ----------------------------------------------
    void declareFunctions()
    {
        std::uint32_t functionIndex = 0;
        for (const auto& function : unit_.functions) {
            const std::string& name = function->name;
            bool valid = true;
            if (isReservedName(name)) {
                error(function->nameRange, concat("'", name, "' is a built-in name and cannot be redefined."));
                valid = false;
            } else if (const auto existing = symbols_.find(name); existing != symbols_.end()) {
                if (existing->second.kind == CallKind::Native && existing->second.declaration.begin.line == 0) {
                    error(function->nameRange, concat("'", name, "' is a built-in function and cannot be redefined."),
                          {concat("It is declared by the native interface '",
                                  model_.natives_[existing->second.index].interfaceName,
                                  "', which every script of this kind can call without declaring it.")});
                } else {
                    error(function->nameRange, concat("Duplicate declaration of function '", name, "'."),
                          {concat("'", name, "' was first declared on line ", existing->second.declaration.begin.line, ".")});
                }
                valid = false;
            } else if (const std::string other = describeExisting(name); !other.empty()) {
                error(function->nameRange, concat("Function '", name, "' has the same name as an enum."), {other});
                valid = false;
            }
            checkParameterNames(*function);
            std::vector<Type> parameters;
            for (const Parameter& parameter : function->parameters) {
                parameters.push_back(resolve(parameter.type));
            }
            const Type returnType = resolve(function->returnType);
            if (function->isExtern) {
                NativeSignature native;
                native.name = name;
                native.parameters = parameters;
                native.returnType = returnType;
                const auto index = static_cast<std::uint32_t>(model_.natives_.size());
                model_.natives_.push_back(std::move(native));
                if (valid) {
                    symbols_[name] = {CallKind::Native, index, function->nameRange};
                }
            } else {
                FunctionSemantics semantics;
                semantics.declaration = function.get();
                semantics.parameters = std::move(parameters);
                semantics.returnType = returnType;
                model_.functions_.push_back(std::move(semantics));
                if (valid) {
                    symbols_[name] = {CallKind::Function, functionIndex, function->nameRange};
                }
                ++functionIndex;
            }
        }
    }

    void checkParameterNames(const FunctionDecl& function)
    {
        for (std::size_t p = 0; p < function.parameters.size(); ++p) {
            for (std::size_t q = 0; q < p; ++q) {
                if (function.parameters[p].name == function.parameters[q].name) {
                    error(function.parameters[p].nameRange, concat("Duplicate parameter '", function.parameters[p].name, "'."));
                }
            }
        }
    }

    // Object declarations and their handlers. Objects share the room namespace (an object
    // cannot be named like a function or native); handler names are scoped to their object.
    void declareObjects()
    {
        std::unordered_map<std::string, SourceRange> objectNames;
        for (std::size_t o = 0; o < unit_.objects.size(); ++o) {
            const ObjectDecl& object = *unit_.objects[o];
            const auto objectIndex = static_cast<std::uint32_t>(o);
            if (isReservedName(object.name)) {
                error(object.nameRange, concat("'", object.name, "' is a built-in name and cannot be used as an object name."));
            } else if (const auto existing = objectNames.find(object.name); existing != objectNames.end()) {
                error(object.nameRange, concat("Duplicate declaration of object '", object.name, "'."),
                      {concat("'", object.name, "' was first declared on line ", existing->second.begin.line,
                              "; put all handlers of an object in one declaration.")});
            } else if (const std::string other = describeExisting(object.name); !other.empty()) {
                error(object.nameRange,
                      concat("Object '", object.name, "' has the same name as ",
                             symbols_.count(object.name) != 0 ? "a function." : "an enum."),
                      {other});
            } else {
                objectNames.emplace(object.name, object.nameRange);
            }
            ObjectSemantics semantics;
            semantics.declaration = &object;
            std::unordered_map<std::string, SourceRange> handlerNames;
            for (const auto& handler : object.handlers) {
                checkParameterNames(*handler);
                const auto functionIndex = static_cast<std::uint32_t>(model_.functions_.size());
                FunctionSemantics function;
                function.declaration = handler.get();
                function.object = objectIndex;
                for (const Parameter& parameter : handler->parameters) {
                    function.parameters.push_back(resolve(parameter.type));
                }
                function.returnType = resolve(handler->returnType);
                model_.functions_.push_back(std::move(function));
                if (isReservedName(handler->name)) {
                    error(handler->nameRange, concat("'", handler->name, "' is a built-in name and cannot be used as a handler name."));
                } else if (const auto existing = handlerNames.find(handler->name); existing != handlerNames.end()) {
                    error(handler->nameRange,
                          concat("Duplicate handler '", handler->name, "' in object '", object.name, "'."),
                          {concat("'", handler->name, "' was first declared on line ", existing->second.begin.line, ".")});
                } else {
                    handlerNames.emplace(handler->name, handler->nameRange);
                    semantics.handlers.push_back({handler->name, functionIndex});
                }
            }
            model_.objects_.push_back(std::move(semantics));
        }
    }

    // "blender.use" for handlers, "use" for room-level functions (messages only).
    [[nodiscard]] std::string displayName(const FunctionDecl& function) const
    {
        if (current_ != nullptr && current_->isHandler()) {
            return concat(unit_.objects[current_->object]->name, ".", function.name);
        }
        return function.name;
    }

    // --- Pass 2: function bodies -------------------------------------------------------------
    void checkFunction(const FunctionDecl& function, std::size_t index)
    {
        current_ = &model_.functions_[index];
        scopes_.clear();
        scopes_.emplace_back();
        if (current_->isHandler()) {
            declareVariable("self", Type::Object, function.nameRange, true); // Variable 0: the hidden parameter.
        }
        for (std::size_t p = 0; p < function.parameters.size(); ++p) {
            const Parameter& parameter = function.parameters[p];
            if (scopes_.back().count(parameter.name) == 0) {
                declareVariable(parameter.name, current_->parameters[p], parameter.nameRange, true);
            }
        }
        if (function.body) {
            const Type returnType = current_->returnType;
            checkBlock(*function.body, function);
            if (returnType != Type::None && returnType != Type::Error && !alwaysReturns(*function.body)) {
                error(function.nameRange,
                      concat("Function '", displayName(function), "' must return a value of type ", quoted(returnType),
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
        } else if (enumNames_.count(name) != 0) {
            error(range, concat("'", name, "' is the name of an enum and cannot be used as a variable name."));
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
            if (value.kind != ExprKind::Call && value.kind != ExprKind::Spawn && value.kind != ExprKind::Error) {
                std::vector<std::string> notes;
                if (value.kind == ExprKind::Binary && value.binaryOp == BinaryOp::Equal && value.left->kind == ExprKind::Name) {
                    notes.push_back("Use '=' to assign a value; '==' compares.");
                }
                error(value.range, "This expression has no effect; only function calls and spawn can be used as statements.",
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
            const Type returnType = current_->returnType;
            if (!statement.value) {
                if (returnType != Type::None && returnType != Type::Error) {
                    error(statement.range, concat("Function '", function.name, "' must return a value of type ",
                                                  quoted(returnType), "."));
                }
                return;
            }
            const Type type = checkExpression(*statement.value);
            if (returnType == Type::None) {
                if (type != Type::None && type != Type::Error) {
                    error(statement.value->range, concat("Function '", function.name, "' has no return type and cannot return a value."),
                          {concat("Declare the return type: 'func ", function.name, "(...) -> ", model_.typeName(type), ":'.")});
                }
                return;
            }
            if (!convertTo(*statement.value, type, returnType)) {
                error(statement.value->range, concat("Cannot return a value of type ", quoted(type), " from function '",
                                                     function.name, "', which returns ", quoted(returnType), "."));
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
        const auto noValue = [&]() {
            error(statement.value->range, "Cannot assign a value of type 'None': the expression has no value.");
        };
        if (statement.declaration) {
            if (existing) {
                const VariableInfo& previous = current_->variables[*existing];
                error(statement.nameRange,
                      concat(previous.isParameter ? "Parameter '" : "Variable '", statement.name, "' is already declared."),
                      {concat("It was declared on line ", previous.declaration.begin.line,
                              "; assign it without 'var' to change it, or choose another name.")});
                return;
            }
            Type declared = statement.annotation ? resolve(*statement.annotation) : type;
            if (type == Type::None) {
                noValue();
                if (!statement.annotation) declared = Type::Error;
            } else if (!convertTo(*statement.value, type, declared)) {
                error(statement.value->range, concat("Cannot assign a value of type ", quoted(type), " to variable '",
                                                     statement.name, "' of type ", quoted(declared), "."));
            }
            info.variable = declareVariable(statement.name, declared, statement.nameRange, false);
            info.declares = true;
            model_.statements_[&statement] = info;
            return;
        }
        if (!existing) {
            std::vector<std::string> notes;
            if (statement.member.empty()) {
                notes.push_back(concat("Declare a new variable with 'var ", statement.name, " = ...'."));
            }
            error(statement.nameRange, concat("Undefined variable '", statement.name, "'."), std::move(notes));
            // Declared anyway (as the value's type), so later uses do not report it again.
            info.variable = declareVariable(statement.name, type == Type::None ? Type::Error : type, statement.nameRange, false);
            info.declares = true;
            model_.statements_[&statement] = info;
            return;
        }
        const VariableInfo& variable = current_->variables[*existing];
        info.variable = *existing;
        if (!statement.member.empty()) {
            if (variable.type != Type::Vec2 && variable.type != Type::Error) {
                error(statement.memberRange, concat("Variable '", statement.name, "' of type ", quoted(variable.type),
                                                    " has no component '", statement.member, "'."));
            } else if (statement.member != "x" && statement.member != "y") {
                error(statement.memberRange, concat("A vec2 has components 'x' and 'y', not '", statement.member, "'."));
            } else if (type == Type::None) {
                noValue();
            } else if (!convertTo(*statement.value, type, Type::Float)) {
                error(statement.value->range, concat("Cannot assign a value of type ", quoted(type), " to component '",
                                                     statement.member, "' of type 'float'."));
            }
            info.component = true;
            info.componentIndex = statement.member == "y" ? 1 : 0;
            model_.statements_[&statement] = info;
            return;
        }
        if (type == Type::None) {
            noValue();
        } else if (!convertTo(*statement.value, type, variable.type)) {
            error(statement.value->range, concat("Cannot assign a value of type ", quoted(type), " to variable '",
                                                 statement.name, "' of type ", quoted(variable.type), "."),
                  {concat("'", statement.name, "' was declared as ", quoted(variable.type), " on line ",
                          variable.declaration.begin.line, ".")});
        }
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
        case ExprKind::Self:
            if (current_ == nullptr || !current_->isHandler()) {
                error(expression.range, "'self' is only available inside object handlers.",
                      {"Declare the function inside an 'object name:' block to use 'self'."});
                info.type = Type::Error;
            } else {
                info.type = Type::Object;
                info.variable = 0;
            }
            break;
        case ExprKind::Unary: return checkUnary(expression);
        case ExprKind::Binary: return checkBinary(expression);
        case ExprKind::Call: return checkCall(expression);
        case ExprKind::Member: return checkMember(expression);
        case ExprKind::Spawn: return checkSpawn(expression);
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
        } else if (enumNames_.count(name) != 0) {
            error(expression.range, concat("'", name, "' is an enum; name one of its members, e.g. '", name, ".",
                                           model_.enums_[enumNames_.at(name)].members.empty()
                                               ? std::string("MEMBER")
                                               : model_.enums_[enumNames_.at(name)].members.front(),
                                           "'."));
        } else {
            error(expression.range, concat("Undefined variable '", name, "'."), std::move(notes));
        }
        info.type = Type::Error;
        return record(expression, info);
    }

    // "Enum.MEMBER" (when the left side names an enum, not a variable) or "vector.x" / ".y".
    Type checkMember(const Expr& expression)
    {
        ExprInfo info;
        const Expr& object = *expression.left;
        if (object.kind == ExprKind::Name && !lookupVariable(object.text)) {
            if (const auto found = enumNames_.find(object.text); found != enumNames_.end()) {
                const EnumInfo& declaration = model_.enums_[found->second];
                info.type = enumType(found->second);
                info.isEnumMember = true;
                bool known = false;
                for (std::size_t m = 0; m < declaration.members.size(); ++m) {
                    if (declaration.members[m] == expression.text) {
                        info.enumMember = static_cast<std::uint32_t>(m);
                        known = true;
                    }
                }
                if (!known) {
                    std::string members;
                    for (std::size_t m = 0; m < declaration.members.size(); ++m) {
                        members += concat(m == 0 ? "" : ", ", declaration.members[m]);
                    }
                    error(expression.nameRange, concat("Enum '", declaration.name, "' has no member '", expression.text, "'."),
                          {concat("The members are ", members, ".")});
                    info.type = Type::Error;
                }
                return record(expression, info);
            }
        }
        const Type type = checkExpression(object);
        if (type == Type::Error) {
            info.type = Type::Error;
        } else if (type != Type::Vec2) {
            error(expression.nameRange, concat("A value of type ", quoted(type), " has no member '", expression.text, "'."));
            info.type = Type::Error;
        } else if (expression.text != "x" && expression.text != "y") {
            error(expression.nameRange, concat("A vec2 has components 'x' and 'y', not '", expression.text, "'."));
            info.type = Type::Error;
        } else {
            info.type = Type::Float;
            info.component = expression.text == "y" ? 1 : 0;
        }
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
        } else if (isNumeric(operand) || operand == Type::Vec2) {
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

    void widen(const Expr& operand, Type type)
    {
        if (type == Type::Int) model_.expressions_[&operand].widenToFloat = true;
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
            } else if (left == Type::Vec2 && right == Type::Vec2) {
                info.type = Type::Vec2;
            } else if (isNumeric(left) && isNumeric(right)) {
                info.type = unifyNumeric(expression, left, right);
            } else if (left == Type::String || right == Type::String) {
                info.type = incompatible({"Convert the other operand with str(...) to concatenate."});
            } else {
                info.type = incompatible();
            }
            break;
        case BinaryOp::Subtract:
            if (left == Type::Vec2 && right == Type::Vec2) {
                info.type = Type::Vec2;
                break;
            }
            info.type = (isNumeric(left) && isNumeric(right)) ? unifyNumeric(expression, left, right) : incompatible();
            break;
        case BinaryOp::Multiply:
            if (left == Type::Vec2 && isNumeric(right)) {
                widen(*expression.right, right);
                info.type = Type::Vec2;
                break;
            }
            if (isNumeric(left) && right == Type::Vec2) {
                widen(*expression.left, left);
                info.type = Type::Vec2;
                info.swapOperands = true;
                break;
            }
            info.type = (isNumeric(left) && isNumeric(right)) ? unifyNumeric(expression, left, right) : incompatible();
            break;
        case BinaryOp::FloorDivide:
        case BinaryOp::Modulo:
            info.type = (isNumeric(left) && isNumeric(right)) ? unifyNumeric(expression, left, right) : incompatible();
            break;
        case BinaryOp::Divide:
            if (left == Type::Vec2 && isNumeric(right)) {
                widen(*expression.right, right);
                info.type = Type::Vec2;
            } else if (isNumeric(left) && isNumeric(right)) {
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
            } else if (isReference(left) && isReference(right)) {
                if ((left == Type::Actor && right == Type::Object) || (left == Type::Object && right == Type::Actor)) {
                    incompatible({"An actor is never the same entity as a room object."});
                }
            } else if (left != right || !isEqualityType(left)) {
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
        // wait(operation) waits for an engine operation: the hidden native await_operation. This
        // is the only overload in the language; wait(float) is the core function itself.
        const bool waitForOperation = name == "wait" && arguments.size() == 1 && arguments[0] == Type::Operation;
        auto symbol = symbols_.find(waitForOperation ? std::string("await_operation") : name);
        if (symbol != symbols_.end() && symbol->second.kind == CallKind::Native && !waitForOperation &&
            model_.natives_[symbol->second.index].hidden) {
            symbol = symbols_.end();
        }
        if (symbol == symbols_.end()) {
            if (lookupVariable(name)) {
                error(call.nameRange, concat("'", name, "' is a variable, not a function."));
            } else if (isSiblingHandler(name)) {
                error(call.nameRange, concat("Undefined function '", name, "'."),
                      {concat("'", name, "' is a handler of object '", unit_.objects[current_->object]->name,
                              "'; handlers are invoked by the engine and cannot be called directly. "
                              "Move shared code into a room-level function.")});
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
            const FunctionSemantics& function = model_.functions_[symbol->second.index];
            userParameters = function.parameters;
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

    // "spawn f(args)": the call is checked as usual; the expression is the new coroutine.
    Type checkSpawn(const Expr& expression)
    {
        const Expr& call = *expression.left;
        checkCall(call);
        ExprInfo info;
        info.type = Type::Coroutine;
        const ExprInfo& target = model_.expressions_[&call];
        if (target.callee == invalidIndex) {
            // The call itself was reported (undefined function, range(), ...).
            if (target.callKind == CallKind::Intrinsic) {
                error(call.nameRange, concat("'", call.text, "' is built into the language and cannot be spawned."));
            }
            return record(expression, info);
        }
        if (target.callKind == CallKind::Function) {
            info.callee = target.callee;
            return record(expression, info);
        }
        const NativeSignature& native = model_.natives_[target.callee];
        for (const Type parameter : native.parameters) {
            if (parameter == Type::Any) {
                error(call.nameRange, concat("'", call.text, "' takes a value of any type and cannot be spawned."),
                      {"Spawn a script function that calls it instead."});
                return record(expression, info);
            }
        }
        // A native runs in a compiler-generated wrapper function (one per spawned native).
        const auto existing = std::find(model_.spawnWrappers_.begin(), model_.spawnWrappers_.end(), target.callee);
        const auto wrapper = static_cast<std::uint32_t>(existing - model_.spawnWrappers_.begin());
        if (existing == model_.spawnWrappers_.end()) {
            model_.spawnWrappers_.push_back(target.callee);
        }
        info.callee = static_cast<std::uint32_t>(model_.functions_.size()) + wrapper;
        return record(expression, info);
    }

    [[nodiscard]] bool isSiblingHandler(const std::string& name) const
    {
        if (current_ == nullptr || !current_->isHandler()) {
            return false;
        }
        for (const auto& handler : unit_.objects[current_->object]->handlers) {
            if (handler->name == name) return true;
        }
        return false;
    }

    Type checkIntrinsic(const Expr& call, Intrinsic intrinsic, const std::vector<Type>& arguments)
    {
        ExprInfo info;
        info.callKind = CallKind::Intrinsic;
        info.intrinsic = intrinsic;
        if (intrinsic == Intrinsic::MakeVec2) {
            info.type = Type::Vec2;
            if (arguments.size() != 2) {
                error(call.range, concat("vec2() takes exactly 2 arguments (x, y), got ", arguments.size(), "."));
                return record(call, info);
            }
            for (std::size_t i = 0; i < 2; ++i) {
                if (isNumeric(arguments[i])) {
                    widen(*call.arguments[i], arguments[i]);
                } else if (arguments[i] != Type::Error) {
                    error(call.arguments[i]->range, concat("Argument ", i + 1, " of vec2() must be a number, got ",
                                                           quoted(arguments[i]), "."));
                }
            }
            return record(call, info);
        }
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
        case Intrinsic::MakeVec2: break;
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
    std::vector<const NativeInterface*> interfaces_;
    std::unordered_map<std::string, FunctionSymbol> symbols_;
    std::unordered_map<std::string, std::uint32_t> enumNames_;
    std::vector<std::unordered_map<std::string, std::uint32_t>> scopes_;
    FunctionSemantics* current_ = nullptr;
};

SemanticModel checkTypes(const SourceUnit& unit, Diagnostics& diagnostics, std::span<const NativeInterface* const> interfaces)
{
    return TypeCheckerImpl(unit, diagnostics, interfaces).run();
}

} // namespace schuimm::script
