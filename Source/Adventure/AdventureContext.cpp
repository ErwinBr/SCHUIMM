#include "Adventure/AdventureContext.h"

#include "Adventure/AdventureInterface.h"
#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/Semantics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <unordered_map>
#include <utility>

namespace schuimm::adventure {

using script::NativeContext;
using script::NativeResult;
using script::Value;

namespace {

// adventure.Direction ordinal -> Facing. Written out: the script enum's order is the interface's
// declaration, not the C++ enum's.
constexpr std::array<room::Facing, 8> directionTable = {
    room::Facing::North,     // NORTH
    room::Facing::NorthEast, // NORTH_EAST
    room::Facing::East,      // EAST
    room::Facing::SouthEast, // SOUTH_EAST
    room::Facing::South,     // SOUTH
    room::Facing::SouthWest, // SOUTH_WEST
    room::Facing::West,      // WEST
    room::Facing::NorthWest, // NORTH_WEST
};

// Script vectors are doubles; engine coordinates floats. Values that do not fit a float (or are
// not finite) are rejected before converting (converting them would be undefined).
std::optional<Vec2> toVec2(const Value& value) noexcept
{
    const auto fits = [](double c) { return std::isfinite(c) && std::fabs(c) <= 1e30; };
    if (!fits(value.vec2X()) || !fits(value.vec2Y())) return std::nullopt;
    return Vec2{static_cast<float>(value.vec2X()), static_cast<float>(value.vec2Y())};
}

std::optional<float> toFloat(double value) noexcept
{
    if (!std::isfinite(value) || std::fabs(value) > 1e30) return std::nullopt;
    return static_cast<float>(value);
}

Value vec2Value(Vec2 v) { return Value::fromVec2(static_cast<double>(v.x), static_cast<double>(v.y)); }

std::string describe(const Value& value)
{
    return concat(value.is(script::ScriptType::Actor) ? "actor" : "object", " reference #", value.asReference().id);
}

NativeResult failure(const Error& error) { return NativeResult::error(error.message); }

} // namespace

room::Facing facingFromDirection(std::uint32_t directionOrdinal) noexcept
{
    return directionOrdinal < directionTable.size() ? directionTable[directionOrdinal] : room::Facing::South;
}

std::uint32_t directionFromFacing(room::Facing facing) noexcept
{
    for (std::uint32_t i = 0; i < directionTable.size(); ++i) {
        if (directionTable[i] == facing) return i;
    }
    return 4; // SOUTH; unreachable: the table covers every facing.
}

AdventureContext::~AdventureContext() { detach(); }

void AdventureContext::cancelAll(const std::string& reason)
{
    // Move the records out first: delivering notifies schedulers, whose abandon handlers may
    // reach the room and report events back here.
    std::vector<Walk> walks = std::move(walks_);
    std::vector<Playback> playbacks = std::move(playbacks_);
    std::vector<script::CompletionRef> recorded = std::move(recorded_);
    walks_.clear();
    playbacks_.clear();
    recorded_.clear();
    for (Walk& walk : walks) walk.token->recordCancelled(reason);
    for (Playback& playback : playbacks) playback.token->recordCancelled(reason);
    // Safe to notify now: rooms are only attached, detached and destroyed between frames.
    for (Walk& walk : walks) walk.token->deliver();
    for (Playback& playback : playbacks) playback.token->deliver();
    for (script::CompletionRef& token : recorded) token->deliver();
}

void AdventureContext::detach()
{
    if (room_ != nullptr) {
        room_->setEventListener(nullptr);
        room_ = nullptr;
    }
    cancelAll("destroyed");
}

void AdventureContext::setActiveRoom(room::RoomInstance* room)
{
    if (room == room_) return;
    detach();
    room_ = room;
    if (room_ != nullptr) room_->setEventListener(this);
}

void AdventureContext::roomDestroyed()
{
    // The room calls this first thing in its destructor (it already forgot this listener).
    room_ = nullptr;
    cancelAll("destroyed");
}

void AdventureContext::movementEnded(script::ObjectRef actor, room::MovementRequestId request, room::MovementOutcome outcome)
{
    const auto walk = std::find_if(walks_.begin(), walks_.end(),
                                   [&](const Walk& w) { return w.actor == actor && w.request == request; });
    if (walk == walks_.end()) return; // Not a script's walk (or already ended).
    if (outcome == room::MovementOutcome::Completed) {
        walk->token->recordCompleted();
    } else {
        walk->token->recordCancelled(toString(outcome));
    }
    recorded_.push_back(std::move(walk->token));
    walks_.erase(walk);
}

void AdventureContext::playbackEnded(script::ObjectRef entity, bool isActor, std::uint64_t generation,
                                     room::PlaybackOutcome outcome)
{
    const auto playback = std::find_if(playbacks_.begin(), playbacks_.end(), [&](const Playback& p) {
        return p.entity == entity && p.actor == isActor && p.generation == generation;
    });
    if (playback == playbacks_.end()) return; // Nobody asked for an operation for it.
    if (outcome == room::PlaybackOutcome::Finished) {
        playback->token->recordCompleted();
    } else {
        playback->token->recordCancelled(toString(outcome));
    }
    recorded_.push_back(std::move(playback->token));
    playbacks_.erase(playback);
}

Result<AdventureContext::Entity> AdventureContext::resolve(const Value& reference) const
{
    const script::ObjectRef handle = reference.asReference();
    const bool isActor = reference.is(script::ScriptType::Actor);
    if (handle.isNull()) {
        return makeError(ErrorCode::InvalidArgument, concat("the ", isActor ? "actor" : "object", " reference is null"));
    }
    if (room_ == nullptr) {
        return makeError(ErrorCode::InvalidState, concat("there is no active room (", describe(reference), ")"));
    }
    Entity entity;
    if (isActor) {
        entity.actor = room_->actor(handle);
    } else {
        entity.object = room_->object(handle);
    }
    if (entity.actor == nullptr && entity.object == nullptr) {
        return makeError(ErrorCode::NotFound,
                         concat(describe(reference), " has expired: it is not in the active room '", room_->resource().id, "'"));
    }
    return entity;
}

const animation::AnimationPlayer& AdventureContext::playerOf(const Entity& entity) noexcept
{
    return entity.actor != nullptr ? entity.actor->player() : entity.object->player();
}

NativeResult AdventureContext::walkTo(const Value& actorValue, const Value& destinationValue)
{
    auto entity = resolve(actorValue);
    if (!entity) return failure(entity.error());
    room::ActorInstance& actor = *entity->actor;
    const std::optional<Vec2> destination = toVec2(destinationValue);
    if (!destination) return NativeResult::error("the destination is not a finite position");
    // Scripts always walk to the nearest reachable point; a rejected request changes nothing.
    auto request = room_->walkTo(actor, *destination, navigation::DestinationPolicy::NearestReachable);
    if (!request) return failure(request.error());
    if (request->completed) return NativeResult::returnValue(); // Already there.
    script::CompletionRef token = script::CompletionToken::create();
    const script::ObjectRef handle = actor.handle();
    const room::MovementRequestId id = request->id;
    // The waiter gave up: stop this walk, but only if it is still the actor's current one.
    token->setAbandonHandler([this, handle, id] {
        if (room_ == nullptr) return;
        room::ActorInstance* walker = room_->actor(handle);
        if (walker != nullptr && walker->isWalking() && walker->movement().request == id) walker->stopWalking();
    });
    walks_.push_back({handle, id, token});
    return NativeResult::await(std::move(token), true);
}

NativeResult AdventureContext::playAnimation(const Value& entityValue, std::string_view name)
{
    auto entity = resolve(entityValue);
    if (!entity) return failure(entity.error());
    const Status status = entity->actor != nullptr ? entity->actor->playAnimation(name) : entity->object->playAnimation(name);
    if (!status) return failure(status.error());
    const animation::AnimationPlayer& player = playerOf(*entity);
    const std::uint64_t generation = player.playbackGeneration();
    const script::ObjectRef handle = entityValue.asReference();
    const bool isActor = entity->actor != nullptr;
    // The same playback continues (the clip was already playing): the same operation.
    for (const Playback& playback : playbacks_) {
        if (playback.entity == handle && playback.actor == isActor && playback.generation == generation &&
            playback.token->isPending()) {
            return NativeResult::returnValue(Value::fromOperation(playback.token));
        }
    }
    script::CompletionRef token = script::CompletionToken::create();
    if (player.hasClip() && player.clip().clip().loop) {
        token->setNotAwaitable(concat("animation '", name, "' loops, so its playback never completes; "
                                      "wait for a non-looping animation, or wait(seconds)"));
    }
    playbacks_.push_back({handle, isActor, generation, token});
    return NativeResult::returnValue(Value::fromOperation(token));
}

void AdventureContext::publish()
{
    // Outcomes were recorded when they happened; this only tells the waiters. Delivering may
    // cancel coroutines, whose abandon handlers can report new events: take the list first.
    std::vector<script::CompletionRef> recorded = std::move(recorded_);
    recorded_.clear();
    for (const script::CompletionRef& token : recorded) token->deliver();
    // A playback nobody can observe (no script value, no waiter) needs no record.
    std::erase_if(playbacks_, [](const Playback& playback) { return playback.token->referenceCount() == 1; });
}

Status AdventureContext::bind(script::NativeRegistry& registry)
{
    using Arguments = std::span<const Value>;
    std::unordered_map<std::string, script::NativeImplementation> natives;
    const auto entityOrFail = [this](const Value& value) { return resolve(value); };

    // --- Lookup ---
    natives["get_actor"] = [this](NativeContext&, Arguments a) {
        const room::ActorInstance* actor = room_ != nullptr ? room_->actor(a[0].asString()) : nullptr;
        return NativeResult::returnValue(Value::fromActor(actor != nullptr ? actor->handle() : script::ObjectRef{}));
    };
    natives["get_object"] = [this](NativeContext&, Arguments a) {
        const room::RoomObject* object = room_ != nullptr ? room_->object(a[0].asString()) : nullptr;
        return NativeResult::returnValue(Value::fromObject(object != nullptr ? object->handle() : script::ObjectRef{}));
    };
    natives["is_valid"] = [this](NativeContext&, Arguments a) {
        return NativeResult::returnValue(Value::fromBool(resolve(a[0]).has_value()));
    };

    // --- Position and visibility ---
    natives["get_position"] = [entityOrFail](NativeContext&, Arguments a) {
        auto e = entityOrFail(a[0]);
        if (!e) return failure(e.error());
        return NativeResult::returnValue(vec2Value(e->actor != nullptr ? e->actor->position() : e->object->position()));
    };
    natives["set_position"] = [entityOrFail](NativeContext&, Arguments a) {
        auto e = entityOrFail(a[0]);
        if (!e) return failure(e.error());
        const std::optional<Vec2> position = toVec2(a[1]);
        if (!position) return NativeResult::error("the position is not finite");
        const Status status = e->actor != nullptr ? e->actor->setPosition(*position) : e->object->setPosition(*position);
        return status ? NativeResult::returnValue() : failure(status.error());
    };
    natives["is_visible"] = [entityOrFail](NativeContext&, Arguments a) {
        auto e = entityOrFail(a[0]);
        if (!e) return failure(e.error());
        return NativeResult::returnValue(Value::fromBool(e->actor != nullptr ? e->actor->visible() : e->object->visible()));
    };
    natives["set_visible"] = [entityOrFail](NativeContext&, Arguments a) {
        auto e = entityOrFail(a[0]);
        if (!e) return failure(e.error());
        if (e->actor != nullptr) {
            e->actor->setVisible(a[1].asBool());
        } else {
            e->object->setVisible(a[1].asBool());
        }
        return NativeResult::returnValue();
    };

    // --- Direction ---
    natives["get_direction"] = [entityOrFail](NativeContext&, Arguments a) {
        auto e = entityOrFail(a[0]);
        if (!e) return failure(e.error());
        return NativeResult::returnValue(Value::fromEnum(0, directionFromFacing(e->actor->facing())));
    };
    natives["set_direction"] = [entityOrFail](NativeContext&, Arguments a) {
        auto e = entityOrFail(a[0]);
        if (!e) return failure(e.error());
        const Status status = e->actor->setFacing(facingFromDirection(a[1].enumOrdinal()));
        return status ? NativeResult::returnValue() : failure(status.error());
    };

    // --- Animation ---
    natives["get_animation"] = [entityOrFail](NativeContext&, Arguments a) {
        auto e = entityOrFail(a[0]);
        if (!e) return failure(e.error());
        if (e->object != nullptr && !e->object->isAnimated()) {
            return NativeResult::error(concat("object '", e->object->id(), "' has no animations"));
        }
        return NativeResult::returnValue(Value::fromString(e->actor != nullptr ? e->actor->animation() : e->object->animation()));
    };
    natives["play_animation"] = [this](NativeContext&, Arguments a) { return playAnimation(a[0], a[1].asString()); };
    // Playback controls: stop ends the playback (its waiters are cancelled at the next publish);
    // pause and resume keep it, and none of them affects walking.
    const auto control = [entityOrFail](void (room::ActorInstance::*actorControl)() noexcept,
                                        Status (room::RoomObject::*objectControl)()) {
        return [entityOrFail, actorControl, objectControl](NativeContext&, Arguments a) {
            auto e = entityOrFail(a[0]);
            if (!e) return failure(e.error());
            if (e->actor != nullptr) {
                (e->actor->*actorControl)();
                return NativeResult::returnValue();
            }
            const Status status = (e->object->*objectControl)();
            return status ? NativeResult::returnValue() : failure(status.error());
        };
    };
    natives["stop_animation"] = control(&room::ActorInstance::stopAnimation, &room::RoomObject::stopAnimation);
    natives["pause_animation"] = control(&room::ActorInstance::pauseAnimation, &room::RoomObject::pauseAnimation);
    natives["resume_animation"] = control(&room::ActorInstance::resumeAnimation, &room::RoomObject::resumeAnimation);

    // --- Movement ---
    natives["walk_to"] = [this](NativeContext&, Arguments a) { return walkTo(a[0], a[1]); };
    natives["stop_walking"] = [entityOrFail](NativeContext&, Arguments a) {
        auto e = entityOrFail(a[0]);
        if (!e) return failure(e.error());
        e->actor->stopWalking();
        return NativeResult::returnValue();
    };
    natives["is_walking"] = [entityOrFail](NativeContext&, Arguments a) {
        auto e = entityOrFail(a[0]);
        if (!e) return failure(e.error());
        return NativeResult::returnValue(Value::fromBool(e->actor->isWalking()));
    };

    // --- Scale and speed ---
    const auto actorFloat = [entityOrFail](float (room::ActorInstance::*getter)() const noexcept) {
        return [entityOrFail, getter](NativeContext&, Arguments a) {
            auto e = entityOrFail(a[0]);
            if (!e) return failure(e.error());
            return NativeResult::returnValue(Value::fromFloat(static_cast<double>((e->actor->*getter)())));
        };
    };
    const auto setActorFloat = [entityOrFail](Status (room::ActorInstance::*setter)(float)) {
        return [entityOrFail, setter](NativeContext&, Arguments a) {
            auto e = entityOrFail(a[0]);
            if (!e) return failure(e.error());
            const std::optional<float> value = toFloat(a[1].asFloat());
            if (!value) return NativeResult::error(concat("the value ", script::formatFloat(a[1].asFloat()), " is not valid"));
            const Status status = (e->actor->*setter)(*value);
            return status ? NativeResult::returnValue() : failure(status.error());
        };
    };
    natives["get_scale"] = actorFloat(&room::ActorInstance::baseScale);
    natives["set_scale"] = setActorFloat(&room::ActorInstance::setBaseScale);
    natives["get_effective_scale"] = actorFloat(&room::ActorInstance::effectiveScale);
    natives["get_walk_speed"] = actorFloat(&room::ActorInstance::walkSpeed);
    natives["set_walk_speed"] = setActorFloat(&room::ActorInstance::setWalkSpeed);

    // --- Camera ---
    natives["get_camera_position"] = [this](NativeContext&, Arguments) {
        if (camera_ == nullptr) return NativeResult::error("there is no camera");
        return NativeResult::returnValue(vec2Value(camera_->position()));
    };
    natives["set_camera_position"] = [this](NativeContext&, Arguments a) {
        if (camera_ == nullptr) return NativeResult::error("there is no camera");
        const std::optional<Vec2> position = toVec2(a[0]);
        if (!position) return NativeResult::error("the camera position is not finite");
        camera_->setPosition(*position);
        return NativeResult::returnValue();
    };
    natives["get_camera_zoom"] = [this](NativeContext&, Arguments) {
        if (camera_ == nullptr) return NativeResult::error("there is no camera");
        return NativeResult::returnValue(Value::fromFloat(static_cast<double>(camera_->zoom())));
    };
    natives["set_camera_zoom"] = [this](NativeContext&, Arguments a) {
        if (camera_ == nullptr) return NativeResult::error("there is no camera");
        const std::optional<float> zoom = toFloat(a[0].asFloat());
        if (!zoom || !(*zoom > 0.0f)) {
            return NativeResult::error(concat("the zoom must be a positive number, got ", script::formatFloat(a[0].asFloat())));
        }
        camera_->setZoom(*zoom);
        return NativeResult::returnValue();
    };
    return registry.bind(adventureInterface(), std::move(natives));
}

} // namespace schuimm::adventure
