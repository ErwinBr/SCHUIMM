#pragma once

// A live room: the runtime objects created from a RoomResource's object definitions, the actors
// created from its actor placements, their handles and mutable state, and the association with
// the room's compiled script.
//
// Immutable data (RoomResource, Program) is shared between all instances of a room; everything
// mutable lives here, so instances are independent and the resource never changes.
//
// Object handles: every room object and every actor gets a script::ObjectRef from one
// process-wide counter (ObjectHandles.h), so handles are unique across objects, actors and all
// instances and never reused (the ObjectRef contract in Script/Runtime/Value.h). After an
// instance is destroyed its handles are stale: they never refer to another object or actor, not
// even to the "same" actor of a new instance of the room.
//
// Actors: created from the room's placements every time the room is instantiated (there is no
// memory of earlier instances: a revisit starts from the authored placements). They live and die
// with the instance. Room objects and actors are separate kinds: object() never finds an actor,
// actor() never an object, and the room script's object declarations refer to objects only.
//
// Lifetime of views: the object and actor collections are fixed when the instance is created, so
// spans, views, pointers and references from objects(), actors(), object() and actor() stay valid
// until the instance is destroyed. Each actor is allocated on its own and can be neither copied
// nor moved, so its address is stable.
//
// Updating: update(deltaSeconds) converts engine time to animation ticks (one AnimationClock per
// instance, so every actor advances by the same ticks) and advances all actors, visible or not.
// drawList() only reads: drawing never advances anything.
//
// Coroutines: the instance remembers the coroutines it started (dispatch(), start()) and cancels
// those still alive when it is destroyed, so no handler of a destroyed room keeps running. An
// instance must therefore not be destroyed while the ScriptHost updates (from a native or a
// script event callback): the running coroutine cannot be cancelled. This is checked in every
// build (violating it stops the program). Destroy rooms between frames.
// The ScriptHost must outlive its instances.

#include "Core/Common/Error.h"
#include "Core/Common/Geometry.h"
#include "Animation/AnimationClock.h"
#include "Room/ActorInstance.h"
#include "Room/ActorRenderState.h"
#include "Room/RoomLibrary.h"
#include "Room/RoomResource.h"
#include "Script/Runtime/Program.h"
#include "Script/Runtime/ScriptHost.h"
#include "Script/Runtime/Value.h"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace schuimm::room {

// One interactive object of a room instance. Its identity (definition and handle) is fixed when
// the instance creates it; its state is initialised from the definition and is the instance's
// own: changing it never changes the definition.
//
// Assignment is deleted, so the identity cannot be overwritten through objects() either. Copying
// one out gives a snapshot that does not affect the room; so does "moving" one out, because there
// is no move constructor (an rvalue is copied), so the room's object keeps its state.
class RoomObject {
public:
    RoomObject(const RoomObject&) = default;
    RoomObject& operator=(const RoomObject&) = delete;
    RoomObject& operator=(RoomObject&&) = delete;

    // In the shared RoomResource.
    [[nodiscard]] const RoomObjectDefinition& definition() const noexcept { return *definition_; }
    // Passed to the object's handlers as self; never null, never reused.
    [[nodiscard]] script::ObjectRef handle() const noexcept { return handle_; }
    [[nodiscard]] const std::string& id() const noexcept { return definition_->id; }

    // Mutable runtime state.
    Vec2 position;
    bool visible = true;
    std::map<std::string, bool, std::less<>> flags;

private:
    friend class RoomInstance;
    RoomObject(const RoomObjectDefinition& definition, script::ObjectRef handle)
        : position(definition.position)
        , definition_(&definition)
        , handle_(handle)
    {
    }

    const RoomObjectDefinition* definition_;
    script::ObjectRef handle_;
};

// A view of a room's actors: ActorInstance& (or const ActorInstance&) in placement order, with
// operator[], size(), empty() and range-for. Like std::span it is a pointer and a size (trivially
// copyable), so references taken from a temporary view, as in `auto& a = room.actors()[0];`,
// refer to the room's actor and stay valid as long as the room (GCC's -Wdangling-reference
// recognises such span-like views; it flags std::views::transform).
template <typename T>
class ActorView {
public:
    using Pointer = std::unique_ptr<std::remove_const_t<T>>;

    class Iterator {
    public:
        explicit Iterator(const Pointer* at) noexcept
            : at_(at)
        {
        }
        T& operator*() const noexcept { return **at_; }
        T* operator->() const noexcept { return at_->get(); }
        Iterator& operator++() noexcept
        {
            ++at_;
            return *this;
        }
        friend bool operator==(Iterator, Iterator) noexcept = default;

    private:
        const Pointer* at_;
    };

    ActorView(const Pointer* data, std::size_t size) noexcept
        : data_(data)
        , size_(size)
    {
    }

    [[nodiscard]] T& operator[](std::size_t index) const noexcept { return *data_[index]; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] Iterator begin() const noexcept { return Iterator(data_); }
    [[nodiscard]] Iterator end() const noexcept { return Iterator(data_ + size_); }

private:
    const Pointer* data_;
    std::size_t size_;
};

class RoomInstance {
public:
    // Creates the runtime objects and actors of `room`. Checks the script's object declarations
    // (validateRoomScript) and the actors (validateRoomActors) again, so an instance never has a
    // mismatched script. All or nothing: if any actor cannot be created, no instance is returned.
    [[nodiscard]] static Result<std::unique_ptr<RoomInstance>> create(LoadedRoom room, script::ScriptHost& host);

    ~RoomInstance();
    RoomInstance(const RoomInstance&) = delete;
    RoomInstance& operator=(const RoomInstance&) = delete;

    [[nodiscard]] const RoomResource& resource() const noexcept { return *room_.resource; }
    [[nodiscard]] const std::shared_ptr<const RoomResource>& sharedResource() const noexcept { return room_.resource; }
    // nullptr if the room has no script.
    [[nodiscard]] const std::shared_ptr<const script::Program>& script() const noexcept { return room_.script; }

    // In the order of the resource's objects.
    [[nodiscard]] std::span<RoomObject> objects() noexcept { return objects_; }
    [[nodiscard]] std::span<const RoomObject> objects() const noexcept { return objects_; }
    // nullptr if there is no such object (in this instance).
    [[nodiscard]] RoomObject* object(std::string_view id) noexcept;
    [[nodiscard]] const RoomObject* object(std::string_view id) const noexcept;
    [[nodiscard]] RoomObject* object(script::ObjectRef handle) noexcept;
    [[nodiscard]] const RoomObject* object(script::ObjectRef handle) const noexcept;

    // In the order of the resource's actor placements (ActorView: actors()[i], size(), range-for).
    [[nodiscard]] ActorView<ActorInstance> actors() noexcept { return {actors_.data(), actors_.size()}; }
    [[nodiscard]] ActorView<const ActorInstance> actors() const noexcept { return {actors_.data(), actors_.size()}; }
    // nullptr if there is no such actor (in this instance).
    [[nodiscard]] ActorInstance* actor(std::string_view id) noexcept;
    [[nodiscard]] const ActorInstance* actor(std::string_view id) const noexcept;
    [[nodiscard]] ActorInstance* actor(script::ObjectRef handle) noexcept;
    [[nodiscard]] const ActorInstance* actor(script::ObjectRef handle) const noexcept;

    // Advances every actor's animation by `deltaSeconds` of engine time.
    void update(double deltaSeconds);
    // The visible actors' current frames, back to front (RoomDrawList).
    [[nodiscard]] RoomDrawList drawList() const;

    // The integration point for event dispatch: invokes the handler `event` of the object with
    // identifier `objectId`, with that object's handle as self (ScriptHost::dispatch).
    //   * NotFound: the room has no object `objectId`;
    //   * {handled = false}: the room has no script, or the script has no such handler;
    //   * otherwise the started coroutine.
    [[nodiscard]] Result<script::DispatchResult> dispatch(std::string_view objectId, std::string_view event,
                                                          std::vector<script::Value> arguments = {});
    // Starts a room-level function of the script (e.g. "enter"). NotFound without a script.
    [[nodiscard]] Result<script::CoroutineHandle> start(std::string_view function,
                                                        std::vector<script::Value> arguments = {});

    // Coroutines started by this instance that have not ended yet.
    [[nodiscard]] std::size_t activeCoroutineCount() const;

private:
    RoomInstance(LoadedRoom room, script::ScriptHost& host, std::vector<std::unique_ptr<ActorInstance>> actors);
    void track(script::CoroutineHandle handle);
    void prune();

    LoadedRoom room_;
    script::ScriptHost& host_;
    std::vector<RoomObject> objects_;
    std::vector<std::unique_ptr<ActorInstance>> actors_;
    animation::AnimationClock clock_;
    std::vector<script::CoroutineHandle> coroutines_;
};

} // namespace schuimm::room
