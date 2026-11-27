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
// instance, so every actor and animated object advances by the same ticks) and advances all of
// them once, visible or not. drawList() only reads: drawing never advances anything.
//
// Coroutines: the instance is the execution owner of the coroutines it starts (dispatch(),
// start()) and of those they spawn, and cancels them all when it is destroyed, so no coroutine
// of a destroyed room keeps running. An instance must not be destroyed while the ScriptHost
// updates (from a native or a script event callback): the running coroutine only stops when its
// native call returns, and that native may still use the room. This is checked in every build
// (violating it stops the program). Destroy rooms between frames.
// The ScriptHost must outlive its instances.

#include "Core/Common/Error.h"
#include "Core/Common/Geometry.h"
#include "Animation/AnimationClock.h"
#include "Animation/AnimationPlayer.h"
#include "Room/ActorInstance.h"
#include "Room/ActorRenderState.h"
#include "Room/RoomDrawList.h"
#include "Room/RoomEvents.h"
#include "Room/ObjectAnimation.h"
#include "Room/RoomLibrary.h"
#include "Room/RoomResource.h"
#include "Script/Runtime/Program.h"
#include "Script/Runtime/ScriptHost.h"
#include "Script/Runtime/ScriptState.h"
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
// State:
//   * position and visibility, through validated setters (a position must be finite and within
//     maxActorCoordinate, so nothing invalid reaches depth sorting or drawing);
//   * flags, free-form state for the game;
//   * for an animated object (its definition has an animation configuration): the current
//     logical animation and its own AnimationPlayer. Objects sharing animation resources never
//     share playback. The default animation starts when the object is created.
//
// Animation controls follow the AnimationPlayer's same-clip rule (AnimationPlayer.h):
// playAnimation of the animation that is playing or paused continues it untouched; of a stopped
// or finished one restarts it; of another one starts that from its beginning. On a static or
// invisible-hotspot object (no animation configuration) every animation control fails with
// InvalidState: an object never becomes animated at run time. Invisible animated objects keep
// playing, like actors.
//
// Assignment is deleted, so the identity cannot be overwritten through objects() either. Copying
// one out gives a snapshot that does not affect the room; so does "moving" one out, because there
// is no move constructor (an rvalue is copied), so the room's object keeps its state.
class RoomObject {
public:
    // A copy is a snapshot: it reports no events (RoomEvents.h).
    RoomObject(const RoomObject& other);
    RoomObject& operator=(const RoomObject&) = delete;
    RoomObject& operator=(RoomObject&&) = delete;

    // In the shared RoomResource.
    [[nodiscard]] const RoomObjectDefinition& definition() const noexcept { return *definition_; }
    // Passed to the object's handlers as self; never null, never reused.
    [[nodiscard]] script::ObjectRef handle() const noexcept { return handle_; }
    [[nodiscard]] const std::string& id() const noexcept { return definition_->id; }

    [[nodiscard]] Vec2 position() const noexcept { return position_; }
    // InvalidArgument (state unchanged) for non-finite coordinates or ones beyond
    // maxActorCoordinate.
    [[nodiscard]] Status setPosition(Vec2 position);
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void setVisible(bool visible) noexcept { visible_ = visible; }

    // Free-form state for the game (not used by the engine).
    std::map<std::string, bool, std::less<>> flags;

    // --- Animation (animated objects only) ------------------------------------------------------
    [[nodiscard]] bool isAnimated() const noexcept { return animations_ != nullptr; }
    // Its resolved animations; nullptr for a static object.
    [[nodiscard]] const ObjectAnimationSet* animations() const noexcept { return animations_.get(); }
    // The current logical animation; empty for a static object.
    [[nodiscard]] const std::string& animation() const noexcept { return animation_; }
    // NotFound for an unknown animation, InvalidState for a static object (state unchanged).
    [[nodiscard]] Status playAnimation(std::string_view name);
    [[nodiscard]] Status restartAnimation();
    [[nodiscard]] Status pauseAnimation();
    [[nodiscard]] Status resumeAnimation();
    [[nodiscard]] Status stopAnimation();
    // The object's playback (a player without clip for a static object).
    [[nodiscard]] const animation::AnimationPlayer& player() const noexcept { return player_; }
    // Advances playback by whole animation ticks (RoomInstance::update; nothing else does).
    animation::AdvanceResult update(std::uint64_t ticks) noexcept;

private:
    friend class RoomInstance;
    RoomObject(const RoomObjectDefinition& definition, script::ObjectRef handle,
               std::shared_ptr<const ObjectAnimationSet> animations);
    [[nodiscard]] Status requireAnimated(std::string_view operation) const;
    void reportPlaybackChange(std::uint64_t before) noexcept;

    const RoomObjectDefinition* definition_;
    script::ObjectRef handle_;
    Vec2 position_;
    bool visible_ = true;
    std::shared_ptr<const ObjectAnimationSet> animations_;
    std::string animation_;
    animation::AnimationPlayer player_;
    const RoomEventSink* events_ = nullptr; // The room's (set by RoomInstance); null for snapshots.
    std::uint64_t reportedGeneration_ = 0;
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

// An accepted walk request.
struct MovementRequest {
    MovementRequestId id = 0;
    Vec2 requested;
    Vec2 resolved;
    bool completed = false; // Already at the destination: no walking needed.
};

class RoomInstance {
public:
    // Creates the runtime objects and actors of `room`. Checks the script's object declarations
    // (validateRoomScript) and the actors (validateRoomActors) again, so an instance never has a
    // mismatched script. All or nothing: if any actor cannot be created, no instance is returned.
    //
    // Script state: `globals` are the global variables the room's script reads and writes (a
    // game session's); without them the instance creates its own (they then live as long as the
    // instance). Each room object with a declaration in the script gets its own behaviour
    // instance, so its instance variables start at their initial values with every instance of
    // the room. InvalidArgument if `globals` belong to another program than the room's script.
    [[nodiscard]] static Result<std::unique_ptr<RoomInstance>> create(LoadedRoom room, script::ScriptHost& host,
                                                                      std::shared_ptr<script::GlobalState> globals = {});

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

    // Advances the room by `deltaSeconds` of engine time: first every walking actor along its path
    // (position, facing, walk/idle animation), then every actor's animation playback.
    void update(double deltaSeconds);

    // Walks `actor` (one of this room's) towards `destination` (room coordinates) along a path
    // through the walkable areas, replacing any walk in progress from where the actor stands. With
    // NearestReachable a destination outside the walkable area, or in an area that cannot be
    // reached, becomes the nearest reachable point. Transactional: on failure nothing changes.
    //   * InvalidArgument: not this room's actor, or a non-finite destination;
    //   * InvalidState: the actor has no movement configuration, or the room no walkable areas;
    //   * NotFound: the actor stands outside the walkable area, the destination is unreachable
    //     (Strict), or the walk or idle animation has no clip for a facing the path needs.
    // A destination the actor is already at completes immediately (MovementRequest::completed).
    [[nodiscard]] Result<MovementRequest> walkTo(ActorInstance& actor, Vec2 destination,
                                                 navigation::DestinationPolicy policy =
                                                     navigation::DestinationPolicy::NearestReachable);
    // The room's navigation (null if it has no walkable areas).
    [[nodiscard]] const navigation::NavigationMap* navigation() const noexcept { return room_.navigation.get(); }
    // The background, props, visible objects and visible actors, in drawing order (RoomDrawList.h).
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

    // The global variables the room's script uses (null without a script).
    [[nodiscard]] const std::shared_ptr<script::GlobalState>& globals() const noexcept { return globals_; }
    // The script state of object `objectId` (its instance variables); null if the object has no
    // declaration in the script (or there is no such object).
    [[nodiscard]] std::shared_ptr<script::BehaviourInstance> objectScript(std::string_view objectId) const;

    // Coroutines of this instance (started by it, or spawned by those) that have not ended yet.
    [[nodiscard]] std::size_t activeCoroutineCount() const;

    // Receives the room's operation events (RoomEvents.h): walks and playbacks ending, and the
    // room's destruction. At most one listener; nullptr detaches. The listener must detach (or
    // outlive the room) before it is destroyed.
    void setEventListener(RoomEventListener* listener) noexcept { events_.listener = listener; }
    [[nodiscard]] RoomEventListener* eventListener() const noexcept { return events_.listener; }

private:
    RoomInstance(LoadedRoom room, script::ScriptHost& host, std::shared_ptr<script::GlobalState> globals,
                 std::vector<std::unique_ptr<ActorInstance>> actors);

    LoadedRoom room_;
    script::ScriptHost& host_;
    script::ExecutionOwner owner_; // Of every coroutine this instance starts (and they spawn).
    RoomEventSink events_;         // Shared by the actors and objects (stable address).
    std::vector<RoomObject> objects_;
    std::vector<std::unique_ptr<ActorInstance>> actors_;
    animation::AnimationClock clock_;
    std::shared_ptr<script::GlobalState> globals_;
    // Per object (objectScripts_[i] for objects_[i]): its behaviour instance, or null.
    std::vector<std::shared_ptr<script::BehaviourInstance>> objectScripts_;
};

} // namespace schuimm::room
