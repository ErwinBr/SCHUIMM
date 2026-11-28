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

#include <string>
#include <vector>

namespace schuimm::room {

// The room's script (if any) is a room script of `program`, and every object declaration of it
// ("object blender:") names an object of the room: a script object without one is an error
// (usually a typo). Room objects without a declaration are fine: they simply have no handlers.
// A room without a script is valid; one with a script needs a program (InvalidState).
[[nodiscard]] Status validateRoomScript(const RoomResource& room, const script::Program* program);

// The object names the room's script declares ("object coffee_mug:") that the room resource does
// not define, in declaration order; empty without a script (or without such a source in
// `module`). Works on a compiled module, linked or not: the game build checks every room with it
// (GameBuild.h), validateRoomScript at run time.
[[nodiscard]] std::vector<std::string> undefinedScriptObjects(const RoomResource& room, const script::Module& module);

// The actor resource's script (if any) is an actor script of `program`.
[[nodiscard]] Status validateActorScript(const ActorResource& actor, const script::Program* program);

} // namespace schuimm::room
