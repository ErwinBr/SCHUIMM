#pragma once

#include "Core/Common/Error.h"
#include "Script/Bytecode/Module.h"
#include "Script/Bytecode/NativeInterface.h"
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
    const Module* module = nullptr;   // The calling module (set by the VM for each call).
};

// What a native function does after it returns: produce a value, suspend the coroutine (wait,
// yield) or fail. Natives never call back into the VM.
//
// Only natives whose return type is None may suspend: the coroutine resumes after the call,
// with no value for a result. Wait/Yield from a native with a result is a runtime error.
// An exception thrown by an implementation is caught by the VM and becomes a Native error of
// the calling coroutine.
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
// An enum argument carries the member's ordinal (Value::enumOrdinal); an enum result is returned
// as Value::fromEnum(0, ordinal): the VM sets the enum index of the calling module and rejects
// an ordinal outside the enum.
using NativeImplementation = std::function<NativeResult(NativeContext& context, std::span<const Value> arguments)>;

struct NativeFunction {
    NativeFunction() = default;
    NativeFunction(std::string functionName, std::vector<NativeType> parameterTypes, NativeType resultType,
                   NativeImplementation function, std::string text = {})
        : name(std::move(functionName))
        , parameters(std::move(parameterTypes))
        , returnType(std::move(resultType))
        , implementation(std::move(function))
        , documentation(std::move(text))
    {
    }

    std::string name;
    // Not None. Enum types name a registered enum by its qualified name ("adventure.Direction").
    std::vector<NativeType> parameters;
    NativeType returnType = ScriptType::None;
    NativeImplementation implementation;
    std::string documentation;
    NativeCategory category = NativeCategory::Immediate;
    std::string interfaceName; // The interface that declares it; empty for add().
};

// Engine functions callable from scripts. The VM knows how to call them (by signature), not
// what they do. A Program links each native import by name and checks its signature (and the
// identity of the engine enums it uses) against the registered function. Register everything
// before linking programs; the registry may be destroyed afterwards (programs keep the functions
// they use).
//
// Two ways to register:
//   * bind(): implementations for a NativeInterface (the core interface, the adventure API).
//     The signatures come from the interface, which the compiler also uses, so both always
//     agree. Scripts call these without declaring them.
//   * add(): one function with a signature of its own; scripts declare it ("extern func").
//
// Implementations typically capture references to engine objects; those objects must outlive
// every Program (and therefore every coroutine) that links the function.
class NativeRegistry {
public:
    // Fails with InvalidArgument for an empty or invalid name, an Any or Entity return type, a
    // None parameter, an enum type naming no registered enum, a missing implementation, or a
    // name that is already registered.
    [[nodiscard]] Status add(NativeFunction function);

    // Registers every function of `interface` with the implementation of the same name, and its
    // enums under their qualified names. Fails (registering nothing) with InvalidArgument if the
    // interface is malformed (checkInterface), a declaration has no implementation, an
    // implementation matches no declaration, a function name is already registered, or an enum
    // of that qualified name is registered with different members.
    [[nodiscard]] Status bind(const NativeInterface& interface,
                              std::unordered_map<std::string, NativeImplementation> implementations);

    [[nodiscard]] std::shared_ptr<const NativeFunction> find(std::string_view name) const;
    // The members of a registered enum, by qualified name; nullptr if unknown.
    [[nodiscard]] const std::vector<std::string>* findEnum(std::string_view qualifiedName) const;
    [[nodiscard]] std::vector<std::string> names() const;

private:
    std::unordered_map<std::string, std::shared_ptr<const NativeFunction>> functions_;
    std::unordered_map<std::string, std::vector<std::string>> enums_;
};

// "wait(float) -> None"; enum types by qualified name.
[[nodiscard]] std::string formatSignature(const NativeFunction& function);

} // namespace schuimm::script
