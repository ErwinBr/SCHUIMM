#pragma once

#include "Script/Compiler/Ast.h"
#include "Script/Compiler/Ir.h"
#include "Script/Compiler/TypeChecker.h"

#include <string>

namespace schuimm::script {

// Lowers the type-checked program (the ASTs the model refers to) to IR. Must only be called when
// type checking reported no errors.
[[nodiscard]] IrModule buildIr(const SemanticModel& semantics);

} // namespace schuimm::script
