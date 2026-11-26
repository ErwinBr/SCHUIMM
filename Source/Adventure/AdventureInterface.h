#pragma once

// The adventure scripting API as a native interface: the one declaration of every adventure
// function and of the engine enum Direction. Declarations only, so the compiler can use it
// (schuimmc --interface adventure) without linking any engine subsystem; the implementations
// are bound by AdventureContext (Adventure/AdventureContext.h).

#include "Script/Bytecode/NativeInterface.h"

namespace schuimm::adventure {

// Name "adventure"; its enum is identified as "adventure.Direction".
[[nodiscard]] const script::NativeInterface& adventureInterface();

} // namespace schuimm::adventure
