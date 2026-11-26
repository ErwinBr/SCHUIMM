#include "Room/RoomInstance.h"

#include "Core/Common/Assert.h"
#include "Core/Common/StringBuilder.h"
#include "Room/ObjectHandles.h"
#include "Core/Common/GeometryPredicates.h"
#include "Room/RoomScript.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace schuimm::room {

namespace {

} // namespace

Result<std::unique_ptr<RoomInstance>> RoomInstance::create(LoadedRoom room, script::ScriptHost& host)
{
    if (!room.resource) return makeError(ErrorCode::InvalidArgument, "RoomInstance::create: no room resource");
    if (auto status = validateRoomScript(*room.resource, room.script.get()); !status) {
        return std::unexpected(status.error());
    }
    if (auto status = validateRoomActors(room); !status) {
        return makeError(status.error().code, concat("room '", room.resource->id, "': ", status.error().message));
    }
    // Animated objects need their resolved animations (RoomLibrary::load provides them).
    const auto& definitions = room.resource->objects;
    if (!room.objectAnimations.empty() && room.objectAnimations.size() != definitions.size()) {
        return makeError(ErrorCode::InvalidArgument, concat("room '", room.resource->id, "': ", definitions.size(),
                                                            " objects but ", room.objectAnimations.size(),
                                                            " object animation sets"));
    }
    for (std::size_t i = 0; i < definitions.size(); ++i) {
        const bool resolved = i < room.objectAnimations.size() && room.objectAnimations[i] != nullptr;
        if (definitions[i].animation.has_value() != resolved) {
            return makeError(ErrorCode::InvalidArgument,
                             concat("room '", room.resource->id, "': objects[", i, "] ('", definitions[i].id, "'): ",
                                    resolved ? "animations for a static object" : "the animations are not resolved"));
        }
    }
    // Every actor is created before the instance exists: a failure publishes nothing.
    const auto& placements = room.resource->actors;
    std::vector<std::unique_ptr<ActorInstance>> actors;
    actors.reserve(placements.size());
    for (std::size_t i = 0; i < placements.size(); ++i) {
        auto actor = ActorInstance::create(placements[i].id, room.actors[i],
                                           placementConfiguration(placements[i], *room.actors[i]),
                                           room.resource->scaleMap ? &*room.resource->scaleMap : nullptr);
        if (!actor) {
            return makeError(actor.error().code, concat("room '", room.resource->id, "': ", actor.error().message));
        }
        actors.push_back(std::move(*actor));
    }
    return std::unique_ptr<RoomInstance>(new RoomInstance(std::move(room), host, std::move(actors)));
}

RoomInstance::RoomInstance(LoadedRoom room, script::ScriptHost& host, std::vector<std::unique_ptr<ActorInstance>> actors)
    : room_(std::move(room))
    , host_(host)
    , owner_(host.createOwner())
    , actors_(std::move(actors))
{
    const auto& definitions = room_.resource->objects;
    objects_.reserve(definitions.size());
    for (std::size_t i = 0; i < definitions.size(); ++i) {
        objects_.push_back(RoomObject(definitions[i], detail::allocateObjectHandle(),
                                      i < room_.objectAnimations.size() ? room_.objectAnimations[i] : nullptr));
        objects_.back().events_ = &events_; // After the copy into the vector (copies report nothing).
    }
    for (const std::unique_ptr<ActorInstance>& actor : actors_) actor->events_ = &events_;
}

RoomInstance::~RoomInstance()
{
    // Checked in every build: a native of this room's script may be running, and it would go on
    // using a room that no longer exists.
    SCHUIMM_VERIFY(!host_.isUpdating(), "a RoomInstance must not be destroyed while the ScriptHost updates");
    // Listeners learn of the destruction while the room and its entities still exist.
    if (RoomEventListener* listener = events_.listener) {
        events_.listener = nullptr;
        listener->roomDestroyed();
    }
    // Everything the room started, and everything those coroutines spawned.
    host_.cancelOwner(owner_);
}

RoomObject::RoomObject(const RoomObjectDefinition& definition, script::ObjectRef handle,
                       std::shared_ptr<const ObjectAnimationSet> animations)
    : definition_(&definition)
    , handle_(handle)
    , position_(definition.position)
    , animations_(std::move(animations))
{
    if (animations_) {
        // Validated when loading: the default animation exists.
        animation_ = animations_->defaultAnimation;
        const animation::AnimationClipRef* clip = animations_->find(animation_);
        SCHUIMM_ASSERT(clip != nullptr, "the default animation is validated when the room is loaded");
        if (clip != nullptr) player_.play(*clip);
    }
}

RoomObject::RoomObject(const RoomObject& other)
    : flags(other.flags)
    , definition_(other.definition_)
    , handle_(other.handle_)
    , position_(other.position_)
    , visible_(other.visible_)
    , animations_(other.animations_)
    , animation_(other.animation_)
    , player_(other.player_)
    , reportedGeneration_(other.reportedGeneration_)
{
}

void RoomObject::reportPlaybackChange(std::uint64_t before) noexcept
{
    if (player_.playbackGeneration() == before || reportedGeneration_ == before) return;
    reportedGeneration_ = before;
    if (events_ != nullptr) events_->playbackEnded(handle_, false, before, PlaybackOutcome::Replaced);
}

animation::AdvanceResult RoomObject::update(std::uint64_t ticks) noexcept
{
    const animation::AdvanceResult result = player_.advance(ticks);
    if (result.finished) {
        reportedGeneration_ = player_.playbackGeneration();
        if (events_ != nullptr) {
            events_->playbackEnded(handle_, false, player_.playbackGeneration(), PlaybackOutcome::Finished);
        }
    }
    return result;
}

Status RoomObject::setPosition(Vec2 position)
{
    const auto valid = [](float c) { return std::isfinite(c) && std::fabs(c) <= maxActorCoordinate; };
    if (!valid(position.x) || !valid(position.y)) {
        return makeError(ErrorCode::InvalidArgument,
                         concat("object '", id(), "': position (", position.x, ", ", position.y, ") must be finite and within ",
                                static_cast<double>(maxActorCoordinate), " pixels of the origin"));
    }
    position_ = position;
    return {};
}

Status RoomObject::requireAnimated(std::string_view operation) const
{
    if (animations_) return {};
    return makeError(ErrorCode::InvalidState,
                     concat("object '", id(), "' has no animations (", operation, " needs an animated object)"));
}

Status RoomObject::playAnimation(std::string_view name)
{
    if (auto status = requireAnimated("playing an animation"); !status) return status;
    const animation::AnimationClipRef* clip = animations_->find(name);
    if (clip == nullptr) {
        return makeError(ErrorCode::NotFound, concat("object '", id(), "' has no animation '", name, "'"));
    }
    const std::uint64_t before = player_.playbackGeneration();
    player_.play(*clip);
    reportPlaybackChange(before);
    animation_ = std::string(name);
    return {};
}

Status RoomObject::restartAnimation()
{
    if (auto status = requireAnimated("restarting an animation"); !status) return status;
    const std::uint64_t before = player_.playbackGeneration();
    player_.restart();
    reportPlaybackChange(before);
    return {};
}

Status RoomObject::pauseAnimation()
{
    if (auto status = requireAnimated("pausing an animation"); !status) return status;
    player_.pause();
    return {};
}

Status RoomObject::resumeAnimation()
{
    if (auto status = requireAnimated("resuming an animation"); !status) return status;
    player_.resume();
    return {};
}

Status RoomObject::stopAnimation()
{
    if (auto status = requireAnimated("stopping an animation"); !status) return status;
    const std::uint64_t before = player_.playbackGeneration();
    player_.stop();
    reportPlaybackChange(before);
    return {};
}

RoomObject* RoomInstance::object(std::string_view id) noexcept
{
    const auto found = std::find_if(objects_.begin(), objects_.end(), [id](const RoomObject& o) { return o.id() == id; });
    return found == objects_.end() ? nullptr : &*found;
}

const RoomObject* RoomInstance::object(std::string_view id) const noexcept
{
    return const_cast<RoomInstance*>(this)->object(id);
}

RoomObject* RoomInstance::object(script::ObjectRef handle) noexcept
{
    if (handle.isNull()) return nullptr;
    const auto found =
        std::find_if(objects_.begin(), objects_.end(), [handle](const RoomObject& o) { return o.handle() == handle; });
    return found == objects_.end() ? nullptr : &*found;
}

const RoomObject* RoomInstance::object(script::ObjectRef handle) const noexcept
{
    return const_cast<RoomInstance*>(this)->object(handle);
}

ActorInstance* RoomInstance::actor(std::string_view id) noexcept
{
    const auto found = std::find_if(actors_.begin(), actors_.end(),
                                    [id](const std::unique_ptr<ActorInstance>& a) { return a->id() == id; });
    return found == actors_.end() ? nullptr : found->get();
}

const ActorInstance* RoomInstance::actor(std::string_view id) const noexcept
{
    return const_cast<RoomInstance*>(this)->actor(id);
}

ActorInstance* RoomInstance::actor(script::ObjectRef handle) noexcept
{
    if (handle.isNull()) return nullptr;
    const auto found = std::find_if(actors_.begin(), actors_.end(),
                                    [handle](const std::unique_ptr<ActorInstance>& a) { return a->handle() == handle; });
    return found == actors_.end() ? nullptr : found->get();
}

const ActorInstance* RoomInstance::actor(script::ObjectRef handle) const noexcept
{
    return const_cast<RoomInstance*>(this)->actor(handle);
}

void RoomInstance::update(double deltaSeconds)
{
    // Movement first (positions, facing, walk/idle), then animation: an actor that arrives during
    // this update plays its idle clip for this update's ticks. Each is advanced exactly once.
    if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0) {
        for (const std::unique_ptr<ActorInstance>& actor : actors_) actor->advanceMovement(deltaSeconds);
    }
    const std::uint64_t ticks = clock_.advance(deltaSeconds);
    if (ticks == 0) return;
    for (const std::unique_ptr<ActorInstance>& actor : actors_) actor->update(ticks);
    for (RoomObject& object : objects_) object.update(ticks);
}

Result<MovementRequest> RoomInstance::walkTo(ActorInstance& actor, Vec2 destination, navigation::DestinationPolicy policy)
{
    const auto fail = [&actor](ErrorCode code, std::string_view reason) {
        return makeError(code, concat("actor '", actor.id(), "': ", reason));
    };
    if (this->actor(actor.handle()) != &actor) {
        return makeError(ErrorCode::InvalidArgument, concat("actor '", actor.id(), "' is not in room '", resource().id, "'"));
    }
    if (!std::isfinite(destination.x) || !std::isfinite(destination.y)) {
        return fail(ErrorCode::InvalidArgument, "the destination is not finite");
    }
    if (!actor.resource().movement) {
        return fail(ErrorCode::InvalidState, concat("actor resource '", actor.resource().id, "' has no movement configuration"));
    }
    const ActorMovementConfig& config = *actor.resource().movement;
    if (!room_.navigation) return fail(ErrorCode::InvalidState, concat("room '", resource().id, "' has no walkable areas"));
    const navigation::NavigationMap& map = *room_.navigation;
    const Vec2 from = actor.position();
    if (!map.contains(from)) {
        return fail(ErrorCode::NotFound,
                    concat("it is at (", from.x, ", ", from.y, "), outside the walkable area, and cannot walk from there"));
    }
    auto resolved = map.resolveDestination(from, destination, policy);
    if (!resolved) return fail(resolved.error().code, resolved.error().message);

    // Everything is checked before anything changes: a failed request leaves the actor (and any
    // walk in progress) as it was.
    const ActorResource& resource = actor.resource();
    const auto idleFacing = [&](Facing facing) -> Status {
        if (auto idle = resource.resolve(config.idleAnimation, facing); !idle) return std::unexpected(idle.error());
        return {};
    };
    const MovementRequestId request = allocateMovementRequest();
    if (distance(from, *resolved) <= geometryTolerance) {
        if (actor.isWalking()) {
            if (auto status = idleFacing(actor.facing()); !status) return fail(status.error().code, status.error().message);
        }
        actor.completeWalk(request, destination, *resolved);
        return MovementRequest{request, destination, *resolved, true};
    }
    auto path = map.findPath(from, *resolved);
    if (!path) return fail(path.error().code, path.error().message);
    std::vector<Facing> facings;
    for (std::size_t i = 1; i < path->waypoints.size(); ++i) {
        if (const auto facing = facingFromDirection(path->waypoints[i] - path->waypoints[i - 1])) facings.push_back(*facing);
    }
    if (facings.empty()) {
        actor.completeWalk(request, destination, *resolved);
        return MovementRequest{request, destination, *resolved, true};
    }
    for (const Facing facing : facings) {
        if (auto walk = resource.resolve(config.walkAnimation, facing); !walk) return fail(walk.error().code, walk.error().message);
        if (auto status = idleFacing(facing); !status) return fail(status.error().code, status.error().message);
    }
    actor.beginWalk(request, destination, *resolved, std::move(path->waypoints), facings.front());
    return MovementRequest{request, destination, *resolved, false};
}


RoomDrawList RoomInstance::drawList() const
{
    const RoomResource& room = resource();
    const auto sized = [this](const AssetPath& image, Vec2 position, Vec2 pivot) -> std::optional<RoomSprite> {
        const auto size = room_.imageSizes.find(image);
        if (size == room_.imageSizes.end()) return std::nullopt; // Not loaded through the library.
        const Extent2D extent = size->second;
        return RoomSprite{image, RectI{0, 0, extent.width, extent.height},
                          RectF{position.x - pivot.x, position.y - pivot.y, static_cast<float>(extent.width),
                                static_cast<float>(extent.height)},
                          false};
    };
    RoomDrawList list;
    if (room.background) list.background = sized(*room.background, {0, 0}, {0, 0});
    list.items.reserve(room.props.size() + objects_.size() + actors_.size());
    std::size_t sequence = 0;
    for (std::size_t i = 0; i < room.props.size(); ++i, ++sequence) {
        const RoomProp& prop = room.props[i];
        if (auto sprite = sized(prop.sprite, prop.position, prop.pivot)) {
            list.items.push_back(RoomDrawItem{RoomDrawKind::Prop, prop.position.y, sequence, i, std::move(*sprite)});
        }
    }
    for (std::size_t i = 0; i < objects_.size(); ++i, ++sequence) {
        const RoomObject& object = objects_[i];
        if (!object.visible()) continue;
        if (object.isAnimated()) {
            if (!object.player().hasClip()) continue;
            // The current frame, its pivot on the object's position (as for actors, unscaled).
            ActorRenderState state = makeActorRenderState(object.player().currentFrame(), object.position(), 1.0f, false);
            list.items.push_back(RoomDrawItem{RoomDrawKind::Object, object.position().y, sequence, i,
                                              RoomSprite{std::move(state.image), state.source, state.destination, false}});
            continue;
        }
        if (!object.definition().sprite) continue;
        if (auto sprite = sized(*object.definition().sprite, object.position(), object.definition().pivot)) {
            list.items.push_back(RoomDrawItem{RoomDrawKind::Object, object.position().y, sequence, i, std::move(*sprite)});
        }
    }
    for (std::size_t i = 0; i < actors_.size(); ++i, ++sequence) {
        const ActorInstance& actor = *actors_[i];
        if (!actor.visible()) continue;
        ActorRenderState state = actor.renderState();
        list.items.push_back(RoomDrawItem{RoomDrawKind::Actor, actor.position().y, sequence, i,
                                          RoomSprite{std::move(state.image), state.source, state.destination, state.flipX}});
    }
    std::sort(list.items.begin(), list.items.end(), [](const RoomDrawItem& a, const RoomDrawItem& b) {
        return a.depth != b.depth ? a.depth < b.depth : a.sequence < b.sequence;
    });
    return list;
}

Result<script::DispatchResult> RoomInstance::dispatch(std::string_view objectId, std::string_view event,
                                                      std::vector<script::Value> arguments)
{
    const RoomObject* target = object(objectId);
    if (target == nullptr) {
        return makeError(ErrorCode::NotFound, concat("room '", resource().id, "' has no object '", objectId, "'"));
    }
    if (!room_.script) return script::DispatchResult{};
    return host_.dispatch(room_.script, objectId, event, target->handle(), std::move(arguments), owner_);
}

Result<script::CoroutineHandle> RoomInstance::start(std::string_view function, std::vector<script::Value> arguments)
{
    if (!room_.script) {
        return makeError(ErrorCode::NotFound, concat("room '", resource().id, "' has no script (cannot start '", function, "')"));
    }
    return host_.start(room_.script, function, std::move(arguments), owner_);
}

std::size_t RoomInstance::activeCoroutineCount() const { return host_.activeCount(owner_); }

} // namespace schuimm::room
