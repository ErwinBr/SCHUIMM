# Resource formats and compatibility

SCHUIMM's authored resource formats are JSON:
- rooms ([Rooms.md](Rooms.md));
- actors ([Actors.md](Actors.md));
- animations ([Animation.md](Animation.md)).

All three are read with the shared decoder in `Resource/Json` ([Resources.md](Resources.md#json-resource-formats-resourcejson)).
Compiled scripts are a generated binary format ([Bytecode.md](../Scripting/Bytecode.md)).

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
