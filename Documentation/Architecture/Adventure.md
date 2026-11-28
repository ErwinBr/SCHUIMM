# Adventure scripting API

`Source/Adventure` connects scripts to the running game: actors, room objects, the camera and the
operations scripts wait for (walking, animation playback). It is the only place that knows those
mechanics. The VM, the compiler and the scheduler stay generic. They see natives, enums and
[completion tokens](../Scripting/Runtime.md#completion-tokens) only.

| Library | Depends on | Contents |
|---|---|---|
| `schuimm_adventure_api` | script bytecode | `adventureInterface()`: the declarations of every adventure function and of `enum Direction` |
| `schuimm_game_resource` | resource | `GameResource`: the game resource (`*.game.json`) |
| `schuimm_game_build` | compiler, room, game_resource, adventure_api | the game build: source discovery and project compilation (tools only) |
| `schuimm_adventure` | room, graphics (`Camera2D`, no graphics API), script runtime, adventure_api, game_resource | `AdventureContext`: the implementations, entity resolution, operation tokens; `AdventureNatives` and `AdventureRuntime`: the game session |

## The interface and the build

The adventure API is a [native interface](../Scripting/Runtime.md#native-interfaces) named
`adventure`.
- **Compiler:** it reads the declarations (`schuimmc --interface adventure`, or
  `CompileOptions::interfaces`), so room scripts call `walk_to` or use `Direction.SOUTH` without
  declaring them.
- **Runtime:** `AdventureNatives::bind(registry)` binds the implementations to the same
  declarations (`AdventureContext::bind` for tests that drive a context directly).
- **Linking:** checks the signatures and the identity of `adventure.Direction`.

The compiler links only `schuimm_adventure_api`, which holds declarations and no engine code.

## AdventureContext

```cpp
adventure::AdventureContext adventure;
REQUIRE(adventure.bind(host.natives()));   // before loading room scripts
adventure.setActiveRoom(room.get());       // at most one active room
adventure.setCamera(&camera);
```

- **Active room.** There is at most one; `setActiveRoom(nullptr)` means none.
  - **Changing it** cancels every pending operation of the previous room with reason
    "destroyed", and makes the context the new room's event listener
    ([Rooms.md](Rooms.md#operation-events)).
  - **Destroying the active room** detaches it automatically: the room tells its listener first,
    and the context cancels the room's operations and forgets the room. Detaching explicitly
    first is still the clearer order.
  - There is no registry of actors or objects besides the room's own.
- **Camera.** `setCamera(camera)`: the camera natives fail without one.
- **Lifetime.**
  - Destroying the context detaches it from the room, so the room never reports to a dead
    listener.
  - A game binds through `AdventureNatives`, which gives the natives shared ownership of the
    context ([Game sessions](#game-sessions-adventureruntime)). Bound directly with
    `AdventureContext::bind`, the natives capture the context, which must then outlive every
    coroutine that can call them.
- **Headless.** No graphics device is involved; the tests run it without a window.

### Record, deliver, execute

The end of an operation goes through three separate steps:

| Step | When | What happens |
|---|---|---|
| **record** | the moment the walk or playback ends: arrival in `room.update`, `stop_walking` inside a native, a C++ `walkTo`, … | The room reports the event ([Rooms.md](Rooms.md#operation-events)), and the context records the outcome on that operation's token (`CompletionToken::recordCompleted` / `recordCancelled`). Nothing is notified and no script runs. |
| **deliver** | `adventure.publish()`, once per frame | The recorded outcomes are delivered (`CompletionToken::deliver`): waiting coroutines become Ready or are cancelled. |
| **execute** | the `ScriptHost` update that follows | Resumed coroutines continue. |

Because the outcome is recorded when it happens, it is never reconstructed from the entity's
later state. A walk that arrives and is followed by another walk before publish still resumes
its waiter, and the same holds for a finished playback followed by another playback.

### Update order

Once per frame, in the game's `onUpdate`, followed by the application's script update:

1. input and pending requests (and room changes: `enterRoom`, `leaveRoom`);
2. `room.update(dt)`: movement, then animation playback (outcomes are recorded here);
3. `adventure.publish()`: delivers the recorded outcomes (`AdventureRuntime::update` does 2 and 3);
4. `ScriptHost::update(dt)`, run by `Application` after `onUpdate`: resumed coroutines continue;
5. rendering, which only reads.

Consequences:
- A coroutine never resumes from inside movement or animation code.
- A walk that arrives in this frame's room update resumes its waiter in this frame's script
  update.
- A walk started by a script begins moving in the next room update.
- An operation that a script changes (`stop_walking`, a new `walk_to`, `stop_animation`) is
  recorded at once:
  - a later `wait(op)` in the same coroutine sees the outcome immediately;
  - coroutines that are already waiting are notified at the next `publish()`.

## Game sessions: AdventureRuntime

A running game is one **game session**: the scripts of the whole game compiled into one program
([Compiler.md](../Scripting/Compiler.md#project-compilation)), its global variables, and the room
the player is in. `AdventureRuntime` (`Adventure/AdventureRuntime.h`) owns it and fixes the order
in which its parts are created and destroyed.

```cpp
// Once, when the ScriptHost is created: the adventure API in its native registry.
auto natives = adventure::AdventureNatives::bind(host.natives()).value();

// A game session from the game resource (GameResource.h).
auto game = adventure::AdventureRuntime::create(host, natives, locator,
                                                AssetPath::parse("Kitchen.game.json").value(), &camera).value();
game->enterRoom(AssetPath::parse("Rooms/Kitchen/Kitchen.room.json").value());
game->dispatchRoom("init");          // the room's "room:" handlers
game->dispatchRoom("ready");
game->dispatchObject("oven", "use"); // an object's handler
game->dispatchActor("cook", "talk_to");
game->start("intro");                // a global function, owned by the session

// Every frame, in onUpdate (then the Application updates the ScriptHost):
game->update(deltaSeconds);          // room update, then publish
```

| Part | Owned by | Lives |
|---|---|---|
| `AdventureNatives` (natives and the `AdventureContext` they act on) | the game, created once per `ScriptHost` | as long as the natives are registered; the context is shared by the natives, so no native can reach a destroyed context |
| program (`script::Program`) | the runtime (and every coroutine running it) | one session |
| global variables (`GlobalState`) | the runtime | one session: initialised once, they survive every room change |
| `RoomLibrary` | the runtime | one session (cached resources) |
| the active `RoomInstance`, its objects and actors, their behaviour instances | the runtime | from `enterRoom` until the next `enterRoom` or `leaveRoom` |
| session coroutines (`start`) | the session's execution owner | until they end, or the session ends |

- **Natives without a session** fail with "no adventure game session is running". Only one
  session uses a binding at a time; a second `create` fails with `InvalidState`. A new runtime
  is a new game: every global starts again at its initial value.
- **enterRoom(room)** accepts only rooms of the game resource. It creates the new instance with
  every object and actor first (all or nothing: on failure the current room stays). Then it
  destroys the previous room and makes the new one active. Nothing is dispatched
  automatically.
- **Destroying a room** cancels its pending operations ("destroyed"), the coroutines it started
  (room and object handlers) and those of each of its actors, also when they are running a
  global function they called or spawned, and it expires the behaviour instances. The globals
  and the session's coroutines are untouched.
- **Destroying the runtime** cancels the session's coroutines, destroys the room and detaches
  from the natives. Suspended coroutines and pending walks are cleaned up safely.
- **References** to entities of a destroyed room stay invalid: a global holding an old actor
  reference does not refer to the new actor with the same placement id (handles are never
  reused).
- Rooms are entered and left, and the runtime is destroyed, **between frames only**, never while
  the `ScriptHost` updates (checked in every build).

### Coroutine ownership

Who started a coroutine decides who owns it, not the file of the function it runs:

| Started by | Owner | Cancelled when |
|---|---|---|
| `dispatchRoom`, `dispatchObject`, `RoomInstance::start` | the room instance | the room is destroyed |
| `dispatchActor` | that actor | its room is destroyed |
| `AdventureRuntime::start` | the session | the session ends |

A spawned coroutine has its spawner's owner. A room handler that spawns the global function
`delayed_reward()` gives the room a coroutine that ends with the room. The same function started
with `start()` survives room changes.

### Room lifecycle handlers

`init`, `ready` and `exit` are the room handler names of the intended room lifecycle (a later
milestone): instantiate the room, initialise script state, `init` prepares the scene before it is
shown (it should not suspend), reveal the scene, `ready` begins visible behaviour, `exit` runs on
departure; then remaining room and actor coroutines are cancelled and the room is destroyed.
Today nothing dispatches them automatically: the game calls `dispatchRoom("init")` and so on
itself. When `init` runs, every object and actor of the room exists. Transitions, input gating
and checking that `init` does not suspend are future work.

### The game build

The build compiles a game's scripts with `schuimmc --game Game.game.json --assets DIR -o
Game.schb` (`Adventure/GameBuild.h`):
- the global scripts of the game resource;
- the script of each listed room;
- the script of each actor resource its placements use, each source once.

After compiling, the build checks how the resources and scripts fit together, so a designer
finds a mismatch before entering the room:
- every object a room script declares is an object of each room using the script; each mismatch
  is an error at the declaration, and all of them are reported in one run;
- every actor placement's initial animation and facing resolve in its actor resource.

```text
Rooms/Courtyard/Courtyard.sch:98:8: error: Room 'courtyard' (Rooms/Courtyard.room.json) has no object 'fann'.
    object fann:
           ^^^^
    note: The room's objects are 'chest', 'window', 'fan'.
```

| Checked by | What |
|---|---|
| the script compiler | the language: syntax, names, types, roles of declarations |
| the resource loaders | each resource on its own |
| the game build | relationships between resources and scripts |
| the runtime (`RoomLibrary`, `RoomInstance`) | the same relationships again, defensively, for what it loads |

The build and the runtime share one implementation of the object check
(`room::undefinedScriptObjects`, `Room/RoomScript.h`). The build reads only resource files and
image headers: no textures, decoded images or navigation graphs.

ActorDemo does it at build time (`Applications/ActorDemo/CMakeLists.txt`). A future DevKit calls
`compileGame(locator, game)` the same way and gets the program and the diagnostics with their
files.

## Entity references

`get_actor(id)` and `get_object(id)` look an entity up in the active room by its id. They return
a null reference if there is no such entity (or no active room). References are the room's
handles (`ObjectRef`).

Every other function resolves its reference against the active room:

| Reference | Result |
|---|---|
| null | script error "the actor reference is null" |
| expired: its room is no longer active, or its entity is gone | script error "actor reference #12 has expired: it is not in the active room 'plaza'" |
| a live actor or room object | the call proceeds |

`is_valid(e)` never fails; it is false for null and expired references. Functions declared for
`actor` accept only actors, and the compiler enforces it. Functions for `entity` accept both,
and fail for an entity that lacks the capability, such as `play_animation` on a static object.

## Functions

| Function | Category | Notes |
|---|---|---|
| `get_actor(id: string) -> actor`, `get_object(id: string) -> object` | immediate | null reference if missing |
| `is_valid(target: entity) -> bool` | immediate | |
| `get_position(target: entity) -> vec2` / `set_position(target, p)` | immediate | an actor's ground point, an object's anchor; validated (finite, within 2²⁰ px). Moving an actor stops its walk |
| `is_visible` / `set_visible(target: entity, v: bool)` | immediate | drawing only: movement, animation and scripts go on |
| `get_direction(target: actor) -> Direction` / `set_direction(target, d)` | immediate | the current animation must have a clip for the direction; turning stops a walk |
| `get_animation(target: entity) -> string` | immediate | the logical animation |
| `play_animation(target: entity, name: string) -> operation` | background | see [Animation operations](#animation-operations) |
| `stop_animation`, `pause_animation`, `resume_animation(target: entity)` | immediate | playback controls; they never affect walking |
| `walk_to(target: actor, destination: vec2)` | suspending | see [walk_to](#walk_to) |
| `stop_walking(target: actor)` / `is_walking(target: actor) -> bool` | immediate | |
| `get_scale` / `set_scale(target: actor, s: float)` | immediate | the base scale (0.001 to 1000) |
| `get_effective_scale(target: actor) -> float` | immediate | base × perspective |
| `get_walk_speed` / `set_walk_speed(target: actor, s: float)` | immediate | the reference speed at perspective scale 1; changing it keeps the walk in progress |
| `get_camera_position() -> vec2` / `set_camera_position(p)` | immediate | the view centre, clamped to the camera's bounds |
| `get_camera_zoom() -> float` / `set_camera_zoom(z)` | immediate | positive; clamped to the camera's limits |

Invalid requests are script errors naming the function, with the script location and call
stack. Examples are a scale of 0, a non-finite position, an unknown animation, or a walk for an
actor that cannot walk. They change nothing.

`Direction` maps to the engine's `Facing` through an explicit table (`facingFromDirection`,
`directionFromFacing`), never by ordinal.

## walk_to

`walk_to(hero, vec2(450, 800))` starts a walk and suspends the caller until **that** walk ends.
- **Destination.** Always the nearest reachable point: a destination outside the walkable area,
  or in a disconnected area, becomes the closest point of the actor's own area. Arriving there is
  success.
- **Rejected requests** are script errors and change nothing: the actor cannot walk, stands
  outside the walkable area, has no clip for a needed direction, or the destination is not
  finite.
- **Already there:** returns at once, without suspending.
- **Concurrent walks** use `spawn walk_to(guard, ...)`. There are no non-blocking variants.

Internally each accepted request gets a completion token keyed by its `MovementRequestId`. The
room reports how *that* request ended, when it ends:

| The request ended because | The waiting coroutine |
|---|---|
| it reached its destination | resumes |
| it was stopped (`stop_walking`, `set_position`, `set_direction`, an explicit `play_animation`) | is cancelled ("stopped") |
| another request replaced it (a later `walk_to`, possibly from another coroutine or from C++) | is cancelled ("replaced") |
| the room was left or destroyed | is cancelled ("destroyed") |

Cancellation is normal and is not reported as a script failure.

If the **waiting coroutine** is cancelled, for example because its room was left or a script
called `cancel(task)`, its walk stops where the actor is. This happens only if that walk is
still the actor's current one: a newer walk of another coroutine is never stopped.

## Animation operations

`play_animation(target, name)` changes the logical animation and returns an `operation` for the
resulting **playback**. It does not suspend.
- **Same playback.** If the clip was already playing or paused, the playback continues (the
  player's same-clip rule), and the same operation is returned.
- **New playback.** Otherwise a new playback, and a new operation, starts.
- **Identity** is the player's playback generation
  ([Animation.md](Animation.md#playback)), not the animation name.

How the playback ended, as the room reports it:

| The playback | Waiters of the operation |
|---|---|
| finished (a non-looping clip reached its end) | resume, even if another playback started before publish |
| ended unfinished: another `play_animation`, `stop_animation`, a walk's automatic walk/idle clip, a direction change selecting another clip | cancelled ("replaced") |
| the room was left or destroyed | cancelled ("destroyed") |
| is paused | still waiting (pausing neither completes nor cancels) |

- **Looping animations** never complete, so waiting for one is a script error ("animation 'spin'
  loops, …"). The animation itself keeps running.
- **Several waiters.** Any number of coroutines may wait for the same operation.
- **Cancelling a waiter** removes its subscription and never stops the animation.
- **Retained outcome.** An operation keeps its outcome after later playbacks: waiting for a
  finished playback returns at once, even if another animation plays now.
- **No history.** A record nobody can observe any more (no script value, no waiter) is dropped at
  the next publish.

## How movement and animation interact

| Call | Walk | Playback operations |
|---|---|---|
| `walk_to` | replaces the current walk (its waiter: "replaced") | the walk clip replaces the current playback |
| `stop_walking` | stops (waiter: "stopped") | idle replaces the walk clip |
| `set_position` (actor) | stops (waiter: "stopped") | idle replaces the walk clip |
| `set_direction` | stops, if it succeeds | replaced if the clip changes |
| `play_animation` (actor) | stops, if it succeeds (the explicit animation wins) | a new playback, unless the clip continues |
| `stop_animation` | unchanged: an actor may walk with its animation stopped | replaced |
| `pause_animation` / `resume_animation` | unchanged | unchanged (same playback) |

These are the actor contract of [Actors.md](Actors.md#movement), unchanged. The adventure layer
only reports the outcomes.

## Example

The ActorDemo courtyard's room script,
[`Courtyard.sch`](../../Applications/ActorDemo/Assets/Rooms/Courtyard/Courtyard.sch), in short:

```python
func patrol(gardener: actor):
	while True:
		walk_to(gardener, vec2(820, 300))     # waits until the gardener arrives
		set_direction(gardener, Direction.SOUTH)
		wait(1.5)
		walk_to(gardener, vec2(120, 260))

room:
	func ready():
		spawn patrol(get_actor("gardener"))   # owned by the room: ends when it is left

	func demonstrate():
		var hero: actor = get_actor("hero")
		var gardener: actor = get_actor("gardener")
		var fan: object = get_object("fan")
		var patrol_task: coroutine = spawn patrol(gardener)   # runs alongside
		walk_to(hero, vec2(800, 150))          # the balcony: walks to the nearest reachable point
		walk_to(hero, get_position(fan) + vec2(-36, 24))
		set_direction(hero, Direction.EAST)
		wait(play_animation(hero, "press_button"))   # a non-looping animation
		play_animation(fan, "spinning")              # loops on its own; never waited for
		spawn walk_to(hero, vec2(320, 480))          # a native, spawned directly
		cancel(patrol_task)                          # the gardener stops where it is
		set_position(gardener, get_position(hero) + vec2(70, 0))

object fan:
	func use():
		if get_animation(self) == "spinning":
			play_animation(self, "idle")
		else:
			play_animation(self, "spinning")
```

ActorDemo runs it through its game session (`AdventureRuntime`) and dispatches from C++:
- `dispatchRoom("ready")` when a room is entered (rooms without that handler ignore it);
- `dispatchRoom("demonstrate")` on P;
- `dispatchObject("fan", "use")` on F.

The walker actors of the hall and garden run their actor script, `walker.sch`.

There is no mouse-driven interaction dispatch yet. A headless test runs this script to the end
on the demo's own assets (`Adventure_TheDemoGameRunsThroughItsRooms`).

## Not in this milestone

The automatic room lifecycle and room transitions, joining coroutines, dialogue, inventory and
inventory scripts, verbs and mouse interaction, calling another entity's handlers from a script,
saving games, forced (direct) movement and smooth camera movement are planned. None of them
exist yet.

## Tests

`Tests/Adventure/AdventureTests.cpp` (headless, real `ScriptHost`, scripts compiled with the
interface) covers:
- the interface and its binding agreeing;
- the Direction table;
- lookups, null and expired references;
- every state native and its validation, with rejected requests changing nothing;
- `walk_to`:
  - suspension and arrival;
  - nearest reachable destinations;
  - already-there;
  - stopping, replacement and repositioning;
  - concurrent walks with spawn;
  - cancelling a walking coroutine;
- animation operations:
  - immediate return and waiting;
  - looping errors;
  - stop and replacement;
  - pause and resume;
  - several waiters;
  - cancelling a waiter;
  - locomotion replacing a playback;
  - outcomes retained after later playbacks;
- the camera;
- leaving the room cancelling its operations, with every token reclaimed;
- completion recorded at the source:
  - walk A arrives and C++ starts walk B before publish: A's waiter resumes;
  - the same for a finished playback followed by another;
- record and deliver separated (a stop is seen by the same coroutine at once, by suspended
  waiters at publish);
- destroying the active room, or the context, without detaching first;
- 20 000 spawned coroutines reclaimed, with bounded scheduler storage;
- the ActorDemo game, compiled by the game build and run by an `AdventureRuntime`: the courtyard
  script to its end, then the hall's walkers running their actor script.

`Tests/Adventure/RuntimeTests.cpp` (headless, a game of global, room and actor scripts compiled
by the game build) covers the game session:
- source discovery (each source once, conflicts) and the game resource's validation;
- globals that persist while rooms are destroyed and recreated, global functions of an inactive
  room;
- room, object and actor variables: per instance, reset with the room;
- `init` seeing every entity;
- coroutine ownership: room- and actor-owned global functions cancelled with the room, session
  coroutines surviving;
- old references staying invalid after a room is recreated;
- a new session starting afresh after one ended with suspended coroutines and a pending walk;
- natives without a session, a second session, unknown rooms, a missing or broken program.
