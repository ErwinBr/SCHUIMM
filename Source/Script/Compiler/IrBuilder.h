#pragma once

#include "Script/Compiler/Ast.h"
#include "Script/Compiler/Ir.h"
#include "Script/Compiler/TypeChecker.h"

#include <string>

namespace schuimm::script {

// Lowers a type-checked AST to IR. Must only be called when type checking reported no errors.
[[nodiscard]] IrModule buildIr(const SourceUnit& unit, const SemanticModel& semantics, std::string sourceName);

} // namespace schuimm::script
