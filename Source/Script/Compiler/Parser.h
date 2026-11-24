#pragma once

#include "Script/Compiler/Ast.h"
#include "Script/Compiler/Diagnostics.h"
#include "Script/Compiler/Token.h"

#include <memory>
#include <vector>

namespace schuimm::script {

// Recursive-descent parser for statements with a precedence-climbing (Pratt) expression parser.
// Always returns a tree; syntax errors are reported to `diagnostics`, after which the parser
// resynchronises at the next line or block so that one run reports several errors.
//
// `tokens` must come from tokenize() (it ends with EndOfFile) and must outlive the call.
[[nodiscard]] std::unique_ptr<SourceUnit> parse(const std::vector<Token>& tokens, Diagnostics& diagnostics);

} // namespace schuimm::script
