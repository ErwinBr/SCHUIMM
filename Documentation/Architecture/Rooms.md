# Rooms: room resources and room instances

A room is the place an adventure scene happens: a background, decoration, interactive objects,
walkable floor, named areas and, optionally, a compiled script that says what the objects do.
This document describes the room resource file format (versions 1 and 2) and how rooms are
loaded, validated, shared and instantiated (`Source/Room`, library `schuimm_room`). Actors and
their placements in rooms (version 2) are described in [Actors.md](Actors.md).

Room resources are written by hand for now. A future Development Kit will author rooms visually
and generate these files. The format belongs to the runtime, not to the editor: the
Development Kit is one producer of it, and the engine does not depend on it.

**Not implemented yet:**
- rendering backgrounds, props and objects (only actors are drawn, see
  [Actors.md](Actors.md#rendering));
- interaction detection;
- movement and automatic scaling, pathfinding;
- inventory, dialogue, room transitions;
- saving game state.

Room resources keep the authored data those systems will use.

## Three things, kept apart

| | What it is | Mutable | Shared |
|---|---|---|---|
| `RoomResource` (`Room/RoomResource.h`) | the authored room as read from `*.room.json`: plain data | no | by every instance of the room |
| `script::Program` | the room's compiled script (`*.schb`), loaded by the existing `ScriptHost` | no | by every instance (and every room using the same script) |
| `RoomInstance` (`Room/RoomInstance.h`) | a live room: runtime objects with their handles and state, and the running coroutines it started | yes | no: each instance is independent |

A `RoomResource` never changes after loading. Gameplay changes the `RoomInstance`. The future
save system will store instance state separately. It never rewrites the resource or the JSON.

## The room resource format

A room resource is a UTF-8 JSON file, by convention named `<Room>.room.json`. A complete
example is [`Rooms/Examples/Kitchen.room.json`](../Rooms/Examples/Kitchen.room.json) with its
script [`Kitchen.sch`](../Rooms/Examples/Kitchen.sch). Both are loaded by the tests.

```json
{
    "version": 1,
    "id": "kitchen",
    "size": { "width": 2560, "height": 1440 },
    "script": "Rooms/Kitchen/Kitchen.schb",
    "background": { "image": "Rooms/Kitchen/Assets/background.png" },
    "props": [
        { "id": "table", "sprite": "Rooms/Kitchen/Assets/table.png", "position": [800, 500], "pivot": [0, 0] }
    ],
    "objects": [
        { "id": "blender", "sprite": "Rooms/Kitchen/Assets/blender.png", "position": [420, 310], "pivot": [24, 60] },
        { "id": "cupboard", "sprite": "Rooms/Kitchen/Assets/cupboard.png", "position": [720, 280] },
        { "id": "window", "position": [1800, 200] }
    ],
    "scale_map": {
        "type": "linear",
        "points": [ { "y": 200, "scale": 0.55 }, { "y": 1100, "scale": 1.0 } ]
    },
    "navigation": {
        "walkable_areas": [
            { "id": "floor", "points": [[100, 800], [2400, 800], [2400, 1350], [100, 1350]] }
        ]
    },
    "regions": [
        { "id": "kitchen_exit", "type": "rectangle", "position": [2300, 700], "size": [200, 400] },
        { "id": "sink_area", "type": "polygon", "points": [[1200, 820], [1500, 820], [1450, 950], [1250, 950]] }
    ]
}
```

### Properties

Required properties are marked **required**. Every other property is optional, and an optional
property may be left out or set to `null` (the two mean the same).

| Property | Type | Rules |
|---|---|---|
| `version` | integer, **required** | `1` or `2`; checked before anything else |
| `id` | identifier, **required** | the room's name in the game |
| `size` | `{ "width", "height" }`, **required** | integers from 1 to 65536: the room's size in pixels |
| `script` | asset path | the compiled room script; must end in `.schb` |
| `background` | `{ "image": asset path }` | the background image, drawn at the room origin |
| `props` | array of props | non-interactive images (see below) |
| `objects` | array of objects | interactive objects (see below) |
| `scale_map` | scale map | character scale by ground position (see below) |
| `navigation` | `{ "walkable_areas": [...] }` | walkable polygons (see below) |
| `regions` | array of regions | named geometry (see below) |
| `actors` | array of actor placements | version 2 only: where and how actors appear ([Actors.md](Actors.md#placements-room-format-version-2)) |

**Props** (`props[]`): `{ "id", "sprite", "position", "pivot" }`.
- `id`: identifier, required.
- `sprite`: asset path, required.
- `position`: point, required.
- `pivot`: point, default `[0, 0]`.

**Objects** (`objects[]`): `{ "id", "sprite", "position", "pivot" }`.
- `id`: identifier, required. This is the name a room script declares as `object <id>:`.
- `sprite`: asset path, optional. An object without a sprite is an invisible object, such as a hotspot on the background. Its interaction area comes with the interaction milestone.
- `position` and `pivot`: as for props.
- An object declares **no** events, handler names or capabilities. Which events an object reacts to is decided by the room script alone (see [Script association](#script-association)).

**Scale map** (`scale_map`): `{ "type": "linear", "points": [{ "y", "scale" }, ...] }`.
- `linear` is the only type.
- At least two points.
- `y` is a number within the room (0 to `size.height`) and strictly increasing from point to point.
- `scale` is a finite number greater than 0.
- The future runtime will interpolate linearly between the points using a character's ground position (the y of its feet). This milestone only stores the points.

**Walkable areas** (`navigation.walkable_areas[]`): `{ "id", "points" }`.
- `id`: identifier, required.
- `points`: a polygon, required.

**Regions** (`regions[]`): named areas whose behaviour scripts and future systems decide. The format declares none.
- `{ "id", "type": "rectangle", "position", "size" }`: `position` is the top-left corner, and `size` is `[width, height]` with both greater than 0.
- `{ "id", "type": "polygon", "points" }`.

**Polygons**:
- an array of at least 3 points;
- no point equal to the previous one;
- the last point not equal to the first: polygons close implicitly;
- an area that is not zero, so not all points lie on one line;
- either winding order is accepted.

Self-intersection is not checked in version 1.

**Value types**:
- **Identifier**: a string matching `[A-Za-z_][A-Za-z0-9_]*`, the script identifier rule, so an object id can be named in `object <id>:`.
- **Point**: `[x, y]`, two finite numbers, at most 1,048,576 (2²⁰) pixels from the origin. Coordinates are stored as `float`, which has no sub-pixel precision beyond that.
- **Asset path**: see [Asset paths](#asset-paths).

**Identifier uniqueness**:
- Props and objects share one namespace, since both are things in the room that later systems and scripts may name. So a prop and an object cannot both be called `table`.
- Walkable areas have their own namespace, and so do regions and actor placements (an actor may share an id with an object: they are different kinds, looked up separately).
- Room ids should be unique within a game. A single file cannot check that, so it is the job of the game's room list (and later the Development Kit).

**Unknown properties are errors**, at every level. An unknown property is almost always a typo (`"sprtie"`), and silently ignoring it would hide the mistake. Duplicate keys in a JSON object are errors too.

### Coordinates and pivots

Room space follows the engine's convention ([Graphics.md](Graphics.md)):
- units are pixels, with 1 room pixel being 1 world unit;
- the origin is at the top-left corner of the room;
- x points right and y points down.

The room covers `[0, width] × [0, height]`.

A **pivot** is an offset in sprite pixels from the sprite's top-left corner. `position` is where the pivot lands in the room. With the default pivot `[0, 0]`, `position` is the sprite's top-left corner.

For an object that stands on the floor, put the pivot at its base, for example `[24, 60]` for a 48 × 60 sprite. Its position is then its ground point, which is also what the scale map and walkable areas use.

### Asset paths

Every file reference (`script`, `background.image`, every `sprite`) is an asset path relative to the **asset root**, never to the JSON file. It follows the rules of [`AssetPath`](Resources.md#asset-paths-assetpath):
- `/` is the separator; `\`, drive letters and URLs are rejected;
- a leading `/` and `..` that leaves the root are rejected;
- paths are normalised (`Rooms/./Kitchen//a.png` becomes `Rooms/Kitchen/a.png`);
- paths are case-sensitive on every platform.

Paths are validated while parsing. Whether the files exist is checked in a separate step (see below).

### Versioning

`version` is the format version, and this engine reads versions 1 and 2.
- **Version 2** added `actors`. Everything else is unchanged, and version 1 files load exactly as before. A version 1 file with `actors` is rejected, as an unknown property.
- A file with any other version is rejected with a message naming the version.
- Adding a property, a region type or a scale-map type means a new version. Older engines then reject newer files clearly instead of misreading them.
- Newer versions keep reading older files.

Planned additions include:
- foreground layers;
- object interaction areas (polygons);
- walk-to points and facing directions;
- exits linking rooms;
- layer and draw-order information.

### Errors

Loading stops at the first error. The message names the file and the property path, in the engine's usual `Error` form:

```text
InvalidData: Rooms/Kitchen/Kitchen.room.json: objects[1].id: duplicate identifier 'blender' (first used by objects[0])
InvalidData: Rooms/Kitchen/Kitchen.room.json: version: unsupported room resource version 3 (this engine reads versions 1 to 2)
InvalidData: Rooms/Kitchen/Kitchen.room.json: navigation.walkable_areas[0].points[3]: repeats the first point (...)
InvalidData: Rooms/Kitchen/Kitchen.room.json: invalid JSON at line 3, column 11: syntax error while parsing value - ...
NotFound: Rooms/Kitchen/Kitchen.room.json: objects[0].sprite: asset 'Rooms/Kitchen/Assets/blender.png' not found (...)
InvalidData: Rooms/Kitchen/Kitchen.room.json: script 'Rooms/Kitchen/Kitchen.schb' declares object 'blendr', which room 'kitchen' does not define
```

Nothing is substituted and nothing is ignored: a missing asset or malformed data fails the load.

## Loading

The steps are separate functions, so each can be used and tested on its own:

1. **`decodeRoomResource(bytes, sourceName)`** (`Room/RoomResourceLoader.h`) parses and validates the JSON into a `RoomResource`. It does not access the disk.
2. **`verifyRoomAssets(room, locator, sourceName)`** checks that the script, the background, every sprite and every actor resource exist as files below the asset root (`AssetLocator::info`).
   - Images are not decoded and nothing is uploaded to the GPU.
   - Textures are loaded later by the renderer through the existing `TextureCache`, from the same asset paths.
3. **`loadRoomResource(locator, path)`** does steps 1 and 2 after reading the file through the `AssetLocator` (at most 16 MiB). It is the loader of `ResourceCache<RoomResource>`.
4. **`loadRoomScript(host, locator, room, sourceName)`** (`Room/RoomScript.h`) reads the `.schb` through the locator and loads it with `ScriptHost::load`, using the existing bytecode validation and native linking. It then runs `validateRoomScript`.
   - Room scripts are compiled at build time with `schuimmc`. The runtime never compiles source and never links the compiler.
   - The room JSON references the compiled file and never contains script source or bytecode.
5. **`RoomLibrary::load(path)`** (`Room/RoomLibrary.h`) runs all of the above and caches the results:
   - the resource in a `ResourceCache<RoomResource>`;
   - the program keyed by its script's asset path, so rooms that share a script share one `Program`.

   - the actor resources of the placements in its `ActorLibrary`, together with their animation resources and image headers (`RoomLibrary::actors()`).

   It returns a `LoadedRoom { resource, script, actors }`, which is validated and cheap to copy.
6. **`RoomInstance::create(loadedRoom, host)`** creates a live instance.

```cpp
AssetLocator& assets = ...;
script::ScriptHost& scripts = ...;            // register the game's natives first
room::RoomLibrary rooms(assets, scripts);

auto kitchen = rooms.load(AssetPath::parse("Rooms/Kitchen/Kitchen.room.json").value());
if (!kitchen) { log(describe(kitchen.error())); return; }
auto instance = room::RoomInstance::create(*kitchen, scripts);
...
(*instance)->dispatch("blender", "look_at"); // the interaction system will call this
```

Because programs are linked when they are loaded, register the game's natives on the
`ScriptHost` before loading rooms. A script that calls a missing native fails to load.

## Script association

A room has at most one compiled script, named by `script`. Its `object` declarations are matched
against the room's objects by identifier:

| Case | Result |
|---|---|
| room object `blender` and `object blender:` in the script | matched: the handlers run with that object's handle as `self` |
| room object without a declaration (`window` in the example) | valid: the object has no script handlers, and its events do nothing |
| `object blendr:` without a room object of that id | **error** when loading: almost always a typo |
| duplicate object ids in the resource | error (resource validation) |
| duplicate `object` declarations in the script | rejected by the compiler and the bytecode validator |

The check (`validateRoomScript`) belongs to the room layer: the compiler knows nothing about
room resources. `RoomInstance::create` repeats it, so a mismatched resource and program cannot
be combined by hand.

## Room instances and object identity

`RoomInstance::create` creates one `RoomObject` per object definition, in resource order. Each
`RoomObject` has:
- an identity that is fixed at creation and read-only:
  - `definition()`, its definition in the shared resource;
  - `handle()`, its `script::ObjectRef`;
  - `id()`;
- mutable state:
  - `position`, initialised from the definition;
  - `visible`;
  - `flags`, a name → bool map.

`RoomObject` cannot be assigned, so its identity cannot be overwritten, not even through
`objects()`. Copying one out gives a snapshot that does not affect the room.

The state is the instance's own: changing it never changes the resource or any other instance. The live state is deliberately small, since later milestones add what their systems need. Scripts cannot reach it yet; natives over room state come with the interaction milestone. Look objects up with `object(id)` or `object(handle)`.

**Handles.** Every runtime object gets an `ObjectRef` from a process-wide 64-bit counter. Handles are therefore:
- never null;
- unique across all instances and rooms;
- never reused during the process.

This is exactly the contract of `ObjectRef` (`Script/Runtime/Value.h`, [Runtime.md](../Scripting/Runtime.md#object-handlers-and-dispatch)), and it is why no generation-checked handles are needed. A handle outlives its object only as a stale value that refers to nothing:
- `object(staleHandle)` returns `nullptr` on every instance;
- future natives will report an error for it, so a suspended script can never reach a different object through an old handle.

Two instances of the same room have different handles for "the same" object, and the handles belong to the instance that created them.

**Dispatch.** `dispatch(objectId, event, arguments)` is the integration point for the future interaction system:
- it looks up the object, then calls `ScriptHost::dispatch` with the room's program, the object id, the event name and the object's handle as `self`;
- an unknown object id returns `NotFound`;
- a room without a script, or a script without that handler, returns `{handled = false}`, which is not an error.

`start(function, arguments)` runs a room-level function such as `enter`. Nothing is started automatically: when `enter` runs is up to the future room-transition code. Event names are never fixed anywhere.

## Ownership and lifetime

- **`RoomLibrary`**
  - Owns its caches.
  - `unloadAll()` drops the library's references, but instances keep the resource and program they use through `shared_ptr`.
  - The `AssetLocator` and `ScriptHost` must outlive it.
- **`RoomInstance`**
  - Holds `shared_ptr`s to its resource and program, so neither disappears while it exists. Running coroutines also keep their program alive.
  - Its objects point into its resource, which it keeps alive.
  - The `ScriptHost` must outlive every instance.
- **Coroutines on destruction**
  - An instance remembers the coroutines it started through `dispatch` and `start`, forgetting ended ones as it goes.
  - Its destructor cancels the ones still running or waiting, so no handler of a destroyed room resumes later. Other instances' coroutines are unaffected.
- **Destroy rooms between frames**
  - A coroutine cannot be cancelled while it is the one running, so an instance must not be destroyed while the `ScriptHost` updates (from inside a native or a script event callback).
  - This is checked in every build (`SCHUIMM_VERIFY`, not only in debug builds): violating it stops the program. The future room-transition code will request an unload and perform it after the update.
- **Not covered yet**
  - Coroutines started by other code that hold one of the room's handles are not cancelled; their handles are simply stale.
  - Cross-room handle lookup (mapping any `ObjectRef` to its live instance, which natives over object state will need) comes with those natives.
- **Large data** (polygons, object lists) lives once in the shared resource, and instances copy only their small mutable state.

## Tests

`Tests/Room/RoomTests.cpp` (`RoomTests`, label `unit`) covers:
- **Parsing:** a minimal room, a full room and `null` optional properties.
- **Rejection, each with its property path:**
  - every required property; unsupported versions; invalid dimensions;
  - invalid and duplicate identifiers, including the shared prop/object namespace;
  - every scale-map and polygon rule; region shapes;
  - invalid asset paths; malformed JSON with line and column; duplicate keys; unknown properties.
- **Assets:** missing-asset checks without image decoding.
- **Scripts:**
  - rooms with and without a script;
  - matched and unmatched object declarations; a script object without a room object;
  - missing, invalid, corrupt and unlinkable bytecode;
  - sharing of resources and programs.
- **Instances:**
  - independent handles and state;
  - `self` in dispatched handlers;
  - cancellation of a destroyed instance's coroutines.
- **Examples:** the example files in `Documentation/Rooms/Examples`.

The test scripts are compiled in-process into a scratch asset root, the only place the compiler is involved.
