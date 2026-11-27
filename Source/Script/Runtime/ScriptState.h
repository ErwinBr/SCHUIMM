#pragma once

// The mutable state of a running program, kept apart from the immutable Program:
//
//   GlobalState        the global variables: one storage location per declaration, for one game
//                      session. Created from a program, initialised from its declarations' initial
//                      values; destroying it ends the session's state.
//   BehaviourInstance  the instance variables of one room, actor or object behaviour instance,
//                      and the entity it runs for ("self"). Created by whoever owns the entity
//                      (a room instance), one per entity, so instances never share variables.
//
// Coroutines hold both by shared ownership (Coroutine), so storage is never freed under a call
// frame. That keeps script storage alive, never the engine entity: an instance whose entity is
// gone is expired (expire()) and no new handler is dispatched to it, and the coroutines running
// for it are cancelled by their execution owner. Not thread-safe (game thread only), like all
// script values.

#include "Core/Common/Error.h"
#include "Script/Runtime/Program.h"
#include "Script/Runtime/Value.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace schuimm::script {

class GlobalState {
public:
    // The program's global variables with their initial values.
    [[nodiscard]] static std::shared_ptr<GlobalState> create(std::shared_ptr<const Program> program);

    [[nodiscard]] const std::shared_ptr<const Program>& program() const noexcept { return program_; }
    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
    [[nodiscard]] Value& value(std::size_t index) noexcept { return values_[index]; }
    [[nodiscard]] const Value& value(std::size_t index) const noexcept { return values_[index]; }
    // A global variable by name (tests, diagnostics); nullptr if the program declares none.
    [[nodiscard]] const Value* find(std::string_view name) const;

private:
    GlobalState() = default;

    std::shared_ptr<const Program> program_;
    std::vector<Value> values_;
};

class BehaviourInstance {
public:
    // An instance of behaviour `behaviour` (index into the program's behaviours) of `globals`'
    // program, with its instance variables at their initial values. `self` is the entity it runs
    // for: an actor reference for actor behaviours, an object reference for object behaviours,
    // none for room behaviours. InvalidArgument for an unknown behaviour, a missing program or a
    // self of the wrong kind (a null reference included).
    [[nodiscard]] static Result<std::shared_ptr<BehaviourInstance>> create(std::shared_ptr<GlobalState> globals,
                                                                          std::uint32_t behaviour, Value self = {});

    [[nodiscard]] const std::shared_ptr<GlobalState>& globals() const noexcept { return globals_; }
    [[nodiscard]] const Program& program() const noexcept { return *globals_->program(); }
    [[nodiscard]] std::uint32_t behaviour() const noexcept { return behaviour_; }
    [[nodiscard]] const Value& self() const noexcept { return self_; }

    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
    [[nodiscard]] Value& value(std::size_t index) noexcept { return values_[index]; }
    [[nodiscard]] const Value& value(std::size_t index) const noexcept { return values_[index]; }
    // An instance variable by name (tests, diagnostics); nullptr if the behaviour declares none.
    [[nodiscard]] const Value* find(std::string_view name) const;

    // The entity is gone: no handler is dispatched to this instance any more. Idempotent.
    void expire() noexcept { expired_ = true; }
    [[nodiscard]] bool isExpired() const noexcept { return expired_; }

private:
    BehaviourInstance() = default;

    std::shared_ptr<GlobalState> globals_;
    std::uint32_t behaviour_ = 0;
    Value self_;
    std::vector<Value> values_;
    bool expired_ = false;
};

} // namespace schuimm::script
