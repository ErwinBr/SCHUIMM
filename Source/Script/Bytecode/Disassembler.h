#pragma once

#include "Script/Bytecode/Module.h"

#include <string>

namespace schuimm::script {

// Human-readable listing of a module: header, constants, native imports and every function with
// its instructions and source lines. Only for validated modules (indices are not re-checked).
[[nodiscard]] std::string disassemble(const Module& module);

// One instruction, e.g. "AddInt r2, r0, r1" or "LoadConst r0, k3 (30)".
[[nodiscard]] std::string formatInstruction(const Module& module, const Instruction& instruction);

// A constant as it appears in listings: 30, 1.5, "text" (escaped).
[[nodiscard]] std::string formatConstant(const Module& module, const Constant& constant);

// Quotes and escapes text for listings and dumps.
[[nodiscard]] std::string quoteString(const std::string& text);
// "int", "vec2", or the enum name for enum types.
[[nodiscard]] std::string formatType(const Module& module, TypeRef type);

} // namespace schuimm::script
