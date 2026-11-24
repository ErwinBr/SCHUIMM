#include "Script/Runtime/StandardNatives.h"

#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/Semantics.h"
#include "Script/Runtime/ScriptScheduler.h"

namespace schuimm::script {

Status registerStandardNatives(NativeRegistry& registry, PrintSink print)
{
    if (auto status = registry.add({"print", {ScriptType::Any}, ScriptType::None,
                                    [sink = std::move(print)](NativeContext& context, std::span<const Value> arguments) {
                                        if (sink) {
                                            sink(context.script, toDisplayString(arguments[0]));
                                        }
                                        return NativeResult::returnValue();
                                    },
                                    "Writes a value as text to the script output."});
        !status) {
        return status;
    }
    if (auto status = registry.add({"wait", {ScriptType::Float}, ScriptType::None,
                                    [](NativeContext&, std::span<const Value> arguments) {
                                        const double seconds = arguments[0].asFloat();
                                        const auto nanoseconds = secondsToNanoseconds(seconds);
                                        if (!nanoseconds) {
                                            return NativeResult::error(concat(
                                                "the duration must be between 0 and 1e9 seconds, got ", formatFloat(seconds)));
                                        }
                                        return NativeResult::waitFor(*nanoseconds);
                                    },
                                    "Suspends the coroutine for the given number of seconds of game time."});
        !status) {
        return status;
    }
    return registry.add({"yield", {}, ScriptType::None,
                         [](NativeContext&, std::span<const Value>) { return NativeResult::yieldNow(); },
                         "Suspends the coroutine until the next scheduler update."});
}

} // namespace schuimm::script
