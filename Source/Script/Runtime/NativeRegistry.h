#pragma once

#include "Core/Common/Error.h"
#include "Script/Runtime/RuntimeTypes.h"
#include "Script/Runtime/Value.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace schuimm::script {

// Information a native function receives about the calling coroutine.
struct NativeContext {
    CoroutineHandle coroutine;
    std::string_view script;          // Program name (normally the source file name).
    std::int64_t timeNanoseconds = 0; // Scheduler time of the current update.
};

// What a native function does after it returns: produce a value, suspend the coroutine (wait,
// yield) or fail. Natives never call back into the VM.
class NativeResult {
public:
    enum class Kind : std::uint8_t { Return, Wait, Yield, Error };

    [[nodiscard]] static NativeResult returnValue(Value value = {}) { return {Kind::Return, std::move(value), 0, {}}; }
    // Suspends the coroutine for `nanoseconds` of scheduler time (>= 0).
    [[nodiscard]] static NativeResult waitFor(std::int64_t nanoseconds) { return {Kind::Wait, {}, nanoseconds, {}}; }
    // Suspends the coroutine until the next scheduler update.
    [[nodiscard]] static NativeResult yieldNow() { return {Kind::Yield, {}, 0, {}}; }
    [[nodiscard]] static NativeResult error(std::string message) { return {Kind::Error, {}, 0, std::move(message)}; }

    Kind kind = Kind::Return;
    Value value;
    std::int64_t waitNanoseconds = 0;
    std::string message;
};

// The implementation receives arguments already checked against the declared parameter types.
using NativeImplementation = std::function<NativeResult(NativeContext& context, std::span<const Value> arguments)>;

struct NativeFunction {
    std::string name;
    std::vector<ScriptType> parameters; // Int, Float, Bool, String or Any (not None).
    ScriptType returnType = ScriptType::None;
    NativeImplementation implementation;
    std::string documentation;
};

// Engine functions callable from scripts. The VM knows how to call them (by signature), not
// what they do. Scripts declare the ones they use ("extern func"), and a Program links each by
// name, checking the signature exactly. Register everything before linking programs; the
// registry may be destroyed afterwards (programs keep the functions they use).
//
// Implementations typically capture references to engine objects; those objects must outlive
// every Program (and therefore every coroutine) that links the function.
class NativeRegistry {
public:
    // Fails with InvalidArgument for an empty or invalid name, a None or Any return type, a
    // None parameter, a missing implementation, or a name that is already registered.
    [[nodiscard]] Status add(NativeFunction function);

    [[nodiscard]] std::shared_ptr<const NativeFunction> find(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> names() const;

private:
    std::unordered_map<std::string, std::shared_ptr<const NativeFunction>> functions_;
};

// "wait(float) -> None"
[[nodiscard]] std::string formatSignature(std::string_view name, const std::vector<ScriptType>& parameters,
                                          ScriptType returnType);

} // namespace schuimm::script
