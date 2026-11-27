#include "Script/Compiler/Parser.h"

#include "Core/Common/StringBuilder.h"

#include <optional>

namespace schuimm::script {

const char* toString(UnaryOp op) noexcept { return op == UnaryOp::Negate ? "-" : "not"; }

const char* toString(BinaryOp op) noexcept
{
    switch (op) {
    case BinaryOp::Add: return "+";
    case BinaryOp::Subtract: return "-";
    case BinaryOp::Multiply: return "*";
    case BinaryOp::Divide: return "/";
    case BinaryOp::FloorDivide: return "//";
    case BinaryOp::Modulo: return "%";
    case BinaryOp::Equal: return "==";
    case BinaryOp::NotEqual: return "!=";
    case BinaryOp::Less: return "<";
    case BinaryOp::LessEqual: return "<=";
    case BinaryOp::Greater: return ">";
    case BinaryOp::GreaterEqual: return ">=";
    case BinaryOp::And: return "and";
    case BinaryOp::Or: return "or";
    }
    return "?";
}

bool isComparison(BinaryOp op) noexcept
{
    switch (op) {
    case BinaryOp::Equal:
    case BinaryOp::NotEqual:
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual: return true;
    default: return false;
    }
}

namespace {

// Binding powers, lowest first (Language.md, "Operator precedence").
enum Precedence : int {
    PrecNone = 0,
    PrecOr = 1,
    PrecAnd = 2,
    PrecNot = 3,
    PrecComparison = 4,
    PrecAdditive = 5,
    PrecMultiplicative = 6,
    PrecUnary = 7,
};

struct BinaryInfo {
    int precedence = PrecNone;
    BinaryOp op = BinaryOp::Add;
};

BinaryInfo binaryInfo(TokenKind kind) noexcept
{
    switch (kind) {
    case TokenKind::KwOr: return {PrecOr, BinaryOp::Or};
    case TokenKind::KwAnd: return {PrecAnd, BinaryOp::And};
    case TokenKind::EqualEqual: return {PrecComparison, BinaryOp::Equal};
    case TokenKind::NotEqual: return {PrecComparison, BinaryOp::NotEqual};
    case TokenKind::Less: return {PrecComparison, BinaryOp::Less};
    case TokenKind::LessEqual: return {PrecComparison, BinaryOp::LessEqual};
    case TokenKind::Greater: return {PrecComparison, BinaryOp::Greater};
    case TokenKind::GreaterEqual: return {PrecComparison, BinaryOp::GreaterEqual};
    case TokenKind::Plus: return {PrecAdditive, BinaryOp::Add};
    case TokenKind::Minus: return {PrecAdditive, BinaryOp::Subtract};
    case TokenKind::Star: return {PrecMultiplicative, BinaryOp::Multiply};
    case TokenKind::Slash: return {PrecMultiplicative, BinaryOp::Divide};
    case TokenKind::SlashSlash: return {PrecMultiplicative, BinaryOp::FloorDivide};
    case TokenKind::Percent: return {PrecMultiplicative, BinaryOp::Modulo};
    default: return {};
    }
}

SourceRange join(const SourceRange& a, const SourceRange& b) noexcept
{
    return {a.file, a.begin, b.end > a.end ? b.end : a.end};
}

// Thrown-free error signalling: a failed production returns null/false and sets panic mode;
// the statement loop then skips to the next line.
class Parser {
public:
    Parser(const std::vector<Token>& tokens, Diagnostics& diagnostics)
        : tokens_(tokens)
        , diagnostics_(diagnostics)
    {
    }

    std::unique_ptr<SourceUnit> run()
    {
        auto unit = std::make_unique<SourceUnit>();
        while (!check(TokenKind::EndOfFile)) {
            if (match(TokenKind::Newline)) {
                continue;
            }
            panic_ = false;
            if (check(TokenKind::KwFunc) || check(TokenKind::KwExtern)) {
                if (auto function = parseFunction()) {
                    unit->functions.push_back(std::move(function));
                }
                continue;
            }
            if (check(TokenKind::KwVar)) {
                if (auto variable = parseVariable()) {
                    unit->variables.push_back(std::move(variable));
                }
                continue;
            }
            if (check(TokenKind::KwObject)) {
                if (auto object = parseObject()) {
                    unit->objects.push_back(std::move(object));
                }
                continue;
            }
            if (check(TokenKind::KwEnum)) {
                if (auto declaration = parseEnum()) {
                    unit->enums.push_back(std::move(declaration));
                }
                continue;
            }
            if (check(TokenKind::Indent)) {
                error(current(), "Unexpected indentation.");
                skipIndentedBlock();
                continue;
            }
            if (check(TokenKind::Dedent)) {
                advance();
                continue;
            }
            error(current(), "Expected a declaration ('var', 'func', 'extern func', 'object' or 'enum') at the top level.",
                  {"Statements must be inside a function, e.g. 'func main():'."});
            skipLine();
            if (check(TokenKind::Indent)) {
                skipIndentedBlock();
            }
        }
        return unit;
    }

private:
    // --- Token helpers ------------------------------------------------------------------------
    [[nodiscard]] const Token& current() const noexcept { return tokens_[position_]; }
    [[nodiscard]] const Token& peekToken(std::size_t ahead) const noexcept
    {
        const std::size_t index = position_ + ahead;
        return index < tokens_.size() ? tokens_[index] : tokens_.back();
    }
    [[nodiscard]] const Token& previous() const noexcept { return tokens_[position_ > 0 ? position_ - 1 : 0]; }
    [[nodiscard]] bool check(TokenKind kind) const noexcept { return current().kind == kind; }
    const Token& advance() noexcept
    {
        const Token& token = tokens_[position_];
        if (token.kind != TokenKind::EndOfFile) {
            ++position_;
        }
        return token;
    }
    bool match(TokenKind kind) noexcept
    {
        if (check(kind)) {
            advance();
            return true;
        }
        return false;
    }

    void error(const Token& at, std::string message, std::vector<std::string> notes = {})
    {
        if (panic_) {
            return; // One error per statement: the rest is likely a consequence.
        }
        panic_ = true;
        if (at.kind == TokenKind::Invalid) {
            return; // Already reported by the lexer.
        }
        diagnostics_.error(DiagnosticPhase::Parser, at.range, std::move(message), std::move(notes));
    }

    bool expect(TokenKind kind, const char* context)
    {
        if (match(kind)) {
            return true;
        }
        error(current(), concat("Expected ", describe(kind), " ", context, ", found ", describe(current().kind), "."));
        return false;
    }

    // Skips the rest of the logical line, including its Newline.
    void skipLine()
    {
        int depth = 0;
        while (!check(TokenKind::EndOfFile)) {
            if (depth == 0 && check(TokenKind::Newline)) {
                advance();
                return;
            }
            if (check(TokenKind::Indent)) ++depth;
            if (check(TokenKind::Dedent)) {
                if (depth == 0) return;
                --depth;
            }
            advance();
        }
    }

    void skipIndentedBlock()
    {
        int depth = 0;
        do {
            if (check(TokenKind::Indent)) ++depth;
            if (check(TokenKind::Dedent)) --depth;
            advance();
        } while (depth > 0 && !check(TokenKind::EndOfFile));
    }

    // --- Declarations -------------------------------------------------------------------------
    // A built-in type, or a name the type checker resolves (an enum).
    std::optional<TypeSyntax> parseType()
    {
        const Token& token = current();
        TypeSyntax type;
        type.range = token.range;
        if (match(TokenKind::KwNone)) {
            type.type = Type::None;
            return type;
        }
        if (match(TokenKind::KwObject)) {
            type.type = Type::Object;
            return type;
        }
        if (check(TokenKind::Identifier)) {
            advance();
            if (const auto builtIn = typeFromName(token.lexeme)) {
                type.type = *builtIn;
            } else {
                type.type = Type::Named;
                type.name = std::string(token.lexeme);
            }
            return type;
        }
        error(token, concat("Expected a type name, found ", describe(token.kind), "."));
        return std::nullopt;
    }

    std::unique_ptr<FunctionDecl> parseFunction()
    {
        auto function = std::make_unique<FunctionDecl>();
        const Token& first = current();
        function->isExtern = match(TokenKind::KwExtern);
        if (!expect(TokenKind::KwFunc, function->isExtern ? "after 'extern'" : "")) {
            skipLine();
            return nullptr;
        }
        const Token& name = current();
        if (!expect(TokenKind::Identifier, "for the function name")) {
            skipLine();
            if (check(TokenKind::Indent)) skipIndentedBlock();
            return nullptr;
        }
        function->name = std::string(name.lexeme);
        function->nameRange = name.range;
        if (!parseParameters(*function)) {
            skipLine();
            if (check(TokenKind::Indent)) skipIndentedBlock();
            return nullptr;
        }
        if (match(TokenKind::Arrow)) {
            const auto type = parseType();
            if (!type) {
                skipLine();
                if (check(TokenKind::Indent)) skipIndentedBlock();
                return nullptr;
            }
            function->returnType = *type;
        }
        function->range = join(first.range, previous().range);
        if (function->isExtern) {
            if (check(TokenKind::Colon)) {
                error(current(), "An extern function has no body.",
                      {"Extern functions are implemented by the engine; end the declaration after its signature."});
                skipLine();
                if (check(TokenKind::Indent)) skipIndentedBlock();
                return function;
            }
            if (!expect(TokenKind::Newline, "after the extern declaration")) {
                skipLine();
            }
            return function;
        }
        if (!expect(TokenKind::Colon, "after the function signature")) {
            skipLine();
            if (check(TokenKind::Indent)) skipIndentedBlock();
            return function;
        }
        function->body = parseBlock("function body");
        return function;
    }

    // object := "object" NAME ":" NEWLINE INDENT ("func" ...)+ DEDENT
    std::unique_ptr<ObjectDecl> parseObject()
    {
        auto object = std::make_unique<ObjectDecl>();
        const Token& first = advance(); // 'object'
        const Token& name = current();
        if (!expect(TokenKind::Identifier, "for the object name")) {
            skipLine();
            if (check(TokenKind::Indent)) skipIndentedBlock();
            return nullptr;
        }
        object->name = std::string(name.lexeme);
        object->nameRange = name.range;
        object->range = join(first.range, name.range);
        if (!expect(TokenKind::Colon, "after the object name")) {
            skipLine();
            if (check(TokenKind::Indent)) skipIndentedBlock();
            return object;
        }
        object->range = join(first.range, previous().range);
        if (!check(TokenKind::Newline)) {
            error(current(), concat("Expected a new line after ':' to start the body of object '", object->name, "'."),
                  {"Put each handler function on its own line, indented by one tab."});
            skipLine();
            if (!check(TokenKind::Indent)) {
                return object;
            }
        } else {
            advance();
        }
        if (!check(TokenKind::Indent)) {
            panic_ = false;
            error(current(), concat("Expected an indented block of handler functions for object '", object->name, "'."),
                  {"An object declaration contains at least one function, e.g. 'func look_at():', indented by one tab."});
            return object;
        }
        advance();
        while (!check(TokenKind::Dedent) && !check(TokenKind::EndOfFile)) {
            panic_ = false;
            if (match(TokenKind::Newline)) {
                continue;
            }
            if (check(TokenKind::Indent)) {
                error(current(), "Unexpected indentation.");
                skipIndentedBlock();
                continue;
            }
            if (check(TokenKind::KwFunc)) {
                if (auto handler = parseFunction()) {
                    handler->isHandler = true;
                    object->handlers.push_back(std::move(handler));
                }
                continue;
            }
            if (check(TokenKind::KwVar)) {
                if (auto variable = parseVariable()) {
                    object->variables.push_back(std::move(variable));
                }
                continue;
            }
            if (check(TokenKind::KwExtern)) {
                error(current(), concat("Extern functions cannot be declared inside object '", object->name, "'."),
                      {"Declare engine functions with 'extern func' at the top level of the script."});
            } else if (check(TokenKind::KwObject)) {
                error(current(), "Object declarations cannot be nested.");
            } else {
                error(current(), concat("Only functions and variables are allowed in object '", object->name, "'."),
                      {"An object contains handler functions ('func name(...):') and variables ('var name: type = value'), "
                       "not statements."});
            }
            skipLine();
            if (check(TokenKind::Indent)) skipIndentedBlock();
        }
        match(TokenKind::Dedent);
        return object;
    }

    // enum := "enum" NAME ":" NEWLINE INDENT (NAME NEWLINE)+ DEDENT
    std::unique_ptr<EnumDecl> parseEnum()
    {
        auto declaration = std::make_unique<EnumDecl>();
        const Token& first = advance(); // 'enum'
        const Token& name = current();
        if (!expect(TokenKind::Identifier, "for the enum name")) {
            skipLine();
            if (check(TokenKind::Indent)) skipIndentedBlock();
            return nullptr;
        }
        declaration->name = std::string(name.lexeme);
        declaration->nameRange = name.range;
        declaration->range = join(first.range, name.range);
        if (!expect(TokenKind::Colon, "after the enum name") || !expect(TokenKind::Newline, "after ':'")) {
            skipLine();
            if (check(TokenKind::Indent)) skipIndentedBlock();
            return declaration;
        }
        if (!check(TokenKind::Indent)) {
            panic_ = false;
            error(current(), concat("Expected an indented list of members for enum '", declaration->name, "'."),
                  {"Put each member name on its own line, indented by one tab."});
            return nullptr;
        }
        advance();
        while (!check(TokenKind::Dedent) && !check(TokenKind::EndOfFile)) {
            panic_ = false;
            if (match(TokenKind::Newline)) {
                continue;
            }
            const Token& member = current();
            if (!check(TokenKind::Identifier)) {
                error(member, concat("Expected a member name in enum '", declaration->name, "', found ", describe(member.kind), "."),
                      {"An enum lists one member name per line, e.g. 'NORTH'."});
                skipLine();
                if (check(TokenKind::Indent)) skipIndentedBlock();
                continue;
            }
            advance();
            declaration->members.push_back({std::string(member.lexeme), member.range});
            if (!check(TokenKind::Newline) && !check(TokenKind::Dedent) && !check(TokenKind::EndOfFile)) {
                error(current(), "Expected the end of the line after an enum member.", {"Put each member on its own line."});
                skipLine();
            } else {
                match(TokenKind::Newline);
            }
        }
        match(TokenKind::Dedent);
        declaration->range = join(declaration->range, previous().range);
        return declaration;
    }

    bool parseParameters(FunctionDecl& function)
    {
        if (!expect(TokenKind::LeftParen, "after the function name")) {
            return false;
        }
        while (!check(TokenKind::RightParen)) {
            Parameter parameter;
            const Token& name = current();
            if (!expect(TokenKind::Identifier, "for a parameter name")) {
                return false;
            }
            parameter.name = std::string(name.lexeme);
            parameter.nameRange = name.range;
            if (!check(TokenKind::Colon)) {
                error(current(), concat("Parameter '", parameter.name, "' needs a type annotation."),
                      {concat("Write '", parameter.name, ": int' (or another type, e.g. float, string, actor, vec2).")});
                return false;
            }
            advance();
            const auto type = parseType();
            if (!type) {
                return false;
            }
            if (type->type == Type::None) {
                diagnostics_.error(DiagnosticPhase::Parser, type->range, "A parameter cannot have type None.");
            }
            parameter.type = *type;
            function.parameters.push_back(std::move(parameter));
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
        return expect(TokenKind::RightParen, "to close the parameter list");
    }

    // block := NEWLINE INDENT statement+ DEDENT
    std::unique_ptr<Block> parseBlock(const char* what)
    {
        auto block = std::make_unique<Block>();
        block->range = current().range;
        if (!check(TokenKind::Newline)) {
            error(current(), concat("Expected a new line after ':' to start the ", what, "."),
                  {"Put the statements of a block on their own lines, indented by one tab."});
            skipLine();
            if (!check(TokenKind::Indent)) {
                return block;
            }
        } else {
            advance();
        }
        if (!check(TokenKind::Indent)) {
            panic_ = false;
            error(current(), concat("Expected an indented block for the ", what, "."),
                  {"Indent the statements of a block by one more tab than the line ending with ':'."});
            return block;
        }
        advance();
        while (!check(TokenKind::Dedent) && !check(TokenKind::EndOfFile)) {
            panic_ = false;
            if (match(TokenKind::Newline)) {
                continue;
            }
            if (check(TokenKind::Indent)) {
                error(current(), "Unexpected indentation.");
                // Parse the over-indented lines as part of this block to keep going.
                advance();
                continue;
            }
            if (auto statement = parseStatement()) {
                block->statements.push_back(std::move(statement));
            }
        }
        match(TokenKind::Dedent);
        block->range = join(block->range, previous().range);
        return block;
    }

    // --- Statements ---------------------------------------------------------------------------
    std::unique_ptr<Stmt> parseStatement()
    {
        const Token& first = current();
        auto statement = std::make_unique<Stmt>();
        switch (first.kind) {
        case TokenKind::KwIf: return parseIf();
        case TokenKind::KwWhile: {
            advance();
            statement->kind = StmtKind::While;
            statement->value = parseExpression();
            if (!statement->value || !expect(TokenKind::Colon, "after the while condition")) {
                return recover(std::move(statement));
            }
            statement->range = join(first.range, previous().range);
            statement->body = parseBlock("while loop");
            return statement;
        }
        case TokenKind::KwFor: return parseFor();
        case TokenKind::KwReturn:
            advance();
            statement->kind = StmtKind::Return;
            if (!check(TokenKind::Newline)) {
                statement->value = parseExpression();
                if (!statement->value) return recover(std::move(statement));
            }
            statement->range = join(first.range, previous().range);
            return endOfStatement(std::move(statement));
        case TokenKind::KwPass:
            advance();
            statement->kind = StmtKind::Pass;
            statement->range = first.range;
            return endOfStatement(std::move(statement));
        case TokenKind::KwElif:
        case TokenKind::KwElse:
            error(first, concat(describe(first.kind), " without a matching 'if'."));
            skipLine();
            if (check(TokenKind::Indent)) skipIndentedBlock();
            return nullptr;
        case TokenKind::KwFunc:
        case TokenKind::KwExtern:
            error(first, "Functions cannot be declared inside other functions.");
            skipLine();
            if (check(TokenKind::Indent)) skipIndentedBlock();
            return nullptr;
        case TokenKind::KwObject:
        case TokenKind::KwEnum:
            error(first, first.kind == TokenKind::KwObject ? "Object declarations are only allowed at the top level."
                                                           : "Enum declarations are only allowed at the top level.");
            skipLine();
            if (check(TokenKind::Indent)) skipIndentedBlock();
            return nullptr;
        case TokenKind::KwVar: return parseVariable();
        default: break;
        }
        if (first.kind == TokenKind::Identifier && peekToken(1).kind == TokenKind::Colon) {
            error(first, concat("Variables are declared with 'var': write 'var ", first.lexeme, ": <type> = <value>'."));
            return recover(std::move(statement));
        }
        if (first.kind == TokenKind::Identifier && peekToken(1).kind == TokenKind::Assign) {
            return parseAssignment();
        }
        if (first.kind == TokenKind::Identifier && compoundOperator(peekToken(1).kind)) {
            auto target = std::make_unique<Expr>();
            target->kind = ExprKind::Name;
            target->text = std::string(first.lexeme);
            target->range = first.range;
            advance();
            return parseCompound(std::move(target));
        }
        statement->kind = StmtKind::Expression;
        statement->value = parseExpression();
        if (!statement->value) {
            return recover(std::move(statement));
        }
        if (compoundOperator(current().kind)) {
            const Expr& target = *statement->value;
            if (target.kind == ExprKind::Member && target.left->kind == ExprKind::Name) {
                return parseCompound(std::move(statement->value));
            }
            error(current(), concat("Only a variable or a component of a vec2 variable can be the target of ",
                                    describe(current().kind), "."));
            return recover(std::move(statement));
        }
        if (check(TokenKind::Assign)) {
            const Expr& target = *statement->value;
            if (target.kind == ExprKind::Member && target.left->kind == ExprKind::Name) {
                // "v.x = value": a component of a vec2 variable.
                auto assignment = std::make_unique<Stmt>();
                assignment->kind = StmtKind::Assign;
                assignment->name = target.left->text;
                assignment->nameRange = target.left->range;
                assignment->member = target.text;
                assignment->memberRange = target.nameRange;
                advance(); // '='
                assignment->value = parseExpression();
                if (!assignment->value) {
                    return recover(std::move(assignment));
                }
                assignment->range = join(target.range, assignment->value->range);
                return endOfStatement(std::move(assignment));
            }
            if (target.kind == ExprKind::Member) {
                error(current(), "Only a component of a vec2 variable can be assigned, e.g. 'position.x = 10.0'.");
            } else if (statement->value->kind == ExprKind::Self) {
                error(current(), "'self' cannot be assigned.",
                      {"'self' always refers to the object whose handler is running."});
            } else {
                error(current(), "Only a variable name can be assigned to.");
            }
            return recover(std::move(statement));
        }
        statement->range = statement->value->range;
        return endOfStatement(std::move(statement));
    }

    std::unique_ptr<Stmt> recover(std::unique_ptr<Stmt> statement)
    {
        (void)statement;
        skipLine();
        if (check(TokenKind::Indent)) {
            skipIndentedBlock();
        }
        return nullptr;
    }

    std::unique_ptr<Stmt> endOfStatement(std::unique_ptr<Stmt> statement)
    {
        if (check(TokenKind::Newline)) {
            advance();
            return statement;
        }
        if (check(TokenKind::Dedent) || check(TokenKind::EndOfFile)) {
            return statement;
        }
        error(current(), concat("Expected the end of the line, found ", describe(current().kind), "."));
        skipLine();
        return statement;
    }

    // The binary operator of "+=", "-=", "*=" or "/=".
    static std::optional<BinaryOp> compoundOperator(TokenKind kind) noexcept
    {
        switch (kind) {
        case TokenKind::PlusAssign: return BinaryOp::Add;
        case TokenKind::MinusAssign: return BinaryOp::Subtract;
        case TokenKind::StarAssign: return BinaryOp::Multiply;
        case TokenKind::SlashAssign: return BinaryOp::Divide;
        default: return std::nullopt;
        }
    }

    // target OP= expression, with target a variable name or "v.x": the assignment
    // "target = target OP (expression)". The target has no side effects, so reading it twice is
    // the same as reading it once.
    std::unique_ptr<Stmt> parseCompound(std::unique_ptr<Expr> target)
    {
        auto statement = std::make_unique<Stmt>();
        statement->kind = StmtKind::Assign;
        const BinaryOp op = *compoundOperator(current().kind);
        advance(); // the operator
        if (target->kind == ExprKind::Member) {
            statement->name = target->left->text;
            statement->nameRange = target->left->range;
            statement->member = target->text;
            statement->memberRange = target->nameRange;
        } else {
            statement->name = target->text;
            statement->nameRange = target->range;
        }
        auto operand = parseExpression();
        if (!operand) {
            return recover(std::move(statement));
        }
        auto value = std::make_unique<Expr>();
        value->kind = ExprKind::Binary;
        value->binaryOp = op;
        value->range = join(target->range, operand->range);
        value->left = std::move(target);
        value->right = std::move(operand);
        statement->range = value->range;
        statement->value = std::move(value);
        return endOfStatement(std::move(statement));
    }

    // var := "var" NAME [":" type] ["=" expression]   (without a value, the type is required)
    std::unique_ptr<Stmt> parseVariable()
    {
        auto statement = std::make_unique<Stmt>();
        statement->kind = StmtKind::Assign;
        statement->declaration = true;
        const Token& first = advance(); // 'var'
        const Token& name = current();
        if (!expect(TokenKind::Identifier, "for the variable name")) {
            return recover(std::move(statement));
        }
        statement->name = std::string(name.lexeme);
        statement->nameRange = name.range;
        if (match(TokenKind::Colon)) {
            auto type = parseType();
            if (!type) {
                return recover(std::move(statement));
            }
            if (type->type == Type::None) {
                diagnostics_.error(DiagnosticPhase::Parser, type->range, "A variable cannot have type None.");
                type->type = Type::Error;
            }
            statement->annotation = std::move(*type);
        }
        if (!check(TokenKind::Assign)) {
            if (statement->annotation && (check(TokenKind::Newline) || check(TokenKind::Dedent) || check(TokenKind::EndOfFile))) {
                // "var name: type": the default value of the type.
                statement->range = join(first.range, previous().range);
                return endOfStatement(std::move(statement));
            }
            if (!statement->annotation) {
                error(current(), concat("Variable '", statement->name, "' needs a type or a value."),
                      {concat("Write 'var ", statement->name, " = <value>' or 'var ", statement->name,
                              ": <type>' (which starts with the type's default value).")});
            } else {
                error(current(), concat("Expected '=' or the end of the line after the type of variable '",
                                        statement->name, "'."));
            }
            return recover(std::move(statement));
        }
        advance(); // '='
        statement->value = parseExpression();
        if (!statement->value) {
            return recover(std::move(statement));
        }
        statement->range = join(first.range, statement->value->range);
        return endOfStatement(std::move(statement));
    }

    // NAME "=" expression: assigns an existing variable.
    std::unique_ptr<Stmt> parseAssignment()
    {
        auto statement = std::make_unique<Stmt>();
        statement->kind = StmtKind::Assign;
        const Token& name = advance();
        statement->name = std::string(name.lexeme);
        statement->nameRange = name.range;
        advance(); // '='
        statement->value = parseExpression();
        if (!statement->value) {
            return recover(std::move(statement));
        }
        statement->range = join(name.range, statement->value->range);
        return endOfStatement(std::move(statement));
    }

    std::unique_ptr<Stmt> parseIf()
    {
        auto statement = std::make_unique<Stmt>();
        statement->kind = StmtKind::If;
        const Token& first = current();
        do {
            advance(); // 'if' or 'elif'
            IfBranch branch;
            branch.condition = parseExpression();
            if (!branch.condition || !expect(TokenKind::Colon, "after the condition")) {
                return recover(std::move(statement));
            }
            branch.body = parseBlock(statement->branches.empty() ? "if statement" : "elif branch");
            statement->branches.push_back(std::move(branch));
            panic_ = false;
        } while (check(TokenKind::KwElif));
        if (match(TokenKind::KwElse)) {
            if (!expect(TokenKind::Colon, "after 'else'")) {
                return recover(std::move(statement));
            }
            statement->elseBlock = parseBlock("else branch");
        }
        statement->range = join(first.range, previous().range);
        return statement;
    }

    // for_stmt := 'for' IDENT 'in' 'range' '(' expr [',' expr [',' expr]] ')' ':' block
    std::unique_ptr<Stmt> parseFor()
    {
        auto statement = std::make_unique<Stmt>();
        statement->kind = StmtKind::For;
        const Token& first = advance();
        const Token& name = current();
        if (!expect(TokenKind::Identifier, "for the loop variable")) {
            return recover(std::move(statement));
        }
        statement->name = std::string(name.lexeme);
        statement->nameRange = name.range;
        if (!expect(TokenKind::KwIn, "after the loop variable")) {
            return recover(std::move(statement));
        }
        if (!(check(TokenKind::Identifier) && current().lexeme == "range")) {
            error(current(), "A for loop iterates over range(...).",
                  {"Write 'for i in range(10):', 'range(start, end)' or 'range(start, end, step)'."});
            return recover(std::move(statement));
        }
        advance();
        if (!expect(TokenKind::LeftParen, "after 'range'")) {
            return recover(std::move(statement));
        }
        while (!check(TokenKind::RightParen)) {
            auto argument = parseExpression();
            if (!argument) {
                return recover(std::move(statement));
            }
            statement->rangeArguments.push_back(std::move(argument));
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
        const Token& close = current();
        if (!expect(TokenKind::RightParen, "to close range(")) {
            return recover(std::move(statement));
        }
        if (statement->rangeArguments.empty() || statement->rangeArguments.size() > 3) {
            diagnostics_.error(DiagnosticPhase::Parser, join(name.range, close.range),
                               concat("range() takes 1 to 3 arguments, got ", statement->rangeArguments.size(), "."));
        }
        if (!expect(TokenKind::Colon, "after range(...)")) {
            return recover(std::move(statement));
        }
        statement->range = join(first.range, previous().range);
        statement->body = parseBlock("for loop");
        return statement;
    }

    // --- Expressions --------------------------------------------------------------------------
    std::unique_ptr<Expr> parseExpression(int minimumPrecedence = PrecOr)
    {
        auto left = parsePrefix();
        if (!left) {
            return nullptr;
        }
        while (true) {
            const BinaryInfo info = binaryInfo(current().kind);
            if (info.precedence == PrecNone || info.precedence < minimumPrecedence) {
                break;
            }
            advance();
            auto right = parseExpression(info.precedence + 1); // Left-associative.
            if (!right) {
                return nullptr;
            }
            auto binary = std::make_unique<Expr>();
            binary->kind = ExprKind::Binary;
            binary->binaryOp = info.op;
            binary->range = join(left->range, right->range);
            binary->left = std::move(left);
            binary->right = std::move(right);
            left = std::move(binary);
            if (info.precedence == PrecComparison && binaryInfo(current().kind).precedence == PrecComparison) {
                error(current(), "Comparisons cannot be chained.", {"Write 'a < b and b < c' instead of 'a < b < c'."});
                return nullptr;
            }
        }
        return left;
    }

    // A primary expression followed by any number of ".member" accesses.
    std::unique_ptr<Expr> parsePrefix()
    {
        auto expression = parsePrimary();
        while (expression && check(TokenKind::Dot)) {
            advance();
            const Token& member = current();
            if (!expect(TokenKind::Identifier, "after '.'")) {
                return nullptr;
            }
            auto access = std::make_unique<Expr>();
            access->kind = ExprKind::Member;
            access->text = std::string(member.lexeme);
            access->nameRange = member.range;
            access->range = join(expression->range, member.range);
            access->left = std::move(expression);
            expression = std::move(access);
            if (check(TokenKind::LeftParen)) {
                error(current(), "Values have no methods; call functions by name, e.g. 'length(v)'.");
                return nullptr;
            }
        }
        return expression;
    }

    std::unique_ptr<Expr> parsePrimary()
    {
        const Token& token = current();
        auto expression = std::make_unique<Expr>();
        expression->range = token.range;
        switch (token.kind) {
        case TokenKind::Integer:
            advance();
            if (token.integerIsMinMagnitude) {
                error(token, "Integer literal is too large for int (64-bit).",
                      {"Only -9223372036854775808 may be written with this magnitude."});
                return nullptr;
            }
            expression->kind = ExprKind::IntLiteral;
            expression->intValue = token.intValue;
            return expression;
        case TokenKind::Float:
            advance();
            expression->kind = ExprKind::FloatLiteral;
            expression->floatValue = token.floatValue;
            return expression;
        case TokenKind::String:
            advance();
            expression->kind = ExprKind::StringLiteral;
            expression->text = token.stringValue;
            return expression;
        case TokenKind::KwTrue:
        case TokenKind::KwFalse:
            advance();
            expression->kind = ExprKind::BoolLiteral;
            expression->boolValue = token.kind == TokenKind::KwTrue;
            return expression;
        case TokenKind::KwNone:
            advance();
            expression->kind = ExprKind::NoneLiteral;
            return expression;
        case TokenKind::Identifier:
            advance();
            if (check(TokenKind::LeftParen)) {
                return parseCall(token);
            }
            expression->kind = ExprKind::Name;
            expression->text = std::string(token.lexeme);
            return expression;
        case TokenKind::KwSelf:
            advance();
            expression->kind = ExprKind::Self;
            return expression;
        case TokenKind::KwSpawn: {
            advance();
            const Token& name = current();
            if (!check(TokenKind::Identifier) || peekToken(1).kind != TokenKind::LeftParen) {
                error(name, "'spawn' must be followed by a function call, e.g. 'spawn patrol(guard)'.");
                return nullptr;
            }
            advance();
            auto call = parseCall(name);
            if (!call) {
                return nullptr;
            }
            expression->kind = ExprKind::Spawn;
            expression->range = join(token.range, call->range);
            expression->left = std::move(call);
            return expression;
        }
        case TokenKind::LeftParen: {
            advance();
            auto inner = parseExpression();
            if (!inner) {
                return nullptr;
            }
            if (!expect(TokenKind::RightParen, "to close '('")) {
                return nullptr;
            }
            inner->range = join(token.range, previous().range);
            return inner;
        }
        case TokenKind::Minus: {
            advance();
            // -9223372036854775808 is the one literal whose magnitude exceeds int's maximum.
            if (check(TokenKind::Integer) && current().integerIsMinMagnitude) {
                const Token& literal = advance();
                expression->kind = ExprKind::IntLiteral;
                expression->intValue = literal.intValue;
                expression->range = join(token.range, literal.range);
                return expression;
            }
            auto operand = parseExpression(PrecUnary);
            if (!operand) {
                return nullptr;
            }
            expression->kind = ExprKind::Unary;
            expression->unaryOp = UnaryOp::Negate;
            expression->range = join(token.range, operand->range);
            expression->left = std::move(operand);
            return expression;
        }
        case TokenKind::KwNot: {
            advance();
            auto operand = parseExpression(PrecNot);
            if (!operand) {
                return nullptr;
            }
            expression->kind = ExprKind::Unary;
            expression->unaryOp = UnaryOp::Not;
            expression->range = join(token.range, operand->range);
            expression->left = std::move(operand);
            return expression;
        }
        case TokenKind::Newline:
        case TokenKind::Dedent:
        case TokenKind::EndOfFile:
            error(token, "Expected an expression before the end of the line.");
            return nullptr;
        default:
            error(token, concat("Expected an expression, found ", describe(token.kind), "."));
            return nullptr;
        }
    }

    std::unique_ptr<Expr> parseCall(const Token& name)
    {
        auto call = std::make_unique<Expr>();
        call->kind = ExprKind::Call;
        call->text = std::string(name.lexeme);
        call->nameRange = name.range;
        advance(); // '('
        while (!check(TokenKind::RightParen)) {
            auto argument = parseExpression();
            if (!argument) {
                return nullptr;
            }
            call->arguments.push_back(std::move(argument));
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
        if (!expect(TokenKind::RightParen, "to close the argument list")) {
            return nullptr;
        }
        call->range = join(name.range, previous().range);
        if (check(TokenKind::LeftParen)) {
            error(current(), "Only named functions can be called.");
            return nullptr;
        }
        return call;
    }

    const std::vector<Token>& tokens_;
    Diagnostics& diagnostics_;
    std::size_t position_ = 0;
    bool panic_ = false;
};

} // namespace

std::unique_ptr<SourceUnit> parse(const std::vector<Token>& tokens, Diagnostics& diagnostics)
{
    return Parser(tokens, diagnostics).run();
}

} // namespace schuimm::script
