#pragma once

// The adventure runtime context: what the adventure natives (adventureInterface()) act on. It
// connects them to the active room instance, the camera and the script host, and owns the
// completion tokens of the gameplay operations scripts wait for. It is headless (no graphics
// device: Camera2D is plain state) and has no loop of its own.
//
//   * At most one active room (setActiveRoom). Natives look entities up in it by their handle;
//     there is no registry of actors or objects beyond the room's own. Destroying the active room
//     detaches it automatically (its pending operations are cancelled "destroyed"); detaching
//     explicitly first is still the clearer order.
//   * bind(registry) registers the implementations. They capture this context: it must outlive
//     every program and coroutine that calls them (destroy or stopAll() the ScriptHost first).
//   * Operations: walk_to suspends its caller on a token for its movement request;
//     play_animation returns a token for its playback. Three separate steps:
//       record   the context listens to the active room's events (RoomEvents.h) and records
//                each request's or playback's outcome on its token the moment it ends, also
//                when another one starts right after;
//       deliver  publish(), once per frame after the room update, notifies the waiters;
//       execute  the resumed coroutines run in the ScriptHost update that follows:
//
//         game onUpdate:  input -> room.update(dt) -> adventure.publish()
//         Application:    ScriptHost::update(dt)
//
//     So a coroutine never resumes from inside movement or animation code. An operation a
//     script changes (stop_walking, a new walk_to, set_position, stop_animation) is recorded at
//     once (a following wait() sees it); waiters already suspended hear of it at publish().
//
// Outcomes, as the room reports them for each request and playback (Adventure.md has the full
// interplay):
//   walk:      completed when the request arrived; cancelled "stopped" (stop_walking,
//              set_position, an explicit animation or direction change), "replaced" (another
//              request took over) or "destroyed" (the room went away). A coroutine cancelled
//              while waiting in walk_to stops its own walk, never a newer one.
//   playback:  completed when it finished; cancelled "replaced" when it ended unfinished
//              (another playback, stop_animation, a walk's automatic clip changes) or
//              "destroyed". Waiting for a looping playback is a script error. Cancelling a
//              waiter never stops the animation (others may watch it).

#include "Core/Common/Error.h"
#include "Graphics/Common/Camera2D.h"
#include "Room/RoomInstance.h"
#include "Script/Runtime/Completion.h"
#include "Script/Runtime/NativeRegistry.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace schuimm::adventure {

// The script enum adventure.Direction and the engine's Facing, mapped by an explicit table (not
// by ordinal).
[[nodiscard]] room::Facing facingFromDirection(std::uint32_t directionOrdinal) noexcept;
[[nodiscard]] std::uint32_t directionFromFacing(room::Facing facing) noexcept;

class AdventureContext final : private room::RoomEventListener {
public:
    AdventureContext() = default;
    ~AdventureContext(); // Detaches from the active room and cancels pending operations.
    AdventureContext(const AdventureContext&) = delete;
    AdventureContext& operator=(const AdventureContext&) = delete;

    // Binds every function of adventureInterface() to this context. Fails if the registry already
    // has one of the names (e.g. bound twice).
    [[nodiscard]] Status bind(script::NativeRegistry& registry);

    // The room the natives act on (nullptr: none; lookups give null references and other
    // natives fail). Changing it cancels every pending operation of the previous room
    // ("destroyed") and listens to the new room's events.
    void setActiveRoom(room::RoomInstance* room);
    [[nodiscard]] room::RoomInstance* activeRoom() const noexcept { return room_; }
    // The camera of the camera natives (nullptr: they fail). Must outlive its use here.
    void setCamera(Camera2D* camera) noexcept { camera_ = camera; }
    [[nodiscard]] Camera2D* camera() const noexcept { return camera_; }

    // Delivers the outcomes recorded since the last publish to their waiters (see above).
    // Playback records nobody can observe any more are dropped, so there is no growing history.
    void publish();
    // Operations that have not ended, and ended ones not delivered yet (tests, diagnostics).
    [[nodiscard]] std::size_t pendingWalks() const noexcept { return walks_.size(); }
    [[nodiscard]] std::size_t pendingPlaybacks() const noexcept { return playbacks_.size(); }
    [[nodiscard]] std::size_t undelivered() const noexcept { return recorded_.size(); }

    // An entity reference resolved against the active room: exactly one of actor/object is set.
    struct Entity {
        room::ActorInstance* actor = nullptr;
        room::RoomObject* object = nullptr;
    };
    // Null, expired (not in the active room) or no active room: an error message naming the
    // reference.
    [[nodiscard]] Result<Entity> resolve(const script::Value& reference) const;

private:
    struct Walk {
        script::ObjectRef actor;
        room::MovementRequestId request = 0;
        script::CompletionRef token;
    };
    struct Playback {
        script::ObjectRef entity;
        bool actor = false;
        std::uint64_t generation = 0;
        script::CompletionRef token;
    };

    script::NativeResult walkTo(const script::Value& actor, const script::Value& destination);
    script::NativeResult playAnimation(const script::Value& entity, std::string_view name);
    // The player of an entity (actor or animated object).
    [[nodiscard]] static const animation::AnimationPlayer& playerOf(const Entity& entity) noexcept;
    void cancelAll(const std::string& reason);
    void detach();

    // RoomEventListener: record outcomes as they happen.
    void movementEnded(script::ObjectRef actor, room::MovementRequestId request, room::MovementOutcome outcome) override;
    void playbackEnded(script::ObjectRef entity, bool isActor, std::uint64_t generation,
                       room::PlaybackOutcome outcome) override;
    void roomDestroyed() override;

    room::RoomInstance* room_ = nullptr;
    Camera2D* camera_ = nullptr;
    std::vector<Walk> walks_;
    std::vector<Playback> playbacks_;
    std::vector<script::CompletionRef> recorded_; // Ended, waiting for publish() to deliver.
};

} // namespace schuimm::adventure
