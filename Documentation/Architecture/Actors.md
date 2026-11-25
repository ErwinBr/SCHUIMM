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
| `version` | integer, **required** | must be `1` |
| `id` | identifier, **required** | names the actor *definition*, not an instance |
| `script` | asset path | a compiled script (`.schb`); it must exist. See [Scripts](#scripts). |
| `animation_resources` | object, **required** | at least one; a local name (identifier) maps to an `*.anim.json` asset |
| `animations` | object, **required** | at least one; logical animation name (identifier) to its facing mappings |
| `default_animation` | identifier, **required** | must be one of `animations` |
| `default_facing` | facing, **required** | the default animation must resolve with it |
| `default_scale` | number | default 1. Positive and at most 1000; it must still be positive after conversion to float. |

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
  - `setScale` requires a positive, finite value of at most 1000.
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
- The **scale** is uniform and scales the artwork around the ground point. The room's scale map
  is not applied automatically yet; that comes with movement.

**Handles:**
- Room objects and actors take handles from one process-wide counter
  (`Room/ObjectHandles.h`), so a handle never collides with another object's or actor's, and is
  never reused.
- A handle is valid only while its actor exists. When the room is destroyed, its handles become
  stale.
- Instantiating the room again gives new handles, so an old handle never finds a new actor,
  even one with the same id and resource.

## Actors in rooms

### Placements (room format version 2)

Room resources gained an optional `actors` array in **version 2** of the room format
([Rooms.md](Rooms.md#versioning)). Version 1 files load exactly as before.

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
| `scale` | number | default: the resource's `default_scale`. Positive, at most 1000, positive as a float. |
| `visible` | boolean | default `true` |

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

## Update and render order

There is no new loop or service. In a game's `ApplicationListener`, each frame runs in this order:

1. **`onUpdate(dt)`:** game logic and input, then `room->update(dt)`. The room's `AnimationClock`
   turns `dt` into ticks and advances every actor by the same number, visible or not. Room
   changes happen here too, outside the script update.
2. **`ScriptHost::update(dt)`:** called by `Application` after `onUpdate`. A script change to an
   actor will appear in the same frame's drawing and advance from the next update.
3. **`onRender`:** `room->drawList()`, then `drawRoom(renderer, list, textures)`.

Rendering only reads: drawing several times without an update draws the same frames.

Movement, navigation and dialogue will hook into the same `onUpdate` step later.

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

**Draw list:** `RoomInstance::drawList()` holds the *visible* actors, sorted back to front by
ground y. Placement order breaks ties, so the order is deterministic. Invisible actors are not
in it, but they keep animating. The list type (`RoomDrawList`) is meant to hold room objects and
props later, so that everything sorts together. Actors are not drawn above everything by design.

**`Source/RoomGraphics`** (library `schuimm_room_graphics`) is the only place where rooms meet
graphics:
- **`RoomTextures::prepare(textureCache, loadedRoom)`** creates, through the engine's
  `TextureCache`, one texture for each image any of the room's actor clips uses. Images shared by
  actors, clips or rooms get a single texture. It also checks each frame against its texture's
  real size. This happens once per room load; playback never decodes images or creates textures.
  Afterwards the decoded images may be unloaded. Destroying a room or its `RoomTextures` never
  invalidates a texture still in use elsewhere.
- **`drawRoom(renderer, list, textures, layer)`** submits one `Sprite` per item, in list order, on
  one layer. `Sprite::flipX` mirrors the texture inside the destination rectangle.

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

- **Movement and navigation:** walking, pathfinding, navigation meshes, automatic scale-map
  scaling, collision, hit-testing and interaction.
- **Story and game flow:** inventory, dialogue, lip sync, room transitions.
- **Persistence:** persistent or off-screen actors, actor managers or registries, cross-room
  validation, save and load.
- **Scripting:** actor script execution and any script-facing actor functions.
- **Animation:** events, blending, state machines, composite or skeletal actors.

## Demo and tests

**ActorDemo** (`Applications/ActorDemo`) shows two rooms that share the walker and lamp
resources. [ManualTesting.md](ManualTesting.md#actordemo) lists its keys. `ActorDemoSmoke` runs
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
- `Tests/Graphics/RoomGraphicsTests.cpp`: textures and sprite submission on a fake renderer.
