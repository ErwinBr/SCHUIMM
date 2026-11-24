#pragma once

#include "Script/Bytecode/Module.h"
#include "Script/Compiler/Diagnostics.h"
#include "Script/Compiler/Ir.h"

namespace schuimm::script {

// Translates IR into a bytecode module:
//   * blocks are laid out in order; a jump to the next block is omitted (fall-through), a
//     branch becomes one conditional jump plus, if neither target follows, one jump;
//   * virtual registers are renumbered densely (parameters stay r0..rn-1); call arguments that
//     are not already consecutive are copied into a scratch window above the locals;
//   * constants and strings are deduplicated; only natives that are called become imports;
//   * every instruction records its source line and column (debug information).
// Limits of the format (65535 registers or constants) are reported as CodeGen diagnostics.
[[nodiscard]] Module generateBytecode(const IrModule& module, Diagnostics& diagnostics);

} // namespace schuimm::script
