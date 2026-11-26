#include "Room/ActorInstance.h"

#include "Core/Common/StringBuilder.h"
#include "Core/Common/Assert.h"
#include "Room/ObjectHandles.h"
#include "Room/Perspective.h"

#include <cmath>
#include <utility>

namespace schuimm::room {

namespace {

Status checkPosition(Vec2 position)
{
    const auto valid = [](float c) { return std::isfinite(c) && std::fabs(c) <= maxActorCoordinate; };
    if (!valid(position.x) || !valid(position.y)) {
        return makeError(ErrorCode::InvalidArgument,
                         concat("actor position (", position.x, ", ", position.y, ") must be finite and within ",
                                static_cast<double>(maxActorCoordinate), " pixels of the origin"));
    }
    return {};
}

Status checkRange(float value, double minimum, double maximum, std::string_view what)
{
    if (!(static_cast<double>(value) >= minimum) || !(static_cast<double>(value) <= maximum)) {
        return makeError(ErrorCode::InvalidArgument,
                         concat("actor ", what, " ", value, " must be from ", minimum, " to ", maximum));
    }
    return {};
}

} // namespace

ActorConfiguration defaultActorConfiguration(const ActorResource& resource, Vec2 position)
{
    return ActorConfiguration{position,        resource.defaultFacing, resource.defaultAnimation, resource.defaultScale,
                              true,            std::nullopt};
}

ActorConfiguration placementConfiguration(const ActorPlacement& placement, const ActorResource& resource)
{
    ActorConfiguration configuration = defaultActorConfiguration(resource, placement.position);
    if (placement.facing) configuration.facing = *placement.facing;
    if (placement.animation) configuration.animation = *placement.animation;
    if (placement.scale) configuration.baseScale = *placement.scale;
    if (placement.visible) configuration.visible = *placement.visible;
    if (placement.walkSpeed) configuration.walkSpeed = *placement.walkSpeed;
    return configuration;
}

Result<std::unique_ptr<ActorInstance>> ActorInstance::create(std::string id, std::shared_ptr<const ActorResource> resource,
                                                            const ActorConfiguration& configuration,
                                                            const ScaleMap* perspective)
{
    if (!resource) return makeError(ErrorCode::InvalidArgument, concat("actor '", id, "': no actor resource"));
    const auto prefix = [&id](const Error& error) {
        return makeError(error.code, concat("actor '", id, "': ", error.message));
    };
    if (auto status = checkPosition(configuration.position); !status) return prefix(status.error());
    if (auto status = checkRange(configuration.baseScale, minActorScale, maxActorScale, "scale"); !status) {
        return prefix(status.error());
    }
    if (configuration.walkSpeed) {
        if (!resource->movement) {
            return makeError(ErrorCode::InvalidState,
                             concat("actor '", id, "': a walk speed is set, but actor '", resource->id, "' cannot walk"));
        }
        if (auto status = checkRange(*configuration.walkSpeed, minWalkSpeed, maxWalkSpeed, "walk speed"); !status) {
            return prefix(status.error());
        }
    }
    auto clip = resource->resolve(configuration.animation, configuration.facing);
    if (!clip) return prefix(clip.error());
    return std::unique_ptr<ActorInstance>(new ActorInstance(std::move(id), detail::allocateObjectHandle(),
                                                            std::move(resource), configuration, std::move(*clip),
                                                            perspective));
}

ActorInstance::ActorInstance(std::string id, script::ObjectRef handle, std::shared_ptr<const ActorResource> resource,
                             const ActorConfiguration& configuration, ResolvedActorClip clip,
                             const ScaleMap* perspective)
    : id_(std::move(id))
    , handle_(handle)
    , resource_(std::move(resource))
    , position_(configuration.position)
    , facing_(configuration.facing)
    , visible_(configuration.visible)
    , baseScale_(configuration.baseScale)
    , perspectiveScaling_(resource_->perspectiveScale)
    , perspective_(perspective)
    , animation_(configuration.animation)
    , flipX_(clip.flipX)
    , walkSpeed_(configuration.walkSpeed.value_or(resource_->movement ? resource_->movement->speed : 0.0f))
{
    player_.play(std::move(clip.clip));
}

Status ActorInstance::setPosition(Vec2 position)
{
    if (auto status = checkPosition(position); !status) return status;
    stopWalking(); // Stale waypoints must not pull the actor back.
    position_ = position;
    return {};
}

Status ActorInstance::setBaseScale(float scale)
{
    if (auto status = checkRange(scale, minActorScale, maxActorScale, "scale"); !status) return status;
    baseScale_ = scale;
    return {};
}

float ActorInstance::perspectiveScale() const noexcept
{
    return perspectiveScaling_ ? perspectiveScaleAt(perspective_, position_.y) : 1.0f;
}

float ActorInstance::effectiveScale() const noexcept
{
    // Both factors are within [0.001, 1000] (validated), so the product is in [1e-6, 1e6].
    return static_cast<float>(static_cast<double>(baseScale_) * static_cast<double>(perspectiveScale()));
}

Status ActorInstance::setWalkSpeed(float speed)
{
    if (!canWalk()) {
        return makeError(ErrorCode::InvalidState, concat("actor '", id_, "' (resource '", resource_->id, "') cannot walk"));
    }
    if (auto status = checkRange(speed, minWalkSpeed, maxWalkSpeed, "walk speed"); !status) return status;
    walkSpeed_ = speed;
    return {};
}

void ActorInstance::endWalk(MovementOutcome outcome)
{
    if (!movement_.isMoving()) return;
    const MovementRequestId request = movement_.state().request;
    movement_.cancel();
    if (events_ != nullptr) events_->movementEnded(handle_, request, outcome);
}

void ActorInstance::reportPlaybackChange(std::uint64_t before) noexcept
{
    if (player_.playbackGeneration() == before || reportedGeneration_ == before) return;
    reportedGeneration_ = before;
    if (events_ != nullptr) events_->playbackEnded(handle_, true, before, PlaybackOutcome::Replaced);
}

void ActorInstance::restartAnimation() noexcept
{
    const std::uint64_t before = player_.playbackGeneration();
    player_.restart();
    reportPlaybackChange(before);
}

void ActorInstance::stopAnimation() noexcept
{
    const std::uint64_t before = player_.playbackGeneration();
    player_.stop();
    reportPlaybackChange(before);
}

animation::AdvanceResult ActorInstance::update(std::uint64_t ticks) noexcept
{
    const animation::AdvanceResult result = player_.advance(ticks);
    if (result.finished) {
        reportedGeneration_ = player_.playbackGeneration();
        if (events_ != nullptr) {
            events_->playbackEnded(handle_, true, player_.playbackGeneration(), PlaybackOutcome::Finished);
        }
    }
    return result;
}

void ActorInstance::stopWalking()
{
    if (!movement_.isMoving()) return;
    endWalk(MovementOutcome::Stopped);
    // The idle animation was checked for every facing of the walk when it was accepted.
    const Status idle = changeAnimation(resource_->movement->idleAnimation, facing_, RestartFinished::No);
    SCHUIMM_ASSERT(idle.has_value(), "the idle animation resolves for every facing of an accepted walk");
}

void ActorInstance::beginWalk(MovementRequestId request, Vec2 requested, Vec2 resolved, std::vector<Vec2> path,
                              Facing facing)
{
    endWalk(MovementOutcome::Replaced); // The walk in progress, if any.
    movement_.start(request, requested, resolved, std::move(path));
    // Entering locomotion: the walk clip must play, even if it was selected but stopped, finished
    // or paused before (it continues if it was already playing). Turns during the walk instead keep
    // an unchanged clip exactly as it is.
    const Status walk = changeAnimation(resource_->movement->walkAnimation, facing, RestartFinished::Yes);
    SCHUIMM_ASSERT(walk.has_value(), "the walk animation was checked for every facing of the walk");
    player_.resume();
}

void ActorInstance::completeWalk(MovementRequestId request, Vec2 requested, Vec2 resolved)
{
    const bool wasWalking = movement_.isMoving();
    endWalk(MovementOutcome::Replaced);
    movement_.completeImmediately(request, requested, resolved);
    if (events_ != nullptr) events_->movementEnded(handle_, request, MovementOutcome::Completed);
    if (wasWalking) {
        const Status idle = changeAnimation(resource_->movement->idleAnimation, facing_, RestartFinished::No);
        SCHUIMM_ASSERT(idle.has_value(), "the idle animation was checked before completing the walk");
    }
}

void ActorInstance::advanceMovement(double seconds)
{
    if (!movement_.isMoving()) return;
    const ActorMovementConfig& config = *resource_->movement;
    const MovementController::Step step =
        movement_.advance(seconds, static_cast<double>(walkSpeed_), config.perspectiveSpeed ? perspective_ : nullptr);
    position_ = step.position;
    if (step.arrived) {
        if (events_ != nullptr) events_->movementEnded(handle_, movement_.state().request, MovementOutcome::Completed);
        // Face the way the last segment was walked, even if it was entered and finished within this
        // update (so the final facing does not depend on the update rate).
        Facing facing = facing_;
        if (step.lastDirection) facing = facingFromDirection(*step.lastDirection).value_or(facing_);
        const Status idle = changeAnimation(config.idleAnimation, facing, RestartFinished::No);
        SCHUIMM_ASSERT(idle.has_value(), "the idle animation resolves for every facing of an accepted walk");
    } else if (step.segmentChanged) {
        if (const auto facing = facingFromDirection(movement_.segmentDirection())) {
            const Status walk = changeAnimation(config.walkAnimation, *facing, RestartFinished::No);
            SCHUIMM_ASSERT(walk.has_value(), "the walk animation was checked for every facing of the walk");
        }
    }
}

Status ActorInstance::changeAnimation(std::string_view name, Facing facing, RestartFinished restart)
{
    // Resolve the whole combination first: nothing changes unless it resolves.
    auto clip = resource_->resolve(name, facing);
    if (!clip) return std::unexpected(clip.error());
    // `name` may refer to animation_ itself (setFacing): copy before assigning.
    std::string animation(name);
    animation_ = std::move(animation);
    facing_ = facing;
    flipX_ = clip->flipX;
    if (!(clip->clip == player_.clip()) || restart == RestartFinished::Yes) {
        // A different clip starts from its beginning; for the same clip, play() restarts it only
        // if it is stopped or finished and leaves it untouched if it is playing or paused.
        const std::uint64_t before = player_.playbackGeneration();
        player_.play(std::move(clip->clip));
        reportPlaybackChange(before);
    }
    return {};
}

// The public animation requests cancel a walk once they succeed: an explicit request owns the
// animation from then on (the walk would otherwise change it again at its next waypoint).
Status ActorInstance::setAnimation(std::string_view name, Facing facing)
{
    auto status = changeAnimation(name, facing, RestartFinished::No);
    if (status) endWalk(MovementOutcome::Stopped);
    return status;
}

Status ActorInstance::setFacing(Facing facing)
{
    auto status = changeAnimation(animation_, facing, RestartFinished::No);
    if (status) endWalk(MovementOutcome::Stopped);
    return status;
}

Status ActorInstance::playAnimation(std::string_view name)
{
    auto status = changeAnimation(name, facing_, RestartFinished::Yes);
    if (status) endWalk(MovementOutcome::Stopped);
    return status;
}

ActorRenderState ActorInstance::renderState() const
{
    return makeActorRenderState(player_.currentFrame(), position_, effectiveScale(), flipX_);
}

} // namespace schuimm::room
