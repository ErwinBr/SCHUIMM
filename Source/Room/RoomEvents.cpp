#include "Room/RoomEvents.h"

namespace schuimm::room {

const char* toString(MovementOutcome outcome) noexcept
{
    switch (outcome) {
    case MovementOutcome::Completed: return "completed";
    case MovementOutcome::Stopped: return "stopped";
    case MovementOutcome::Replaced: return "replaced";
    }
    return "?";
}

const char* toString(PlaybackOutcome outcome) noexcept
{
    switch (outcome) {
    case PlaybackOutcome::Finished: return "finished";
    case PlaybackOutcome::Replaced: return "replaced";
    }
    return "?";
}

} // namespace schuimm::room
