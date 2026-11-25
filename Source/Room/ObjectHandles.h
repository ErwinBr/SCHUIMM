#pragma once

// Runtime object handles (script::ObjectRef) for everything a room instance creates: room
// objects and actors draw from one process-wide counter, so no two live or past entities ever
// share a handle (the ObjectRef contract in Script/Runtime/Value.h: never null, never reused).
// 64 bits never wrap in practice.
//
// Internal to the room library; handles are not saved or restored across processes.

#include "Script/Runtime/Value.h"

namespace schuimm::room::detail {

[[nodiscard]] script::ObjectRef allocateObjectHandle() noexcept;

} // namespace schuimm::room::detail
