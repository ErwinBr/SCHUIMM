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

AdventureContext::~AdventureContext() { cancelAll("destroyed"); }

void AdventureContext::cancelAll(const std::string& reason)
{
    // Move the records out first: cancelling notifies schedulers, whose abandon handlers could
    // otherwise see half-cleared lists.
    std::vector<Walk> walks = std::move(walks_);
    std::vector<Playback> playbacks = std::move(playbacks_);
    walks_.clear();
    playbacks_.clear();
    for (Walk& walk : walks) walk.token->cancel(reason);
    for (Playback& playback : playbacks) playback.token->cancel(reason);
}

void AdventureContext::setActiveRoom(room::RoomInstance* room)
{
    if (room == room_) return;
    cancelAll("destroyed");
    room_ = room;
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
    // Resolve outside the loops: resolving notifies schedulers, and their abandon handlers may
    // touch actors (never these lists).
    std::vector<std::pair<script::CompletionRef, std::string>> cancelled;
    std::vector<script::CompletionRef> completed;
    std::erase_if(walks_, [&](const Walk& walk) {
        const room::ActorInstance* actor = room_ != nullptr ? room_->actor(walk.actor) : nullptr;
        if (actor == nullptr) {
            cancelled.emplace_back(walk.token, "destroyed");
            return true;
        }
        const room::MovementState& state = actor->movement();
        if (state.request != walk.request) {
            cancelled.emplace_back(walk.token, "replaced");
            return true;
        }
        if (state.status == room::MovementStatus::Completed) {
            completed.push_back(walk.token);
            return true;
        }
        if (state.status != room::MovementStatus::Moving) {
            cancelled.emplace_back(walk.token, "stopped");
            return true;
        }
        return false;
    });
    std::erase_if(playbacks_, [&](const Playback& playback) {
        if (playback.token->referenceCount() == 1 || !playback.token->isPending()) {
            return true; // Nobody can observe it any more (no script value, no waiter).
        }
        Entity entity;
        if (room_ != nullptr) {
            if (playback.actor) {
                entity.actor = room_->actor(playback.entity);
            } else {
                entity.object = room_->object(playback.entity);
            }
        }
        if (entity.actor == nullptr && entity.object == nullptr) {
            cancelled.emplace_back(playback.token, "destroyed");
            return true;
        }
        const animation::AnimationPlayer& player = playerOf(entity);
        if (player.playbackGeneration() != playback.generation) {
            cancelled.emplace_back(playback.token, "replaced");
            return true;
        }
        if (player.status() == animation::PlaybackStatus::Finished) {
            completed.push_back(playback.token);
            return true;
        }
        return false;
    });
    for (const script::CompletionRef& token : completed) token->complete();
    for (const auto& [token, reason] : cancelled) token->cancel(reason);
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
