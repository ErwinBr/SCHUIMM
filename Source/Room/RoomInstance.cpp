#include "Room/RoomInstance.h"

#include "Core/Common/Assert.h"
#include "Core/Common/StringBuilder.h"
#include "Room/ObjectHandles.h"
#include "Room/RoomScript.h"

#include <algorithm>
#include <cstdint>

namespace schuimm::room {

namespace {

bool isAlive(const script::ScriptHost& host, script::CoroutineHandle handle)
{
    const auto state = host.state(handle);
    return state && !script::isTerminal(*state);
}

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
    // Every actor is created before the instance exists: a failure publishes nothing.
    const auto& placements = room.resource->actors;
    std::vector<std::unique_ptr<ActorInstance>> actors;
    actors.reserve(placements.size());
    for (std::size_t i = 0; i < placements.size(); ++i) {
        auto actor = ActorInstance::create(placements[i].id, room.actors[i],
                                           placementConfiguration(placements[i], *room.actors[i]));
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
    , actors_(std::move(actors))
{
    objects_.reserve(room_.resource->objects.size());
    for (const RoomObjectDefinition& definition : room_.resource->objects) {
        objects_.push_back(RoomObject(definition, detail::allocateObjectHandle()));
    }
}

RoomInstance::~RoomInstance()
{
    // Checked in every build: the running coroutine cannot be cancelled, so it would go on running
    // for a room that no longer exists.
    SCHUIMM_VERIFY(!host_.isUpdating(), "a RoomInstance must not be destroyed while the ScriptHost updates");
    for (const script::CoroutineHandle handle : coroutines_) {
        if (isAlive(host_, handle)) host_.cancel(handle);
    }
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
    const std::uint64_t ticks = clock_.advance(deltaSeconds);
    if (ticks == 0) return;
    for (const std::unique_ptr<ActorInstance>& actor : actors_) actor->update(ticks);
}

RoomDrawList RoomInstance::drawList() const
{
    RoomDrawList list;
    list.reserve(actors_.size());
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        const ActorInstance& actor = *actors_[i];
        if (!actor.visible()) continue;
        list.push_back(RoomDrawItem{actor.position().y, i, actor.renderState()});
    }
    std::sort(list.begin(), list.end(), [](const RoomDrawItem& a, const RoomDrawItem& b) {
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
    auto result = host_.dispatch(room_.script, objectId, event, target->handle(), std::move(arguments));
    if (result && result->handled) track(result->handle);
    return result;
}

Result<script::CoroutineHandle> RoomInstance::start(std::string_view function, std::vector<script::Value> arguments)
{
    if (!room_.script) {
        return makeError(ErrorCode::NotFound, concat("room '", resource().id, "' has no script (cannot start '", function, "')"));
    }
    auto handle = host_.start(room_.script, function, std::move(arguments));
    if (handle) track(*handle);
    return handle;
}

std::size_t RoomInstance::activeCoroutineCount() const
{
    return static_cast<std::size_t>(std::count_if(coroutines_.begin(), coroutines_.end(),
                                                  [this](script::CoroutineHandle h) { return isAlive(host_, h); }));
}

void RoomInstance::track(script::CoroutineHandle handle)
{
    prune();
    coroutines_.push_back(handle);
}

void RoomInstance::prune()
{
    std::erase_if(coroutines_, [this](script::CoroutineHandle h) { return !isAlive(host_, h); });
}

} // namespace schuimm::room
