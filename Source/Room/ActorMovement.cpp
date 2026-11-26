#include "Room/ActorMovement.h"

#include "Core/Common/GeometryPredicates.h"
#include "Room/Perspective.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

namespace schuimm::room {

namespace {

// |k L / s0| below this: the scale is treated as constant over the piece.
constexpr double linearThreshold = 1e-9;

struct Travel {
    double time = 0.0;     // Time used.
    double distance = 0.0; // New distance along the segment.
};

// Moves from `startDistance` along segment a -> b (length `length`) for at most `seconds`.
Travel travelAlong(Vec2 a, Vec2 b, double length, double startDistance, double seconds, double speed,
                   const ScaleMap* perspective)
{
    if (perspective == nullptr || perspective->points.empty()) {
        const double remaining = length - startDistance;
        const double reach = speed * seconds;
        if (reach >= remaining) return {remaining / speed, length};
        return {seconds, startDistance + reach};
    }
    const auto ay = static_cast<double>(a.y);
    const double dy = static_cast<double>(b.y) - ay;
    const auto yAt = [&](double l) { return static_cast<float>(ay + dy * l / length); };
    // Piece boundaries: where the segment crosses a control point's y, after startDistance.
    std::vector<double> cuts;
    if (dy != 0.0) {
        for (const ScalePoint& point : perspective->points) {
            const double l = (static_cast<double>(point.y) - ay) * length / dy;
            if (l > startDistance && l < length) cuts.push_back(l);
        }
        std::sort(cuts.begin(), cuts.end());
    }
    cuts.push_back(length);

    double used = 0.0;
    double at = startDistance;
    for (const double end : cuts) {
        const double pieceLength = end - at;
        if (pieceLength <= 0.0) continue;
        const auto s0 = static_cast<double>(evaluateScaleMap(*perspective, yAt(at)));
        const auto s1 = static_cast<double>(evaluateScaleMap(*perspective, yAt(end)));
        const double k = (s1 - s0) / pieceLength;
        const double left = seconds - used;
        const bool linear = std::fabs(k * pieceLength / s0) < linearThreshold;
        const double pieceTime = linear ? pieceLength / (speed * s0) : std::log1p(k * pieceLength / s0) / (speed * k);
        if (pieceTime <= left) {
            used += pieceTime;
            at = end;
            continue;
        }
        const double moved = linear ? speed * s0 * left : s0 * std::expm1(speed * k * left) / k;
        return {seconds, std::min(end, at + std::max(0.0, moved))};
    }
    return {used, length};
}

} // namespace

const char* movementStatusName(MovementStatus status) noexcept
{
    switch (status) {
    case MovementStatus::Idle: return "idle";
    case MovementStatus::Moving: return "moving";
    case MovementStatus::Completed: return "completed";
    case MovementStatus::Cancelled: return "cancelled";
    }
    return "unknown";
}

MovementRequestId allocateMovementRequest() noexcept
{
    static std::atomic<MovementRequestId> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

void MovementController::start(MovementRequestId request, Vec2 requested, Vec2 resolved, std::vector<Vec2> path)
{
    state_ = MovementState{MovementStatus::Moving, request, requested, resolved, std::move(path), 1, 0.0};
}

void MovementController::completeImmediately(MovementRequestId request, Vec2 requested, Vec2 resolved)
{
    state_ = MovementState{MovementStatus::Completed, request, requested, resolved, {resolved}, 0, 0.0};
}

void MovementController::cancel() noexcept
{
    if (state_.status == MovementStatus::Moving) state_.status = MovementStatus::Cancelled;
}

Vec2 MovementController::segmentDirection() const noexcept
{
    if (!isMoving()) return {};
    return state_.path[state_.nextWaypoint] - state_.path[state_.nextWaypoint - 1];
}

Vec2 MovementController::position() const noexcept
{
    switch (state_.status) {
    case MovementStatus::Idle: return {};
    case MovementStatus::Completed: return state_.resolved;
    case MovementStatus::Moving:
    case MovementStatus::Cancelled: break;
    }
    if (state_.nextWaypoint == 0 || state_.nextWaypoint >= state_.path.size()) return state_.resolved;
    const Vec2 a = state_.path[state_.nextWaypoint - 1];
    const Vec2 b = state_.path[state_.nextWaypoint];
    const double l = distance(a, b);
    return l > 0.0 ? lerp(a, b, state_.travelled / l) : b;
}

MovementController::Step MovementController::advance(double seconds, double speed, const ScaleMap* perspective)
{
    Step step;
    if (!isMoving() || !(seconds > 0.0) || !(speed > 0.0)) {
        step.position = position();
        return step;
    }
    double remaining = seconds;
    // Each iteration either finishes a segment or uses up the time: at most path.size() rounds.
    while (remaining > 0.0 && isMoving()) {
        const Vec2 a = state_.path[state_.nextWaypoint - 1];
        const Vec2 b = state_.path[state_.nextWaypoint];
        const double length = distance(a, b);
        bool segmentDone = length <= geometryTolerance;
        if (!segmentDone) {
            const Travel travel = travelAlong(a, b, length, state_.travelled, remaining, speed, perspective);
            remaining -= travel.time;
            state_.travelled = travel.distance;
            segmentDone = state_.travelled >= length;
            step.lastDirection = b - a;
            if (!segmentDone && travel.time <= 0.0) break; // No numerical progress possible.
        }
        if (!segmentDone) break;
        state_.travelled = 0.0;
        ++state_.nextWaypoint;
        if (state_.nextWaypoint >= state_.path.size()) {
            state_.status = MovementStatus::Completed;
            step.arrived = true;
        } else {
            step.segmentChanged = true;
        }
    }
    step.position = position();
    return step;
}

} // namespace schuimm::room
