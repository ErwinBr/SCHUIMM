# Actors

Actors are the figures of an adventure: the player character, other people, animated things.
This document covers:
- the actor resource format;
- how rooms place actors;
- how actor instances live and die with their room;
- how their animation is chosen, advanced and drawn.

The code is in `Source/Room` (library `schuimm_room`), on top of [Animation.md](Animation.md).
Drawing is in `Source/RoomGraphics`.

## Principles

- **Actor resources define what actors are**: appearance, animations, defaults and their script.
- **Room resources define where and how actors appear**: placements.
- **Actor scripts will define how actors behave.** A resource can reference its script now; actor
  scripts are not executed yet.
- **Actor instances hold temporary, room-scoped runtime state.**
- **Room instances own their actors and control their lifetime.**
- **Global variables will provide designer-controlled gameplay continuity** across rooms.
- **Saved games will restore runtime state** rather than perform ordinary room initialisation.

## Separate concepts

| | What it is | Mutable | Shared |
|---|---|---|---|
| `AnimationResource` | clips and frames ([Animation.md](Animation.md)) | no | by every actor resource that references it |
| `ActorResource` (`Room/ActorResource.h`) | what an actor is: animations, facing mappings, defaults, script reference | no | by every instance, in every room |
| `ActorPlacement` (`Room/RoomResource.h`) | where and how an actor appears in one room | no | part of the room resource |
| `ActorInstance` (`Room/ActorInstance.h`) | a live actor: identity, position, facing, visibility, scale, animation and playback | yes | no |
| `RoomInstance` | owns the room's actors, next to its room objects | yes | no |

Actors are not room objects. `ActorInstance` does not derive from `RoomObject`, and there is no
entity hierarchy or ECS.

## The actor resource format, version 1

A complete example is
[`walker.actor.json`](../../Applications/ActorDemo/Assets/Actors/Walker/walker.actor.json). All
its clips and images are in the same folder.

```json
{
    "version": 1,
    "id": "walker",
    "script": "Actors/Walker/walker.schb",
    "animation_resources": { "main": "Actors/Walker/walker.anim.json" },
    "animations": {
        "idle": {
            "south": { "resource": "main", "clip": "idle_south" },
            "north": { "resource": "main", "clip": "idle_north" },
            "west": { "resource": "main", "clip": "idle_west" },
            "east": { "resource": "main", "clip": "idle_west", "flip_x": true },
            "fallback": "south"
        },
        "wave": { "south": { "resource": "main", "clip": "wave_south" }, "fallback": "south" }
    },
    "default_animation": "idle",
    "default_facing": "south",
    "default_scale": 2.0
}
```

| Property | Type | Rules |
|---|---|---|
| `version` | integer, **required** | must be `1` (formats are provisional before release: [ResourceFormats.md](ResourceFormats.md)) |
| `id` | identifier, **required** | names the actor *definition*, not an instance |
| `script` | asset path | a compiled script (`.schb`); it must exist. See [Scripts](#scripts). |
| `animation_resources` | object, **required** | at least one; a local name (identifier) maps to an `*.anim.json` asset |
| `animations` | object, **required** | at least one; logical animation name (identifier) to its facing mappings |
| `default_animation` | identifier, **required** | must be one of `animations` |
| `default_facing` | facing, **required** | the default animation must resolve with it |
| `default_scale` | number | the base scale: default 1, from 0.001 to 1000 |
| `perspective_scale` | boolean | default `true`: the room's scale map scales the actor ([Perspective](#perspective)) |
| `movement` | object | how the actor walks ([Movement](#movement)); leave it out for decorative actors |

**Animations:**
- A **mapping** is `{ "resource": <local name>, "clip": <clip name>, "flip_x": <boolean, default false> }`.
  The clip must exist in that animation resource.
- An **animation** contains either `"any"` alone, or one or more facing names plus an optional
  `"fallback"`.
- `"any"` is one mapping used for every facing.
- `"fallback"` must name one of the animation's mapped facings; there are no chains.
- **Facing names:** `north`, `north_east`, `east`, `south_east`, `south`, `south_west`, `west`,
  `north_west`.

**Clips and names:** an actor resource references clips; it never copies frames. Logical
animation names such as `idle`, `walk` or `talk` are the designer's choice: nothing in the
engine knows any of them.

**Errors** follow the shared JSON rules: unknown properties, duplicate keys and other versions are
rejected, and messages read `"<file>: <property path>: <reason>"`. A missing referenced
animation resource, image or script keeps its own code (`NotFound`).

### Resource identity and instance identity

The resource `id` (`"walker"`) names the reusable definition. A room names each placement
itself:
- two guards may both use `guard.actor.json` as `guard_01` and `guard_02`;
- two rooms may each have a placement `hero` of the same resource.

Instance identifiers are room-local. They are unique within the room's actors and need not be
unique across the game. Rooms are not checked against each other.

## Facing and directional animation

`Facing` (`Room/Facing.h`) has the eight compass directions and is independent of the animation:
an actor can face west while playing `idle` or `walk`. Actors need artwork only for the
directions they use.

`ActorResource::resolve(animation, facing)` picks the clip in this order:
1. the explicit mapping for that facing;
2. the direction-independent mapping (`any`);
3. the mapping of the animation's `fallback` facing;
4. otherwise `NotFound`, naming the actor, animation and facing. No other direction is ever
   chosen silently.

**Mirroring** happens only through an explicit `flip_x` on a mapping; the engine never mirrors on
its own. The walker's `east` mapping is `idle_west` mirrored.

**Changing animation and facing together** (`ActorInstance::setAnimation(name, facing)`) resolves
the whole combination first and changes the actor only if it resolves. Movement code needs this.
Take an actor that is idle and facing south, where `idle` has no north clip and `walk` has no
south clip:
- calling `setFacing(North)` then `playAnimation("walk")` fails at the first step;
- calling them the other way round also fails at the first step;
- `setAnimation("walk", North)` succeeds.

`setFacing(f)` is `setAnimation(current animation, f)`.

**Changing facing** (`ActorInstance::setFacing`, and `setAnimation` in general):
- **Same clip:** if the new facing resolves to the clip already selected (perhaps mirrored
  differently), playback continues untouched, including a paused state.
- **Different clip:** it starts from its first frame. The phase is not synchronised between
  directional clips yet.
- **No clip:** if the current animation has no clip for the new facing, the call fails and the
  actor keeps its previous facing, clip and playback.

## Actor instances

- **Identity:** an `ActorInstance` has a room-local `id`, a runtime `handle()` (a
  `script::ObjectRef`) and a shared `ActorResource`. All three are fixed at creation.
- **No copying or moving:** an actor can be neither copied nor moved. `ActorInstance::create`
  returns a `std::unique_ptr` and the room owns its actors that way, so an identity is never
  duplicated, overwritten or moved out of its room, and an actor's address is fixed for its
  lifetime.
- **State:** position, facing, visibility, scale, the logical animation and its playback. All of
  it lives in the instance; the resources never change.
- **Validation:** changes are validated, and a failed one leaves the actor exactly as it was.
  - `setPosition` requires finite coordinates within 2^20.
  - `setBaseScale` requires a value from 0.001 to 1000.
  - `setFacing` and `playAnimation` require a clip that resolves.
- **Playing an animation:** `playAnimation(name)` is "play this":
  - it uses the current facing;
  - asking for the clip that is already playing or paused does not restart it;
  - a stopped or finished clip starts again, so a finished wave can be replayed.

  `setAnimation` and `setFacing` never restart the clip that is already selected, whatever its
  status. Re-asserting the current animation every frame therefore leaves a finished clip on its
  last frame.
- **Playback controls:** `restartAnimation`, `pauseAnimation`, `resumeAnimation`,
  `stopAnimation`.
- **Advancing:** `update(ticks)` advances playback; nothing else does.

**Position and scale:**
- `position` is the actor's **ground point** in room coordinates, normally its feet. The current
  frame's pivot is placed there.
- Frame changes never move the actor, even when frames differ in size or pivot.
- The **scale** is uniform and scales the artwork around the ground point. It is the base scale
  times the room's perspective scale ([Perspective](#perspective)).

**Handles:**
- Room objects and actors take handles from one process-wide counter
  (`Room/ObjectHandles.h`), so a handle never collides with another object's or actor's, and is
  never reused.
- A handle is valid only while its actor exists. When the room is destroyed, its handles become
  stale.
- Instantiating the room again gives new handles, so an old handle never finds a new actor,
  even one with the same id and resource.

## Actors in rooms

### Placements

A room resource lists its actors in an optional `actors` array ([Rooms.md](Rooms.md)).

```json
"actors": [
    { "id": "hero", "actor": "Actors/Walker/walker.actor.json", "position": [320, 300], "facing": "south" },
    { "id": "guard_01", "actor": "Actors/Walker/walker.actor.json", "position": [180, 230],
      "facing": "east", "animation": "idle", "scale": 1.5, "visible": true }
]
```

| Property | Type | Rules |
|---|---|---|
| `id` | identifier, **required** | unique among the room's actors. It has its own namespace: an actor may share an id with a room object. |
| `actor` | asset path, **required** | the actor resource |
| `position` | point, **required** | the ground point. It need not be inside the room or a walkable area. |
| `facing` | facing | default: the resource's `default_facing` |
| `animation` | identifier | default: the resource's `default_animation` |
| `scale` | number | base scale; default: the resource's `default_scale`; from 0.001 to 1000 |
| `visible` | boolean | default `true` |
| `walk_speed` | number | overrides the resource's `movement.speed` for this actor (it must be able to walk) |

`RoomLibrary::load` loads each placement's actor resource (cached in its `ActorLibrary`) and
checks that the placement's animation and facing resolve. Errors name the placement, for example
`Rooms/Hall.room.json: actors[1] ('cook'): actor 'cook' has no animation 'dance'`.

### Normal room initialisation

**Single active room:** SCHUIMM has at most one live room instance. A transition from room A to
room B destroys A before B is instantiated. Immutable resources stay cached in the library.

**Every instantiation creates fresh actors** from the authored placements, by these steps:
1. resolve each placement's actor resource;
2. validate the effective configuration;
3. create every `ActorInstance` with a fresh handle;
4. start its animation;
5. publish the room.

If any actor fails, `RoomInstance::create` returns an error and no partial room exists.

**Nothing is remembered between visits.** There is no "first visit" and no per-room actor memory:
a position, facing, animation, scale or visibility changed during a visit is gone when the room
is destroyed. Separate placements of the same actor resource in different rooms are separate
instances that share only immutable data.

### Designer-managed persistence

Story continuity is the designer's job, done with global script variables (a later milestone).
Room-entry behaviour will check those variables and configure the fresh actors, for example to:
- hide an actor who has left;
- choose an outfit animation;
- move an actor to another spot.

There is deliberately no automatic actor persistence.

### Saved games

Loading a saved game is the one exception to normal initialisation: the saved room will be
reconstructed and its actors' runtime state restored instead of the placements.

**What an actor's state is:**
- room-local id and actor resource path;
- position, facing, visibility, scale;
- logical animation;
- movement (the request, its destinations, the path and the progress along it);
- playback (clip, elapsed ticks, status).

These are all explicit and readable, so a future save system can capture them.

**What a save must not contain:**
- raw pointers or GPU handles;
- process-local `ObjectRef` values, which are never reused and mean nothing in another process.

The save system will define how script references are reconstructed. None of this is implemented
yet.

### Ownership, lookup and lifetime

**Lookup:**
- `RoomInstance::actors()` returns the actors in placement order as an `ActorView`: a span-like
  view of `ActorInstance&` (`actors()[i]`, `size()` and range-for all work).
- `actor(id)` and `actor(ObjectRef)` return nullptr if there is no such actor in this instance.
- Objects and actors are distinct kinds: `object()` never returns an actor and `actor()` never
  returns an object.

**Lifetime of what lookups return:** the collection is fixed when the instance is created, so
spans, pointers and references stay valid until the instance is destroyed, and no longer. Do not
keep them across a room change.

**Destroying a room:**
- destroys its actors, makes their handles stale, and releases their references to the
  resources;
- cancels the room's coroutines, as before;
- is refused in every build (`SCHUIMM_VERIFY`, release builds included) while the `ScriptHost`
  updates.

Cached resources and shared textures survive.

## Movement

Actors walk through the room's walkable areas ([Navigation.md](Navigation.md)). Walking needs a
`movement` block in the actor resource:

```json
"movement": { "speed": 110, "walk_animation": "walk", "idle_animation": "idle", "perspective_speed": true }
```

| Property | Type | Rules |
|---|---|---|
| `speed` | number, **required** | room pixels per second at perspective scale 1, from 0.001 to 100000 |
| `walk_animation` | identifier, **required** | a logical animation of the actor, played while walking |
| `idle_animation` | identifier, **required** | selected when a walk completes or is cancelled |
| `perspective_speed` | boolean | default `true`: the speed is multiplied by the room's perspective scale at the actor's feet |

**Animation names** are the designer's (`run`, `crawl` and `fly` work as well as `walk`); the
engine knows none of them.

**Decorative actors** leave the block out and cannot walk; asking them to is an `InvalidState`
error.

**Per instance:**
- **Walk speed:** a placement's `walk_speed` overrides the speed, and so does
  `ActorInstance::setWalkSpeed`, also during a walk.
- **Perspective scaling:** `setPerspectiveScaling` turns it on or off.

**Ownership:** the movement state belongs to the actor (`ActorInstance::movement()`, a
`MovementController` component), so no two actors share any. The room holds the navigation the
paths come from.

### Walking

`RoomInstance::walkTo(actor, destination, policy = NearestReachable)` takes a room-coordinate
destination. It is **transactional**: everything is checked before anything changes, so a
failure leaves the actor, and any walk in progress, as it was.

**Checks, in order:**
1. The actor must belong to this room (`InvalidArgument`), and the destination must be finite.
2. The actor needs a `movement` block (`InvalidState`).
3. The room needs walkable areas (`InvalidState`).
4. The actor must stand in the walkable area. Otherwise the result is `NotFound`; it is never
   teleported. Placing an actor anywhere with `setPosition` is allowed, and it then cannot walk
   until it is put back on walkable ground.
5. **Destination:** with `NearestReachable`, a click outside the walkable area, or on an area
   that cannot be reached such as the demo's balcony, becomes the nearest point the actor can
   reach. With `Strict`, it must be walkable and reachable, or the result is `NotFound`. The
   request keeps both the requested and the resolved destination.
6. **Path:** found by `findPath`, from where the actor stands now.
7. **Animations:** a facing is computed for each segment (below). The walk animation, and the
   idle animation, must resolve for every one of those facings. A failure is `NotFound`, naming
   the animation and the facing.

On success the request gets a new, process-unique `MovementRequestId`, and the actor starts
walking.
- **Replacement:** any walk in progress is replaced.
- **Already there:** if the actor is already at the resolved destination, the request completes
  immediately (`MovementRequest::completed`), and the animation changes only if the actor was
  walking.

**Movement status** (`ActorInstance::movementStatus()`):
- `Idle`: never asked to walk.
- `Moving`.
- `Completed`: the last request arrived.
- `Cancelled`: the last request was stopped.

The request id tells a waiter whether "completed" means its own request. Script-level waiting comes
in Milestone 4.4.

**Stopping and taking over** (the walk never fights an explicit request):
- `stopWalking()` stops where the actor is (`Cancelled`) and selects the idle animation.
- `setPosition()` cancels the walk and selects idle, so stale waypoints never pull the actor back.
- An explicit `setAnimation`, `setFacing` or `playAnimation` that succeeds cancels the walk. The
  explicit animation wins, and no idle is selected. One that fails changes nothing, and the walk
  goes on.

### Timing

Movement is time-based (seconds, the engine's update delta) and independent of animation ticks.
An actor moves smoothly however few frames its walk clip has.

- **Leftover time:** time left over at a waypoint carries on to the next segment, so several
  waypoints can be passed in one update.
- **Arrival:** the actor arrives exactly at the resolved destination, without overshooting.
- **Precision:** positions are computed in double from the segment start and the distance
  travelled, so rounding does not accumulate.
- **Zero and large updates:** zero-time updates do nothing. Large ones, which `Application`
  clamps to 0.25 s, simply travel further.

### Facing and animation while walking

**Facing:** an actor faces the direction of the segment it walks, using eight 45° sectors centred
on the compass directions.
- **Coordinates:** room y points down, so (0, −1) is north.
- **Sector boundaries:** a direction exactly 22.5° from a compass direction belongs to the sector
  counter-clockwise from it on screen: from east, that is north-east.
- **Zero-length segments** do not change the facing.

**Animation**, always with the atomic `setAnimation(animation, facing)`:
- **At the start:** the walk animation, in the first segment's facing.
- **At each waypoint:** the walk animation, in the new facing. The same clip keeps playing,
  mirrored or not. A different clip starts over; directional clips are not phase-synchronised.
- **On completion or cancellation:** the idle animation, keeping the last facing.

Actors with fewer directions use the existing explicit mappings, mirroring and `fallback`. The
walking actor in ActorDemo has north, south and east, with west mirrored.

## Perspective

The room's scale map (`scale_map`, [Rooms.md](Rooms.md)) gives a **perspective scale** for a
ground y. `evaluateScaleMap` (`Room/Perspective.h`) computes it:
- piecewise linear between the points, and the first or last point's scale beyond them, with no
  extrapolation;
- increasing or decreasing, since a room need not get larger towards the bottom;
- 1 without a map.

Point scales are from 0.001 to 1000.

**Scales, kept separate:**
- **Base scale:** the actor's own (`default_scale`, the placement's `scale`, `setBaseScale`). It is
  never overwritten.
- **Perspective scale:** derived from the actor's current position on every call. It is 1 when
  `perspective_scale` is false, after `setPerspectiveScaling(false)`, or in a room without a map.
  Being derived, it is never stale after a move or reposition.
- **Effective scale** = base × perspective. Both factors lie in [0.001, 1000], so the product is
  finite and never zero. Rendering uses it, around the ground point.

**Speed:** the effective walking speed is the reference speed × perspective scale at the actor's
feet, when `perspective_speed` is on.
- It is never multiplied by the base scale: a smaller actor does not walk more slowly.
- Visual perspective and speed perspective can be switched off independently.

**Integration:** the scale changes continuously along a path, and the motion is integrated exactly.
- Each segment is split where it crosses a scale-map point's y. Within a piece the scale is linear
  in the distance travelled, `s(l) = s0 + k·l`.
- The time to travel a distance L is `ln(1 + kL/s0) / (v·k)`.
- The distance covered in a time T is `s0·(exp(v·k·T) − 1) / k`. Near-constant pieces use the
  linear forms.
- Any split of time into updates gives the same position, up to rounding.

## Update and render order

There is no new loop or service. In a game's `ApplicationListener`, each frame runs in this order:

1. **`onUpdate(dt)`:** game logic and input (walk requests included), then `room->update(dt)`.
   Room changes happen here too, outside the script update. Within `update`:
   1. Every walking actor moves along its path by `dt` seconds: position, then facing and the
      walk or idle animation.
   2. The room's `AnimationClock` turns `dt` into ticks.
   3. Every actor's playback advances by those ticks, visible or not.

   An actor that arrives during an update spends that update's ticks on its idle clip. Movement
   time and animation ticks are separate, and each is advanced exactly once per update.
2. **`ScriptHost::update(dt)`:** called by `Application` after `onUpdate`. A script change to an
   actor will appear in the same frame's drawing and advance from the next update.
3. **`onRender`:** `room->drawList()`, then `drawRoom(renderer, list, textures)`.

Rendering only reads: drawing several times without an update draws the same frames.

Dialogue and other adventure systems will hook into the same `onUpdate` step later.

## Rendering

**Render state:** `ActorInstance::renderState()` gives the image, the source rectangle, the
ground position, the pivot, the scale, `flipX`, and the destination rectangle in room
coordinates. No graphics API is involved. With `w` and `h` the frame size and `px` the pivot
x-coordinate, mirrored when `flipX` is set:

```
px = flipX ? w − pivot.x : pivot.x
destination = { x − px·scale, y − pivot.y·scale, w·scale, h·scale }
```

The ground point therefore stays fixed across frames, sizes, pivots, mirroring and scale.

**Draw list:** `RoomInstance::drawList()` (`Room/RoomDrawList.h`) describes the whole room:
- **The background** first, at the room origin.
- **The other items**, back to front by ground y in one order:
  - every prop;
  - every *visible* room object that has a sprite, at its runtime position;
  - every *visible* actor, at its effective scale.
- **Ties** at equal depth go to props, then objects, then actors, each in file order, so the order
  is deterministic.

So actors are drawn in front of some props and behind others, not above everything. Invisible
actors are not in the list, but they keep animating. Props and objects are placed with their pivot
on their position, using the image size from its header (`LoadedRoom::imageSizes`).

**`Source/RoomGraphics`** (library `schuimm_room_graphics`) is the only place where rooms meet
graphics:
- **`RoomTextures::prepare(textureCache, loadedRoom)`** creates, through the engine's
  `TextureCache`, one texture for the background, each prop and object sprite, and each image any
  of the room's actor clips uses. Images shared by
  actors, clips or rooms get a single texture. It also checks each frame against its texture's
  real size. This happens once per room load; playback never decodes images or creates textures.
  Afterwards the decoded images may be unloaded. Destroying a room or its `RoomTextures` never
  invalidates a texture still in use elsewhere.
- **`drawRoom(renderer, list, textures, layer)`** submits the background, then one `Sprite` per
  item in list order, on one layer. `Sprite::flipX` mirrors the texture inside the destination
  rectangle.

## Scripts

Room scripts define the room and its objects. Actor scripts will define actor behaviour
independently of the room an actor is in, and a room script never handles an actor's events.

**What exists now:**
- An actor resource's `script` reference is checked to exist and is kept on the resource
  (`ActorResource::script`). It is not loaded or executed.
- Room-script linking is unchanged: an `object` declaration must match a room *object*. An actor
  placement with the same id does not satisfy it.

**Planned:**
- **Dispatch:** actor events will be dispatched to the actor's own script, with designer-defined
  event names. A missing handler means no action. An existing handler with the wrong signature
  is an error.
- **Coroutine ownership:**
  - coroutines started by a room or for its objects end with the room (already the case);
  - actor handler coroutines will end with their actor, which here means with its room;
  - global coroutines with the game session.
  - Holding a handle does not imply ownership.
- **Language:** the syntax for actor handlers, the implicit actor context and global state belongs
  to the adventure-scripting milestone. Until then there are no actor-specific script types or
  functions, and the language and bytecode are unchanged.

## Not included

- **Movement extras:** collision between actors, avoidance, clearance, acceleration, root motion,
  phase-synchronised direction changes, interaction hit-testing.
- **Story and game flow:** inventory, dialogue, lip sync, room transitions.
- **Persistence:** persistent or off-screen actors, actor managers or registries, cross-room
  validation, save and load.
- **Scripting:** actor script execution and any script-facing actor functions.
- **Animation:** events, blending, state machines, composite or skeletal actors.

## Demo and tests

**ActorDemo** (`Applications/ActorDemo`) shows three rooms:
- the courtyard, with click-to-walk, perspective and a debug overlay;
- the hall and the garden, which share the walker and lamp resources. [ManualTesting.md](ManualTesting.md#actordemo) lists its keys. `ActorDemoSmoke` runs
its automatic sequence.

**Tests:**
- `Tests/Room/ActorTests.cpp`:
  - the actor format and the resolution order;
  - instance state and failed changes;
  - mirroring, pivots and scale;
  - handles and lookups;
  - placements and their validation;
  - destroying and re-creating rooms;
  - resource reuse and all-or-nothing creation;
  - object linking;
  - updating and draw-list order;
  - that the demo's rooms are valid.
- `Tests/Room/MovementTests.cpp`:
  - facing sectors;
  - walking: timing, waypoints, exact arrival, update-rate independence;
  - cancelling, replacing, failed requests, nearest-reachable destinations, repositioning;
  - animation ownership, the walk and idle clips along a path, mirroring, missing animations;
  - perspective (scale maps, base and perspective scale, perspective speed);
  - room drawing order.
- `Tests/Graphics/RoomGraphicsTests.cpp`: textures and sprite submission on a fake renderer.
