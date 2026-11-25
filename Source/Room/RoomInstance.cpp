#include "Room/RoomInstance.h"

#include "Core/Common/Assert.h"
#include "Core/Common/StringBuilder.h"
#include "Room/RoomScript.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace schuimm::room {

namespace {

// Process-wide, so handles are unique across instances, rooms and libraries; 64 bits never wrap
// in practice, so an id is never reused. 0 stays the null handle.
script::ObjectRef allocateObjectRef() noexcept
{
    static std::atomic<std::uint64_t> next{1};
    return script::ObjectRef{next.fetch_add(1, std::memory_order_relaxed)};
}

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
    return std::unique_ptr<RoomInstance>(new RoomInstance(std::move(room), host));
}

RoomInstance::RoomInstance(LoadedRoom room, script::ScriptHost& host)
    : room_(std::move(room))
    , host_(host)
{
    objects_.reserve(room_.resource->objects.size());
    for (const RoomObjectDefinition& definition : room_.resource->objects) {
        RoomObject object;
        object.definition = &definition;
        object.handle = allocateObjectRef();
        object.position = definition.position;
        objects_.push_back(std::move(object));
    }
}

RoomInstance::~RoomInstance()
{
    SCHUIMM_ASSERT(!host_.isUpdating(), "a RoomInstance must not be destroyed while the ScriptHost updates");
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
        std::find_if(objects_.begin(), objects_.end(), [handle](const RoomObject& o) { return o.handle == handle; });
    return found == objects_.end() ? nullptr : &*found;
}

const RoomObject* RoomInstance::object(script::ObjectRef handle) const noexcept
{
    return const_cast<RoomInstance*>(this)->object(handle);
}

Result<script::DispatchResult> RoomInstance::dispatch(std::string_view objectId, std::string_view event,
                                                      std::vector<script::Value> arguments)
{
    const RoomObject* target = object(objectId);
    if (target == nullptr) {
        return makeError(ErrorCode::NotFound, concat("room '", resource().id, "' has no object '", objectId, "'"));
    }
    if (!room_.script) return script::DispatchResult{};
    auto result = host_.dispatch(room_.script, objectId, event, target->handle, std::move(arguments));
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
