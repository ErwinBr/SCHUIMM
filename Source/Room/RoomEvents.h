#pragma once

// Notifications of a room instance's operations ending: a walk request, an animation playback,
// the room itself. They are emitted at the moment the state changes (not reconstructed later
// from the latest state), so a listener learns the outcome of *each* request and playback even
// if another one starts right after. The adventure layer records them on completion tokens
// (Adventure.md); nothing in the room layer knows about scripts.
//
// A listener must not change the room from inside a notification (record and return).

#include "Room/ActorMovement.h"
#include "Script/Runtime/Value.h"

#include <cstdint>

namespace schuimm::room {

enum class MovementOutcome : std::uint8_t {
    Completed, // The request reached its (resolved) destination, or needed no walking.
    Stopped,   // stopWalking, setPosition, or an explicit animation or facing change.
    Replaced,  // Another walk request of the actor took over.
};

enum class PlaybackOutcome : std::uint8_t {
    Finished, // A non-looping clip reached its end.
    Replaced, // Another playback started, or the playback was stopped.
};

[[nodiscard]] const char* toString(MovementOutcome outcome) noexcept;
[[nodiscard]] const char* toString(PlaybackOutcome outcome) noexcept;

class RoomEventListener {
public:
    virtual void movementEnded(script::ObjectRef actor, MovementRequestId request, MovementOutcome outcome) = 0;
    // `generation`: the player's playbackGeneration() of the playback that ended.
    virtual void playbackEnded(script::ObjectRef entity, bool isActor, std::uint64_t generation,
                               PlaybackOutcome outcome) = 0;
    // First thing the room's destructor does: the room and its entities still exist.
    virtual void roomDestroyed() = 0;

protected:
    ~RoomEventListener() = default;
};

// The dispatch point a room's actors and objects report to (owned by the RoomInstance; null
// listener: nobody listens). Entities outside a room have no sink and report nothing.
struct RoomEventSink {
    RoomEventListener* listener = nullptr;

    void movementEnded(script::ObjectRef actor, MovementRequestId request, MovementOutcome outcome) const
    {
        if (listener != nullptr) listener->movementEnded(actor, request, outcome);
    }
    void playbackEnded(script::ObjectRef entity, bool isActor, std::uint64_t generation, PlaybackOutcome outcome) const
    {
        if (listener != nullptr) listener->playbackEnded(entity, isActor, generation, outcome);
    }
};

} // namespace schuimm::room
