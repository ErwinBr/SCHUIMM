#pragma once

#include "Core/Common/Error.h"
#include "Script/Bytecode/Module.h"

#include <cstddef>
#include <span>

namespace schuimm::script {

// Parses and validates a .schb file. Bytecode is never trusted: every size, offset, index,
// register and branch target is checked (overflow-safe) before anything can execute it.
// Errors: InvalidData (malformed or inconsistent) or Unsupported (newer format or language
// version). The message names the problem and, where it applies, the byte offset.
[[nodiscard]] Result<Module> readBytecode(std::span<const std::byte> data);

// The semantic checks of readBytecode on an in-memory module (e.g. one the compiler just
// produced): indices, types, registers, call windows, branch targets, terminators.
[[nodiscard]] Status validateModule(const Module& module);

} // namespace schuimm::script
