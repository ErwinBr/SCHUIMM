#pragma once

#include "Core/Common/Error.h"
#include "Script/Bytecode/Module.h"
#include "Script/Runtime/NativeRegistry.h"
#include "Script/Runtime/Value.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::script {

// A loaded, validated and linked script: the bytecode module, its constants as runtime values
// and the resolved native functions. Immutable after creation and shared (shared_ptr) by every
// coroutine running it, so it lives as long as its longest-running coroutine.
class Program {
public:
    // Reads and validates .schb bytes (readBytecode), then links (see below).
    [[nodiscard]] static Result<std::shared_ptr<const Program>> load(std::span<const std::byte> bytecode,
                                                                    const NativeRegistry& natives, std::string name = {});

    // Validates the module again (bytecode is never executed unvalidated, also when it comes
    // from the compiler in memory) and resolves every native import by name. Fails with
    // NotFound if a native is not registered and InvalidData if its signature differs.
    // `name` identifies the program in errors and logs; default: the module's source name.
    [[nodiscard]] static Result<std::shared_ptr<const Program>> link(Module module, const NativeRegistry& natives,
                                                                    std::string name = {});

    [[nodiscard]] const Module& module() const noexcept { return module_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const Value& constant(std::size_t index) const noexcept { return constants_[index]; }
    [[nodiscard]] const NativeFunction& native(std::size_t importIndex) const noexcept { return *natives_[importIndex]; }
    // A game-wide (global) function by name. Handlers are not found here: they can only be
    // invoked through their behaviour (findHandler), because they need its context.
    [[nodiscard]] std::optional<std::uint32_t> findFunction(std::string_view name) const;
    [[nodiscard]] const std::string& functionName(std::size_t index) const { return module_.string(module_.functions[index].nameIndex); }

    // A behaviour (index into module().behaviours): the room or actor behaviour of the source
    // `source`, or its object behaviour `name`. An empty `source` matches any source (the first
    // one, in source order). nullopt if there is none, a normal condition.
    [[nodiscard]] std::optional<std::uint32_t> findBehaviour(BehaviourKind kind, std::string_view source,
                                                             std::string_view name = {}) const;
    // The handler `event` of behaviour `behaviour`; nullopt if it has none (not an error).
    [[nodiscard]] std::optional<std::uint32_t> findHandler(std::uint32_t behaviour, std::string_view event) const;
    // Object handlers. `object` is the object identifier of an "object" declaration (the id of
    // the object in the room resource), `event` the handler name. Any names are accepted; a
    // missing object or handler is nullopt (a normal condition, not an error). With several room
    // scripts in the program the first one declaring `object` is used (findBehaviour).
    [[nodiscard]] std::optional<std::uint32_t> findHandler(std::string_view object, std::string_view event) const;
    [[nodiscard]] bool hasHandler(std::string_view object, std::string_view event) const
    {
        return findHandler(object, event).has_value();
    }
    [[nodiscard]] bool hasObject(std::string_view object) const;
    // Whether function `index` belongs to a behaviour (runs for an instance of it).
    [[nodiscard]] bool isHandler(std::size_t index) const noexcept { return owner_[index] != noOwner; }
    // The behaviour function `index` belongs to; nullopt for global functions.
    [[nodiscard]] std::optional<std::uint32_t> behaviourOf(std::size_t index) const noexcept
    {
        return owner_[index] != noOwner ? std::optional<std::uint32_t>(owner_[index]) : std::nullopt;
    }
    // The initial values of the global variables, and of the instance variables of behaviour
    // `behaviour` (GlobalState, BehaviourInstance).
    [[nodiscard]] const std::vector<Value>& globalInitialValues() const noexcept { return globalInitial_; }
    [[nodiscard]] const std::vector<Value>& instanceInitialValues(std::size_t behaviour) const noexcept
    {
        return instanceInitial_[behaviour];
    }
    // The null value of a nullable type (LoadNull): an actor or object reference with id 0 (an
    // entity's null is the null actor reference), the invalid coroutine, the null operation.
    [[nodiscard]] static Value nullValue(TypeRef type) noexcept;

    // "blender.look_at" (or "room.ready", "actor.talk_to") for handlers, the plain name for
    // global functions (errors, logs).
    [[nodiscard]] std::string functionDisplayName(std::size_t index) const;
    // The source file of function `index` (its path in the Sources table); the program name for
    // compiler-generated functions.
    [[nodiscard]] const std::string& functionSource(std::size_t index) const;

private:
    Program() = default;

    static constexpr std::uint32_t noOwner = 0xFFFFFFFFu;

    Module module_;
    std::vector<std::uint32_t> owner_; // Per function: index into module_.behaviours, or noOwner.
    std::string name_;
    std::vector<Value> constants_;
    std::vector<Value> globalInitial_;
    std::vector<std::vector<Value>> instanceInitial_; // Per behaviour.
    std::vector<std::shared_ptr<const NativeFunction>> natives_;
};

} // namespace schuimm::script
