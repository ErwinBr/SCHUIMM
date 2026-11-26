#pragma once

// Following a path: the per-actor movement state and its time-based advance. Part of an
// ActorInstance (which applies the position and chooses facing and animation); paths come from
// the room's NavigationMap. Pure state and arithmetic: no navigation queries, no animation.
//
// Timing: advance(seconds) moves along the path at `speed` room pixels per second, times the
// room's perspective scale at the actor's ground y when a scale map is given. Because the scale
// map is piecewise linear in y, the scale is linear in the distance travelled within each piece
// of a segment between two control-point y values, s(l) = s0 + k l, and the motion is integrated
// exactly there:
//     time to travel L:          ln(1 + k L / s0) / (v k)      (L / (v s0) when k ~ 0)
//     distance travelled in T:   s0 (exp(v k T) - 1) / k       (v s0 T when k ~ 0)
// so the result does not depend on how the time is split into updates (up to rounding). Time left
// over at a waypoint carries on to the next segment; the final waypoint is reached exactly. The
// position is recomputed from the segment start and the distance travelled (double), so errors do
// not accumulate.

#include "Core/Common/Geometry.h"
#include "Room/RoomResource.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace schuimm::room {

// Identifies one walk request (non-zero, unique in the process), so that code waiting for a walk
// can tell its own completion from a replacement or cancellation.
using MovementRequestId = std::uint64_t;

enum class MovementStatus {
    Idle,      // Never asked to walk.
    Moving,
    Completed, // The last request reached its (resolved) destination.
    Cancelled, // The last request was stopped, replaced by an explicit change, or the actor was moved.
};

[[nodiscard]] const char* movementStatusName(MovementStatus status) noexcept;

struct MovementState {
    MovementStatus status = MovementStatus::Idle;
    MovementRequestId request = 0; // Of the last accepted request; 0 before the first.
    Vec2 requested;                // Where the request asked to go.
    Vec2 resolved;                 // Where it goes (DestinationPolicy applied).
    std::vector<Vec2> path;        // Waypoints from the start position to `resolved`.
    std::size_t nextWaypoint = 0;  // While moving: the waypoint being walked to (path[nextWaypoint]).
    double travelled = 0.0;        // While moving: distance from path[nextWaypoint - 1].
};

class MovementController {
public:
    [[nodiscard]] const MovementState& state() const noexcept { return state_; }
    [[nodiscard]] bool isMoving() const noexcept { return state_.status == MovementStatus::Moving; }

    // Starts following `path` (at least two points; consecutive points may coincide).
    void start(MovementRequestId request, Vec2 requested, Vec2 resolved, std::vector<Vec2> path);
    // A request that needed no walking.
    void completeImmediately(MovementRequestId request, Vec2 requested, Vec2 resolved);
    // Moving -> Cancelled, keeping the segment and the distance travelled (position() stays where
    // the walk stopped); no effect otherwise.
    void cancel() noexcept;

    // The direction of the segment being walked ({0, 0} when not moving).
    [[nodiscard]] Vec2 segmentDirection() const noexcept;

    struct Step {
        Vec2 position;
        bool segmentChanged = false; // A waypoint was passed.
        bool arrived = false;        // The destination was reached (status is now Completed).
        // The direction of the last (non-degenerate) segment walked during this advance, the
        // final one included: on arrival it gives the facing to keep, whatever the update rate.
        std::optional<Vec2> lastDirection;
    };
    // Advances a moving controller by `seconds` (no effect when not moving or for seconds <= 0).
    // `speed` > 0 in room pixels per second; `perspective` (may be null) scales it.
    Step advance(double seconds, double speed, const ScaleMap* perspective);

    // Where the walk is: on its current segment while moving or after a cancel, the resolved
    // destination once completed, {0, 0} before any request.
    [[nodiscard]] Vec2 position() const noexcept;

private:
    MovementState state_;
};

[[nodiscard]] MovementRequestId allocateMovementRequest() noexcept;

} // namespace schuimm::room
