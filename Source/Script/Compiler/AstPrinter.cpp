#include "Script/Compiler/AstPrinter.h"

#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/Disassembler.h"
#include "Script/Bytecode/Semantics.h"
#include "Script/Compiler/TypeChecker.h"

namespace schuimm::script {

namespace {

std::string syntaxName(const TypeSyntax& type)
{
    return type.type == Type::Named ? type.name : std::string(toString(type.type));
}

class Printer {
public:
    explicit Printer(const SemanticModel* semantics)
        : semantics_(semantics)
    {
    }

    std::string out;

    void line(int depth, const std::string& text)
    {
        out.append(static_cast<std::size_t>(depth) * 2, ' ');
        out += text;
        out += '\n';
    }

    std::string typeSuffix(const Expr& expression) const
    {
        if (semantics_ == nullptr) return {};
        const ExprInfo* info = semantics_->findExpression(expression);
        if (info == nullptr) return {};
        std::string text = concat(" : ", semantics_->typeName(info->type));
        if (info->widenToFloat) text += " -> float";
        return text;
    }

    void expression(const Expr& e, int depth)
    {
        switch (e.kind) {
        case ExprKind::Error: line(depth, "Error"); return;
        case ExprKind::IntLiteral: line(depth, concat("Int ", formatInt(e.intValue), typeSuffix(e))); return;
        case ExprKind::FloatLiteral: line(depth, concat("Float ", formatFloat(e.floatValue), typeSuffix(e))); return;
        case ExprKind::StringLiteral: line(depth, concat("String ", quoteString(e.text), typeSuffix(e))); return;
        case ExprKind::BoolLiteral: line(depth, concat("Bool ", formatBool(e.boolValue), typeSuffix(e))); return;
        case ExprKind::NoneLiteral: line(depth, concat("None", typeSuffix(e))); return;
        case ExprKind::Name: line(depth, concat("Name ", e.text, typeSuffix(e))); return;
        case ExprKind::Self: line(depth, concat("Self", typeSuffix(e))); return;
        case ExprKind::Unary:
            line(depth, concat("Unary ", toString(e.unaryOp), typeSuffix(e)));
            expression(*e.left, depth + 1);
            return;
        case ExprKind::Binary:
            line(depth, concat("Binary ", toString(e.binaryOp), typeSuffix(e)));
            expression(*e.left, depth + 1);
            expression(*e.right, depth + 1);
            return;
        case ExprKind::Spawn:
            line(depth, concat("Spawn", typeSuffix(e)));
            expression(*e.left, depth + 1);
            return;
        case ExprKind::Member:
            line(depth, concat("Member .", e.text, typeSuffix(e)));
            expression(*e.left, depth + 1);
            return;
        case ExprKind::Call:
            line(depth, concat("Call ", e.text, typeSuffix(e)));
            for (const auto& argument : e.arguments) {
                expression(*argument, depth + 1);
            }
            return;
        }
    }

    void block(const Block& b, int depth)
    {
        for (const auto& s : b.statements) {
            statement(*s, depth);
        }
    }

    void statement(const Stmt& s, int depth)
    {
        switch (s.kind) {
        case StmtKind::Pass: line(depth, "Pass"); return;
        case StmtKind::Expression:
            line(depth, "ExprStmt");
            expression(*s.value, depth + 1);
            return;
        case StmtKind::Assign: {
            std::string text = concat(s.declaration ? "Var " : "Assign ", s.name);
            if (!s.member.empty()) text += "." + s.member;
            if (s.annotation) text += concat(": ", syntaxName(*s.annotation));
            if (semantics_ != nullptr) text += semantics_->statement(s).declares ? " (declare)" : " (assign)";
            line(depth, text);
            if (s.value) expression(*s.value, depth + 1);
            return;
        }
        case StmtKind::If:
            for (std::size_t i = 0; i < s.branches.size(); ++i) {
                line(depth, i == 0 ? "If" : "Elif");
                expression(*s.branches[i].condition, depth + 1);
                line(depth, "Then");
                block(*s.branches[i].body, depth + 1);
            }
            if (s.elseBlock) {
                line(depth, "Else");
                block(*s.elseBlock, depth + 1);
            }
            return;
        case StmtKind::While:
            line(depth, "While");
            expression(*s.value, depth + 1);
            line(depth, "Do");
            block(*s.body, depth + 1);
            return;
        case StmtKind::For:
            line(depth, concat("For ", s.name, " in range"));
            for (const auto& argument : s.rangeArguments) {
                expression(*argument, depth + 1);
            }
            line(depth, "Do");
            block(*s.body, depth + 1);
            return;
        case StmtKind::Return:
            line(depth, "Return");
            if (s.value) expression(*s.value, depth + 1);
            return;
        }
    }

private:
    const SemanticModel* semantics_;
};

} // namespace

std::string dumpAst(const SourceUnit& unit, const SemanticModel* semantics)
{
    Printer printer(semantics);
    const auto printFunction = [&](const FunctionDecl& function, int depth) {
        std::string header = concat(function.isExtern ? "ExternFunc " : "Func ", function.name, "(");
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            header += concat(i == 0 ? "" : ", ", function.parameters[i].name, ": ", syntaxName(function.parameters[i].type));
        }
        header += concat(") -> ", syntaxName(function.returnType));
        printer.line(depth, header);
        if (function.body) {
            printer.block(*function.body, depth + 1);
        }
    };
    for (const auto& declaration : unit.enums) {
        std::string text = concat("Enum ", declaration->name, ":");
        for (const EnumMember& member : declaration->members) {
            text += " " + member.name;
        }
        printer.line(0, text);
    }
    for (const auto& variable : unit.variables) {
        printer.statement(*variable, 0);
    }
    for (const auto& function : unit.functions) {
        printFunction(*function, 0);
    }
    for (const auto& object : unit.behaviours) {
        printer.line(0, object->kind == BehaviourKind::Object ? concat("Object ", object->name)
                                                              : std::string(object->kind == BehaviourKind::Room ? "Room" : "Actor"));
        for (const auto& variable : object->variables) {
            printer.statement(*variable, 1);
        }
        for (const auto& handler : object->handlers) {
            printFunction(*handler, 1);
        }
    }
    return std::move(printer.out);
}

std::string formatExpression(const Expr& e)
{
    switch (e.kind) {
    case ExprKind::Error: return "<error>";
    case ExprKind::IntLiteral: return formatInt(e.intValue);
    case ExprKind::FloatLiteral: return formatFloat(e.floatValue);
    case ExprKind::StringLiteral: return quoteString(e.text);
    case ExprKind::BoolLiteral: return formatBool(e.boolValue);
    case ExprKind::NoneLiteral: return "None";
    case ExprKind::Name: return e.text;
    case ExprKind::Self: return "self";
    case ExprKind::Unary:
        return concat("(", toString(e.unaryOp), e.unaryOp == UnaryOp::Not ? " " : "", formatExpression(*e.left), ")");
    case ExprKind::Binary:
        return concat("(", formatExpression(*e.left), " ", toString(e.binaryOp), " ", formatExpression(*e.right), ")");
    case ExprKind::Member: return concat(formatExpression(*e.left), ".", e.text);
    case ExprKind::Spawn: return "spawn " + formatExpression(*e.left);
    case ExprKind::Call: {
        std::string text = e.text + "(";
        for (std::size_t i = 0; i < e.arguments.size(); ++i) {
            text += (i == 0 ? "" : ", ") + formatExpression(*e.arguments[i]);
        }
        return text + ")";
    }
    }
    return "?";
}

} // namespace schuimm::script
