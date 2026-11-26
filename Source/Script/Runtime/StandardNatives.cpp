#include "Script/Runtime/StandardNatives.h"

#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/Semantics.h"
#include "Script/Runtime/ScriptScheduler.h"

#include <cmath>

namespace schuimm::script {

namespace {

NativeResult returnVec2(double x, double y)
{
    return NativeResult::returnValue(Value::fromVec2(x, y));
}

} // namespace

Status registerStandardNatives(NativeRegistry& registry, PrintSink print)
{
    std::unordered_map<std::string, NativeImplementation> implementations;
    implementations["print"] = [sink = std::move(print)](NativeContext& context, std::span<const Value> arguments) {
        if (sink) {
            sink(context.script, context.module != nullptr ? toDisplayString(arguments[0], *context.module)
                                                           : toDisplayString(arguments[0]));
        }
        return NativeResult::returnValue();
    };
    implementations["wait"] = [](NativeContext&, std::span<const Value> arguments) {
        const double seconds = arguments[0].asFloat();
        const auto nanoseconds = secondsToNanoseconds(seconds);
        if (!nanoseconds) {
            return NativeResult::error(concat("the duration must be between 0 and 1e9 seconds, got ", formatFloat(seconds)));
        }
        return NativeResult::waitFor(*nanoseconds);
    };
    implementations["yield"] = [](NativeContext&, std::span<const Value>) { return NativeResult::yieldNow(); };
    implementations["await_operation"] = [](NativeContext&, std::span<const Value> arguments) {
        CompletionToken* token = arguments[0].asOperation();
        if (token == nullptr) {
            return NativeResult::error("wait(): the operation is null");
        }
        return NativeResult::await(CompletionRef::share(token));
    };
    implementations["cancel"] = [](NativeContext& context, std::span<const Value> arguments) {
        if (context.scheduler != nullptr) {
            context.scheduler->cancel(arguments[0].asCoroutine()); // Stale or ended: nothing happens.
        }
        return NativeResult::returnValue();
    };
    implementations["length"] = [](NativeContext&, std::span<const Value> arguments) {
        return NativeResult::returnValue(Value::fromFloat(std::hypot(arguments[0].vec2X(), arguments[0].vec2Y())));
    };
    implementations["distance"] = [](NativeContext&, std::span<const Value> arguments) {
        return NativeResult::returnValue(Value::fromFloat(
            std::hypot(arguments[1].vec2X() - arguments[0].vec2X(), arguments[1].vec2Y() - arguments[0].vec2Y())));
    };
    implementations["normalize"] = [](NativeContext&, std::span<const Value> arguments) {
        const double x = arguments[0].vec2X();
        const double y = arguments[0].vec2Y();
        const double length = std::hypot(x, y);
        if (!(length > 0.0) || !std::isfinite(length)) {
            return returnVec2(0.0, 0.0);
        }
        return returnVec2(x / length, y / length);
    };
    implementations["lerp"] = [](NativeContext&, std::span<const Value> arguments) {
        const double t = arguments[2].asFloat();
        const Value& a = arguments[0];
        const Value& b = arguments[1];
        return returnVec2(a.vec2X() + (b.vec2X() - a.vec2X()) * t, a.vec2Y() + (b.vec2Y() - a.vec2Y()) * t);
    };
    return registry.bind(coreInterface(), std::move(implementations));
}

} // namespace schuimm::script
