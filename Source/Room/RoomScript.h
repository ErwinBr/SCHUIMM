#pragma once

// The scripts of a room in the game program. A game's scripts are compiled into one program
// (Compiler.md, "Project compilation"); a room resource's "script" and an actor resource's
// "script" name sources of it. These checks connect resources and program when a room is
// loaded (RoomLibrary) and again when it is instantiated (RoomInstance). The compiler is not
// involved: the program is compiled at build time (schuimmc --game).

#include "Core/Common/Error.h"
#include "Room/ActorResource.h"
#include "Room/RoomResource.h"
#include "Script/Runtime/Program.h"

namespace schuimm::room {

// The room's script (if any) is a room script of `program`, and every object declaration of it
// ("object blender:") names an object of the room: a script object without one is an error
// (usually a typo). Room objects without a declaration are fine: they simply have no handlers.
// A room without a script is valid; one with a script needs a program (InvalidState).
[[nodiscard]] Status validateRoomScript(const RoomResource& room, const script::Program* program);

// The actor resource's script (if any) is an actor script of `program`.
[[nodiscard]] Status validateActorScript(const ActorResource& actor, const script::Program* program);

} // namespace schuimm::room
