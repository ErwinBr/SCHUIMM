# Adventure scripting API

`Source/Adventure` connects scripts to the running game: actors, room objects, the camera and the
operations scripts wait for (walking, animation playback). It is the only place that knows those
mechanics. The VM, the compiler and the scheduler stay generic. They see natives, enums and
[completion tokens](../Scripting/Runtime.md#completion-tokens) only.

| Library | Depends on | Contents |
|---|---|---|
| `schuimm_adventure_api` | script bytecode | `adventureInterface()`: the declarations of every adventure function and of `enum Direction` |
| `schuimm_adventure` | room, graphics (`Camera2D`, no graphics API), script runtime, adventure_api | `AdventureContext`: the implementations, entity resolution, operation tokens |

## The interface and the build

The adventure API is a [native interface](../Scripting/Runtime.md#native-interfaces) named
`adventure`.
- **Compiler:** it reads the declarations (`schuimmc --interface adventure`, or
  `CompileOptions::interfaces`), so room scripts call `walk_to` or use `Direction.SOUTH` without
  declaring them.
- **Runtime:** `AdventureContext::bind(registry)` binds the implementations to the same
  declarations.
- **Linking:** checks the signatures and the identity of `adventure.Direction`.

The compiler links only `schuimm_adventure_api`, which holds declarations and no engine code.

## AdventureContext

```cpp
adventure::AdventureContext adventure;
REQUIRE(adventure.bind(host.natives()));   // before loading room scripts
adventure.setActiveRoom(room.get());       // at most one active room
adventure.setCamera(&camera);
```

- **Active room.** There is at most one; `setActiveRoom(nullptr)` means none. Changing it
  cancels every pending operation of the previous room with reason "destroyed". **Call
  `setActiveRoom(nullptr)` before destroying the active room.** There is no registry of actors or
  objects besides the room's own.
- **Camera.** `setCamera(camera)`: the camera natives fail without one.
- **Lifetime.** The bound natives capture the context, so it must outlive every coroutine that
  can call them. Stop or destroy the `ScriptHost` first.
- **Headless.** No graphics device is involved; the tests run it without a window.

### Update order

Once per frame, in the game's `onUpdate`, followed by the application's script update:

1. input and pending requests;
2. `room.update(dt)`: movement, then animation playback;
3. `adventure.publish()`: resolves the operations from the room's new state;
4. `ScriptHost::update(dt)`, run by `Application` after `onUpdate`: resumed coroutines continue;
5. rendering, which only reads.

Consequences:
- A coroutine never resumes from inside movement or animation code.
- A walk that arrives in this frame's room update resumes its waiter in this frame's script
  update.
- A walk started by a script begins moving in the next room update.
- An operation that a script changes (`stop_walking`, a new `walk_to`, `stop_animation`) reaches
  its waiters at the next `publish()`.

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

Internally each accepted request gets a completion token keyed by its `MovementRequestId`.
`publish()` resolves it:

| The actor's state at publish | The waiting coroutine |
|---|---|
| this request has Completed | resumes |
| this request was cancelled (`stop_walking`, `set_position`, `set_direction`, an explicit `play_animation`) | is cancelled ("stopped") |
| another request replaced it (a later `walk_to`, possibly from another coroutine) | is cancelled ("replaced") |
| the actor or room is gone | is cancelled ("destroyed") |

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

`wait(op)` and `publish()`:

| At publish | Waiters of the operation |
|---|---|
| same playback generation, player Finished | resume |
| the generation changed: another `play_animation`, `stop_animation`, a walk's automatic walk/idle clip, a direction change selecting another clip | cancelled ("replaced") |
| the entity or room is gone | cancelled ("destroyed") |
| paused | still waiting (pausing neither completes nor cancels) |

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

ActorDemo calls the room's functions and handlers from C++:
- `room.start("enter")` when the room is entered;
- `room.start("demonstrate")` on P;
- `room.dispatch("fan", "use")` on F.

There is no mouse-driven interaction dispatch yet. A headless test runs this script to the end
on the demo's own assets (`Adventure_TheDemoCourtyardScriptRunsToTheEnd`).

## Not in this milestone (Part II and later)

Script modules and global variables, cross-script calls, actor scripts and actor-local or
object-local state, room transitions, joining coroutines, dialogue, inventory, verbs and mouse
interaction, forced (direct) movement and smooth camera movement are planned. None of them exist
yet; scripts are one file per room, and all state lives in the engine.

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
- the ActorDemo courtyard script running to its end on the demo's assets.
