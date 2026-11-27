# Resource formats and compatibility

SCHUIMM's authored resource formats are JSON:
- games ([below](#the-game-resource));
- rooms ([Rooms.md](Rooms.md));
- actors ([Actors.md](Actors.md));
- animations ([Animation.md](Animation.md)).

All of them are read with the shared decoder in `Resource/Json` ([Resources.md](Resources.md#json-resource-formats-resourcejson)).
Script sources (`.sch`) are text; the compiled game program is a generated binary format
([Bytecode.md](../Scripting/Bytecode.md)).

## The game resource

`*.game.json` (`Adventure/GameResource.h`) configures a game's scripting environment, nothing
else (no display, input, save or packaging settings):

```json
{
  "version": 1,
  "id": "kitchen_demo",
  "program": "Scripts/Game.schb",
  "global_scripts": ["Scripts/Global.sch", "Scripts/Interface.sch"],
  "rooms": ["Rooms/Kitchen/Kitchen.room.json", "Rooms/Garden/Garden.room.json"]
}
```

| Property | Type | Rules |
|---|---|---|
| `version` | integer, **required** | must be `1` |
| `id` | identifier, **required** | the game's name |
| `program` | asset path, **required** | the compiled program (`.schb`): the build writes it, the runtime loads it |
| `global_scripts` | array of asset paths | script sources (`.sch`) with game-wide declarations only; no duplicates |
| `rooms` | array of asset paths, **required** | the game's room resources (`.json`), at least one, no duplicates; the runtime enters only these |

Room scripts and actor scripts are not listed: the build finds them through the rooms (their
`script`) and their actor placements (the actor resources' `script`), each source once
([Adventure.md](Adventure.md#the-game-build)). One file serves both sides: the build reads the
sources and writes `program`; the shipped runtime reads `program` and `rooms` only and never
compiles anything.

## Pre-release resource format and compatibility policy

SCHUIMM is currently in active pre-release development. Its resource formats, scripting
language, public APIs and runtime architecture are not considered stable.

During this development phase:

- Architectural correctness, simplicity and maintainability take precedence over backwards
  compatibility.
- Authored resource schemas may change incompatibly whenever doing so improves the architecture.
- Resource format version 1 is provisional and does not represent a stable compatibility contract.
- Do not increment authored resource schema versions for internal development changes.
- Resource loaders should support only the current schema.
- Do not introduce legacy parsing branches, deprecated property aliases, compatibility adapters or
  resource migration systems.
- When a resource schema changes, update all affected repository assets, demonstrations, tests and
  documentation.
- Remove obsolete implementation paths rather than maintaining parallel old and new
  implementations.
- Existing prototype resources must not constrain architectural improvements.
- Preserve strict resource validation and useful error diagnostics.

### Generated binary formats

Compiled bytecode and other generated binary formats must retain explicit format-version
validation.

Increment their format versions when incompatible binary or execution-contract changes occur.

Reject incompatible generated artifacts and regenerate them from their source files. Do not
implement support for historical binary formats during pre-release development.

### Stable release

A formal backwards compatibility and resource migration policy will be established when SCHUIMM's
architecture and resource formats are sufficiently mature for public game development.

Until then, backwards compatibility with previous development commits is explicitly not
guaranteed.

## What this means for the loaders

Every authored format is at `"version": 1`, and a file with any other version is rejected with a
message naming the version. Schema changes before release change version 1 in place, together
with every file in the repository that uses it.
