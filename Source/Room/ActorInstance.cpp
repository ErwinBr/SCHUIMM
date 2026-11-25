#include "Room/ActorInstance.h"

#include "Core/Common/StringBuilder.h"
#include "Room/ObjectHandles.h"

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

Status checkScale(float scale)
{
    if (!std::isfinite(scale) || !(scale > 0.0f) || static_cast<double>(scale) > maxActorScale) {
        return makeError(ErrorCode::InvalidArgument,
                         concat("actor scale ", scale, " must be positive, finite and at most ", maxActorScale));
    }
    return {};
}

} // namespace

ActorConfiguration defaultActorConfiguration(const ActorResource& resource, Vec2 position)
{
    return ActorConfiguration{position, resource.defaultFacing, resource.defaultAnimation, resource.defaultScale, true};
}

ActorConfiguration placementConfiguration(const ActorPlacement& placement, const ActorResource& resource)
{
    ActorConfiguration configuration = defaultActorConfiguration(resource, placement.position);
    if (placement.facing) configuration.facing = *placement.facing;
    if (placement.animation) configuration.animation = *placement.animation;
    if (placement.scale) configuration.scale = *placement.scale;
    if (placement.visible) configuration.visible = *placement.visible;
    return configuration;
}

Result<std::unique_ptr<ActorInstance>> ActorInstance::create(std::string id, std::shared_ptr<const ActorResource> resource,
                                            const ActorConfiguration& configuration)
{
    if (!resource) return makeError(ErrorCode::InvalidArgument, concat("actor '", id, "': no actor resource"));
    const auto prefix = [&id](const Error& error) {
        return makeError(error.code, concat("actor '", id, "': ", error.message));
    };
    if (auto status = checkPosition(configuration.position); !status) return prefix(status.error());
    if (auto status = checkScale(configuration.scale); !status) return prefix(status.error());
    auto clip = resource->resolve(configuration.animation, configuration.facing);
    if (!clip) return prefix(clip.error());
    return std::unique_ptr<ActorInstance>(new ActorInstance(std::move(id), detail::allocateObjectHandle(),
                                                            std::move(resource), configuration, std::move(*clip)));
}

ActorInstance::ActorInstance(std::string id, script::ObjectRef handle, std::shared_ptr<const ActorResource> resource,
                             const ActorConfiguration& configuration, ResolvedActorClip clip)
    : id_(std::move(id))
    , handle_(handle)
    , resource_(std::move(resource))
    , position_(configuration.position)
    , facing_(configuration.facing)
    , visible_(configuration.visible)
    , scale_(configuration.scale)
    , animation_(configuration.animation)
    , flipX_(clip.flipX)
{
    player_.play(std::move(clip.clip));
}

Status ActorInstance::setPosition(Vec2 position)
{
    if (auto status = checkPosition(position); !status) return status;
    position_ = position;
    return {};
}

Status ActorInstance::setScale(float scale)
{
    if (auto status = checkScale(scale); !status) return status;
    scale_ = scale;
    return {};
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
        player_.play(std::move(clip->clip));
    }
    return {};
}

Status ActorInstance::setAnimation(std::string_view name, Facing facing)
{
    return changeAnimation(name, facing, RestartFinished::No);
}

Status ActorInstance::setFacing(Facing facing)
{
    return changeAnimation(animation_, facing, RestartFinished::No);
}

Status ActorInstance::playAnimation(std::string_view name)
{
    return changeAnimation(name, facing_, RestartFinished::Yes);
}

ActorRenderState ActorInstance::renderState() const
{
    return makeActorRenderState(player_.currentFrame(), position_, scale_, flipX_);
}

} // namespace schuimm::room
