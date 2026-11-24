#pragma once

#include "Core/Common/Error.h"
#include "Script/Bytecode/Module.h"

#include <cstddef>
#include <vector>

namespace schuimm::script {

// Serialises a module into the .schb format. Fails with InvalidArgument if the module cannot be
// encoded (more parameters, strings or instructions than the format allows); it does not
// validate semantics (validateModule does).
[[nodiscard]] Result<std::vector<std::byte>> writeBytecode(const Module& module);

} // namespace schuimm::script
