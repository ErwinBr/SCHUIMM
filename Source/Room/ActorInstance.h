#pragma once

// A live actor: one actor resource placed in one room instance, with state of its own. Owned by
// its RoomInstance and destroyed with it; it never outlives or leaves its room (a revisit
// creates a new instance from the room's authored placement).
//
// Identity (fixed at creation, never changes): the room-local identifier from the placement, a
// runtime handle (script::ObjectRef, unique among all actors and room objects ever created, see
// ObjectHandles.h) and the shared, immutable actor resource. An actor can be neither copied nor
// moved: create() hands out a unique_ptr, so an identity is never duplicated, overwritten or
// moved out of its room, and an actor's address is fixed for its whole lifetime.
//
// State (all of it here, none in the resources): ground position (room coordinates), facing,
// visibility, scale, the logical animation and its playback. State changes are validated: a
// failed change leaves the actor exactly as it was.
//
//   * setAnimation(name, facing) changes both at once: the combination is resolved first and the
//     actor changes only if it resolves (so idle-south -> walk-north works even when idle has no
//     north clip and walk no south clip). The same clip (perhaps with different mirroring)
//     continues untouched, whatever its playback status; a different clip starts from its
//     beginning. Movement code should use this.
//   * setFacing(facing) is setAnimation(current animation, facing).
//   * playAnimation(name) is setAnimation(name, current facing), except that it also restarts the
//     resolved clip if that clip is stopped or finished ("play this"): a finished wave plays again.
//     A clip that is playing or paused continues untouched.
//   * Visibility only affects drawing: an invisible actor keeps animating.
//   * update(ticks) advances playback; nothing else does (drawing is read-only).

#include "Animation/AnimationPlayer.h"
#include "Room/RoomEvents.h"
#include "Core/Common/Error.h"
#include "Core/Common/Geometry.h"
#include "Room/ActorMovement.h"
#include "Room/ActorRenderState.h"
#include "Room/ActorResource.h"
#include "Room/Facing.h"
#include "Room/RoomResource.h"
#include "Script/Runtime/Value.h"

#include <cstdint>
#include <optional>
#include <vector>
#include <memory>
#include <string>
#include <string_view>

namespace schuimm::room {

// Positions are limited like room coordinates (2^20 pixels from the origin).
inline constexpr float maxActorCoordinate = 1048576.0f;

struct ActorConfiguration {
    Vec2 position;
    Facing facing = Facing::South;
    std::string animation;
    float baseScale = 1.0f;
    bool visible = true;
    std::optional<float> walkSpeed; // Defaults to the resource's movement speed.
};

// The configuration an actor starts with when nothing overrides it: the resource's defaults.
[[nodiscard]] ActorConfiguration defaultActorConfiguration(const ActorResource& resource, Vec2 position);
// A placement's configuration: its own values, the resource's defaults for the rest.
[[nodiscard]] ActorConfiguration placementConfiguration(const ActorPlacement& placement, const ActorResource& resource);

class ActorInstance {
public:
    // Validates `configuration` (position, scale, and that the animation resolves for the facing),
    // allocates a fresh handle and starts the animation.
    // `perspective` is the room's scale map (null outside a room or in a room without one); it
    // must outlive the actor (the RoomInstance holds the room resource).
    [[nodiscard]] static Result<std::unique_ptr<ActorInstance>> create(std::string id,
                                                                       std::shared_ptr<const ActorResource> resource,
                                                                       const ActorConfiguration& configuration,
                                                                       const ScaleMap* perspective = nullptr);

    ActorInstance(ActorInstance&&) = delete;
    ActorInstance(const ActorInstance&) = delete;
    ActorInstance& operator=(const ActorInstance&) = delete;
    ActorInstance& operator=(ActorInstance&&) = delete;

    // --- Identity ------------------------------------------------------------------------------
    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] script::ObjectRef handle() const noexcept { return handle_; }
    [[nodiscard]] const ActorResource& resource() const noexcept { return *resource_; }
    [[nodiscard]] const std::shared_ptr<const ActorResource>& sharedResource() const noexcept { return resource_; }

    // --- State ---------------------------------------------------------------------------------
    [[nodiscard]] Vec2 position() const noexcept { return position_; }
    // Places the actor directly (anywhere, walkable or not). InvalidArgument for non-finite
    // coordinates or ones beyond maxActorCoordinate. Cancels a walk: the actor stops there and
    // selects its idle animation.
    [[nodiscard]] Status setPosition(Vec2 position);

    [[nodiscard]] Facing facing() const noexcept { return facing_; }
    // NotFound if the current animation has no clip for `facing` (state unchanged).
    [[nodiscard]] Status setFacing(Facing facing);
    // Animation and facing together; NotFound (state unchanged) unless the pair resolves.
    [[nodiscard]] Status setAnimation(std::string_view name, Facing facing);

    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void setVisible(bool visible) noexcept { visible_ = visible; }

    // Scale: effective = base x perspective. The base scale is the actor's own; the perspective
    // scale comes from the room's scale map at the actor's ground y (1 without a map or when
    // perspective scaling is off), derived on every call so it never goes stale.
    [[nodiscard]] float baseScale() const noexcept { return baseScale_; }
    // InvalidArgument unless within [minActorScale, maxActorScale].
    [[nodiscard]] Status setBaseScale(float scale);
    [[nodiscard]] float perspectiveScale() const noexcept;
    [[nodiscard]] float effectiveScale() const noexcept;
    [[nodiscard]] bool perspectiveScaling() const noexcept { return perspectiveScaling_; }
    void setPerspectiveScaling(bool enabled) noexcept { perspectiveScaling_ = enabled; }

    // --- Movement ------------------------------------------------------------------------------
    // Walking is requested through RoomInstance::walkTo (it needs the room's navigation).
    [[nodiscard]] bool canWalk() const noexcept { return resource_->movement.has_value(); }
    [[nodiscard]] const MovementState& movement() const noexcept { return movement_.state(); }
    [[nodiscard]] bool isWalking() const noexcept { return movement_.isMoving(); }
    [[nodiscard]] MovementStatus movementStatus() const noexcept { return movement_.state().status; }
    // Stops a walk where the actor is (Cancelled) and selects the idle animation; no effect when
    // not walking.
    void stopWalking();
    // Room pixels per second at perspective scale 1 (0 for an actor that cannot walk).
    [[nodiscard]] float walkSpeed() const noexcept { return walkSpeed_; }
    // InvalidArgument unless within [minWalkSpeed, maxWalkSpeed]; InvalidState if the actor cannot
    // walk. Takes effect immediately, also for a walk in progress.
    [[nodiscard]] Status setWalkSpeed(float speed);

    // --- Animation -----------------------------------------------------------------------------
    [[nodiscard]] const std::string& animation() const noexcept { return animation_; }
    // Whether the resolved clip is drawn mirrored.
    [[nodiscard]] bool flipX() const noexcept { return flipX_; }
    // NotFound if the actor has no such animation or it has no clip for the current facing.
    [[nodiscard]] Status playAnimation(std::string_view name);
    // (setAnimation, setFacing and playAnimation cancel a walk when they succeed: an explicit
    // animation request wins, and the walk does not override it at its next waypoint.)
    void restartAnimation() noexcept;
    void pauseAnimation() noexcept { player_.pause(); }
    void resumeAnimation() noexcept { player_.resume(); }
    void stopAnimation() noexcept;
    [[nodiscard]] animation::PlaybackStatus playbackStatus() const noexcept { return player_.status(); }
    [[nodiscard]] const animation::AnimationPlayer& player() const noexcept { return player_; }

    // Advances playback by whole animation ticks.
    animation::AdvanceResult update(std::uint64_t ticks) noexcept;

    // The current frame as it is to be drawn (also when invisible: callers skip those).
    [[nodiscard]] ActorRenderState renderState() const;

private:
    friend class RoomInstance;
    enum class RestartFinished { No, Yes };

    // For RoomInstance::walkTo, after it validated the request.
    void beginWalk(MovementRequestId request, Vec2 requested, Vec2 resolved, std::vector<Vec2> path, Facing facing);
    void completeWalk(MovementRequestId request, Vec2 requested, Vec2 resolved);
    // For RoomInstance::update: moves along the path, turning and switching to idle on arrival.
    void advanceMovement(double seconds);

    Status changeAnimation(std::string_view name, Facing facing, RestartFinished restart);
    // Ends the walk in progress, if any, and reports how (RoomEvents.h).
    void endWalk(MovementOutcome outcome);
    // After a player change: reports the playback of `before` as replaced if it ended without
    // finishing.
    void reportPlaybackChange(std::uint64_t before) noexcept;

    ActorInstance(std::string id, script::ObjectRef handle, std::shared_ptr<const ActorResource> resource,
                  const ActorConfiguration& configuration, ResolvedActorClip clip, const ScaleMap* perspective);

    std::string id_;
    script::ObjectRef handle_;
    std::shared_ptr<const ActorResource> resource_;

    Vec2 position_;
    Facing facing_;
    bool visible_;
    float baseScale_;
    bool perspectiveScaling_;
    const ScaleMap* perspective_;
    std::string animation_;
    bool flipX_;
    animation::AnimationPlayer player_;
    float walkSpeed_;
    MovementController movement_;
    // The room's event sink (set by RoomInstance; null outside a room) and the last playback
    // generation whose end was reported.
    const RoomEventSink* events_ = nullptr;
    std::uint64_t reportedGeneration_ = 0;
};

} // namespace schuimm::room
