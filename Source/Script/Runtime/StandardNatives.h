#pragma once

#include "Core/Common/Error.h"
#include "Script/Runtime/NativeRegistry.h"

#include <functional>
#include <string_view>

namespace schuimm::script {

// Receives the program name (NativeContext::script) and the text.
using PrintSink = std::function<void(std::string_view program, std::string_view text)>;

// Registers the prelude every script can call without declaring it:
//   print(value: any)      writes toDisplayString(value) to `print`;
//   wait(seconds: float)   suspends the coroutine for that much scheduler time (0 = yield);
//                          negative, NaN, infinite or > 1e9 s is a runtime error;
//   yield()                suspends the coroutine until the next scheduler update.
[[nodiscard]] Status registerStandardNatives(NativeRegistry& registry, PrintSink print);

} // namespace schuimm::script
