#pragma once

#include "Script/Compiler/Ast.h"

#include <string>

namespace schuimm::script {

class SemanticModel;

// Indented tree dump of the AST. With a semantic model each expression also shows its type
// ("Binary + : float") and implicit conversions.
[[nodiscard]] std::string dumpAst(const SourceUnit& unit, const SemanticModel* semantics = nullptr);

// One expression in source-like, fully parenthesised form: ((a + b) * c). Used by tests.
[[nodiscard]] std::string formatExpression(const Expr& expression);

} // namespace schuimm::script
