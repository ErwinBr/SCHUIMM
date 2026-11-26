#pragma once

#include "Core/Common/Error.h"
#include "Script/Runtime/NativeRegistry.h"

#include <functional>
#include <string_view>

namespace schuimm::script {

// Receives the program name (NativeContext::script) and the text.
using PrintSink = std::function<void(std::string_view program, std::string_view text)>;

// Binds the core interface (coreInterface()), which every script can call without declaring it:
//   print(value: any)                    writes the value as text to `print` (enums by name);
//   wait(seconds: float)                 suspends the coroutine for that much scheduler time
//                                        (0 = yield); negative, NaN, infinite or > 1e9 s is a
//                                        runtime error;
//   yield()                              suspends the coroutine until the next scheduler update;
//   length(v: vec2) -> float             the Euclidean length;
//   distance(a: vec2, b: vec2) -> float  length(b - a);
//   normalize(v: vec2) -> vec2           v / length(v); vec2(0, 0) if the length is zero or not
//                                        finite (never NaN from a zero vector);
//   lerp(a: vec2, b: vec2, t: float) -> vec2
//                                        a + (b - a) * t, t not clamped.
// The vector functions follow float arithmetic otherwise: non-finite components propagate.
[[nodiscard]] Status registerStandardNatives(NativeRegistry& registry, PrintSink print);

} // namespace schuimm::script
