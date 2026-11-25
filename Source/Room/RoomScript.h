#pragma once

// The compiled script of a room: loaded through the existing ScriptHost (validation and native
// linking included) and checked against the room's objects. The compiler is not involved:
// room scripts are compiled at build time (schuimmc) and referenced as .schb assets.

#include "Core/Common/Error.h"
#include "Resource/AssetLocator.h"
#include "Room/RoomResource.h"
#include "Script/Runtime/Program.h"
#include "Script/Runtime/ScriptHost.h"

#include <memory>
#include <string_view>

namespace schuimm::room {

// Reads room.script through `locator` and loads it with `host` (program name: the asset path).
// A room without a script gives nullptr. Errors: the locator's NotFound/IoFailure for a missing
// file, the bytecode validator's or linker's error for an invalid program (prefixed with
// `sourceName`, the room resource), and validateRoomScript's error.
[[nodiscard]] Result<std::shared_ptr<const script::Program>> loadRoomScript(script::ScriptHost& host,
                                                                           const AssetLocator& locator,
                                                                           const RoomResource& room,
                                                                           std::string_view sourceName);

// Every object declaration of `program` ("object blender:") must name an object of the room;
// a script object without one is an error (usually a typo). Room objects without a declaration
// are fine: they simply have no script handlers. A null program is valid (no script).
[[nodiscard]] Status validateRoomScript(const RoomResource& room, const script::Program* program);

} // namespace schuimm::room
