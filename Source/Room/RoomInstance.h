#pragma once

// A live room: the runtime objects created from a RoomResource's object definitions, their
// handles and mutable state, and the association with the room's compiled script.
//
// Immutable data (RoomResource, Program) is shared between all instances of a room; everything
// mutable lives here, so instances are independent and the resource never changes.
//
// Object handles: every runtime object gets a script::ObjectRef from a process-wide counter, so
// ids are unique across all instances and never reused (the ObjectRef contract in
// Script/Runtime/Value.h). After an instance is destroyed its handles are stale: they never
// refer to another object.
//
// Coroutines: the instance remembers the coroutines it started (dispatch(), start()) and cancels
// those still alive when it is destroyed, so no handler of a destroyed room keeps running. An
// instance must therefore not be destroyed while the ScriptHost updates (from a native or a
// script event callback): the running coroutine cannot be cancelled. Destroy it between frames.
// The ScriptHost must outlive its instances.

#include "Core/Common/Error.h"
#include "Core/Common/Geometry.h"
#include "Room/RoomLibrary.h"
#include "Room/RoomResource.h"
#include "Script/Runtime/Program.h"
#include "Script/Runtime/ScriptHost.h"
#include "Script/Runtime/Value.h"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::room {

// The runtime state of one interactive object. Initialised from its definition; changing it
// never changes the definition.
struct RoomObject {
    const RoomObjectDefinition* definition = nullptr; // In the shared RoomResource.
    script::ObjectRef handle;                         // Passed to its handlers as self.
    Vec2 position;
    bool visible = true;
    std::map<std::string, bool, std::less<>> flags;

    [[nodiscard]] const std::string& id() const noexcept { return definition->id; }
};

class RoomInstance {
public:
    // Creates the runtime objects of `room`. Checks the script's object declarations against
    // the resource again (validateRoomScript), so an instance never has a mismatched script.
    [[nodiscard]] static Result<std::unique_ptr<RoomInstance>> create(LoadedRoom room, script::ScriptHost& host);

    ~RoomInstance();
    RoomInstance(const RoomInstance&) = delete;
    RoomInstance& operator=(const RoomInstance&) = delete;

    [[nodiscard]] const RoomResource& resource() const noexcept { return *room_.resource; }
    [[nodiscard]] const std::shared_ptr<const RoomResource>& sharedResource() const noexcept { return room_.resource; }
    // nullptr if the room has no script.
    [[nodiscard]] const std::shared_ptr<const script::Program>& script() const noexcept { return room_.script; }

    // In the order of the resource's objects.
    [[nodiscard]] std::span<RoomObject> objects() noexcept { return objects_; }
    [[nodiscard]] std::span<const RoomObject> objects() const noexcept { return objects_; }
    // nullptr if there is no such object (in this instance).
    [[nodiscard]] RoomObject* object(std::string_view id) noexcept;
    [[nodiscard]] const RoomObject* object(std::string_view id) const noexcept;
    [[nodiscard]] RoomObject* object(script::ObjectRef handle) noexcept;
    [[nodiscard]] const RoomObject* object(script::ObjectRef handle) const noexcept;

    // The integration point for event dispatch: invokes the handler `event` of the object with
    // identifier `objectId`, with that object's handle as self (ScriptHost::dispatch).
    //   * NotFound: the room has no object `objectId`;
    //   * {handled = false}: the room has no script, or the script has no such handler;
    //   * otherwise the started coroutine.
    [[nodiscard]] Result<script::DispatchResult> dispatch(std::string_view objectId, std::string_view event,
                                                          std::vector<script::Value> arguments = {});
    // Starts a room-level function of the script (e.g. "enter"). NotFound without a script.
    [[nodiscard]] Result<script::CoroutineHandle> start(std::string_view function,
                                                        std::vector<script::Value> arguments = {});

    // Coroutines started by this instance that have not ended yet.
    [[nodiscard]] std::size_t activeCoroutineCount() const;

private:
    RoomInstance(LoadedRoom room, script::ScriptHost& host);
    void track(script::CoroutineHandle handle);
    void prune();

    LoadedRoom room_;
    script::ScriptHost& host_;
    std::vector<RoomObject> objects_;
    std::vector<script::CoroutineHandle> coroutines_;
};

} // namespace schuimm::room
