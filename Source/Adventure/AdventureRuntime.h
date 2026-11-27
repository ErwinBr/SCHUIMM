#pragma once

// The game session of an adventure: the one place that owns the scripting environment of a
// running game and the order in which its parts are created and destroyed.
//
//   AdventureNatives  the adventure API bound into a ScriptHost's native registry, once. It owns
//                     the AdventureContext the natives act on (shared ownership: a native can
//                     never reach a destroyed context). Without a running session every native
//                     fails with "no adventure game session is running".
//   AdventureRuntime  one game session, created from a game resource (GameResource.h):
//                       * the game's compiled program, loaded (validated and linked) once;
//                       * its GlobalState: every global variable, initialised once per session;
//                       * the RoomLibrary over that program, and at most one active RoomInstance;
//                       * an execution owner for the session's own coroutines (start()).
//                     Rooms come and go (enterRoom, leaveRoom); globals and session coroutines
//                     stay. Destroying the runtime ends the session: it cancels the session's
//                     coroutines, destroys the room (which cancels the room's and its actors'),
//                     and detaches from the natives. A new runtime is a new game.
//
// Ownership of coroutines (Language.md §9.2): a coroutine belongs to whoever started it and
// keeps that owner whatever it calls or spawns — a global function called or spawned by a room
// handler is the room's and ends with the room; started with start(), it is the session's and
// survives rooms.
//
// Frame order (Adventure.md): the game's onUpdate handles input, then calls update(dt), which
// advances the room (movement, animation) and publishes recorded operation outcomes; the
// Application then updates the ScriptHost once, which runs the scripts; then the room is drawn.
// Rooms are entered and left between those steps, never while the ScriptHost updates (checked:
// a native or script callback must not do it).
//
// Headless: no graphics device or audio is needed (the camera is plain state).

#include "Adventure/AdventureContext.h"
#include "Adventure/GameResource.h"
#include "Core/Common/Error.h"
#include "Graphics/Common/Camera2D.h"
#include "Resource/AssetLocator.h"
#include "Resource/AssetPath.h"
#include "Room/RoomInstance.h"
#include "Room/RoomLibrary.h"
#include "Script/Runtime/ScriptHost.h"
#include "Script/Runtime/ScriptState.h"

#include <memory>
#include <string_view>
#include <vector>

namespace schuimm::adventure {

class AdventureNatives {
public:
    // Registers the adventure API (adventureInterface()) in `registry`. Bind once per registry
    // (InvalidArgument when its names are already registered); the natives stay valid for as
    // long as anything holds them, also after this object is gone.
    [[nodiscard]] static Result<AdventureNatives> bind(script::NativeRegistry& registry);

    // The context the natives act on (its active room and camera belong to the running session).
    [[nodiscard]] AdventureContext& context() const noexcept { return state_->context; }
    [[nodiscard]] bool inSession() const noexcept { return state_->session; }

private:
    friend class AdventureRuntime;
    struct State {
        AdventureContext context;
        bool session = false;
    };
    explicit AdventureNatives(std::shared_ptr<State> state)
        : state_(std::move(state))
    {
    }
    std::shared_ptr<State> state_;
};

class AdventureRuntime {
public:
    // Starts a game session: reads the game resource `game` and its program through `locator`,
    // loads the program with `host` (whose registry `natives` was bound to) and initialises the
    // global variables. No room is active yet. Errors: the loaders' and Program::load's;
    // InvalidState if another session uses `natives`. `host`, `locator`, `natives` and `camera`
    // (may be null: the camera natives then fail) must outlive the runtime.
    [[nodiscard]] static Result<std::unique_ptr<AdventureRuntime>> create(script::ScriptHost& host, AdventureNatives& natives,
                                                                          const AssetLocator& locator, const AssetPath& game,
                                                                          Camera2D* camera = nullptr);
    ~AdventureRuntime();
    AdventureRuntime(const AdventureRuntime&) = delete;
    AdventureRuntime& operator=(const AdventureRuntime&) = delete;

    // Makes `room` (one of the game's rooms) the active room: a new instance with its objects
    // and actors, all created before anything runs (nothing is dispatched: call dispatchRoom
    // ("init"), etc.). The previous room is destroyed afterwards. All or nothing: on failure the
    // previous room stays active. Not while the ScriptHost updates.
    [[nodiscard]] Status enterRoom(const AssetPath& room);
    // Destroys the active room, if any (its coroutines and its actors' are cancelled).
    void leaveRoom();
    [[nodiscard]] room::RoomInstance* activeRoom() const noexcept { return room_.get(); }

    // Handlers of the active room's behaviours (RoomInstance::dispatchRoom, dispatch,
    // dispatchActor). Without an active room: {handled = false}.
    [[nodiscard]] Result<script::DispatchResult> dispatchRoom(std::string_view event, std::vector<script::Value> arguments = {});
    [[nodiscard]] Result<script::DispatchResult> dispatchObject(std::string_view object, std::string_view event,
                                                                std::vector<script::Value> arguments = {});
    [[nodiscard]] Result<script::DispatchResult> dispatchActor(std::string_view actor, std::string_view event,
                                                               std::vector<script::Value> arguments = {});
    // Starts global function `function` as a session coroutine: it survives room changes and
    // ends with the session.
    [[nodiscard]] Result<script::CoroutineHandle> start(std::string_view function, std::vector<script::Value> arguments = {});

    // Advances the active room by `deltaSeconds` and publishes the recorded operation outcomes
    // (the step before the ScriptHost update; see the frame order above).
    void update(double deltaSeconds);

    [[nodiscard]] const GameResource& game() const noexcept { return game_; }
    [[nodiscard]] const std::shared_ptr<const script::Program>& program() const noexcept { return program_; }
    [[nodiscard]] const std::shared_ptr<script::GlobalState>& globals() const noexcept { return globals_; }
    [[nodiscard]] AdventureContext& context() const noexcept { return natives_.context(); }
    // The game's rooms and actors (loaded resources are cached; e.g. to prepare a room's textures
    // before entering it).
    [[nodiscard]] room::RoomLibrary& library() noexcept { return library_; }
    [[nodiscard]] script::ExecutionOwner sessionOwner() const noexcept { return owner_; }
    // Session coroutines that have not ended.
    [[nodiscard]] std::size_t sessionCoroutineCount() const { return host_.activeCount(owner_); }

private:
    AdventureRuntime(script::ScriptHost& host, AdventureNatives& natives, const AssetLocator& locator, GameResource game,
                     std::shared_ptr<const script::Program> program);

    script::ScriptHost& host_;
    AdventureNatives& natives_;
    GameResource game_;
    std::shared_ptr<const script::Program> program_;
    std::shared_ptr<script::GlobalState> globals_;
    room::RoomLibrary library_;
    script::ExecutionOwner owner_;
    std::unique_ptr<room::RoomInstance> room_;
};

} // namespace schuimm::adventure
