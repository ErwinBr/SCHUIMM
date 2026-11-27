# Bytecode format (.schb), version 3.0

Compiled scripts are stored in a platform-independent binary format. It is written and read
**byte by byte**: no C++ structure is ever serialised, so the format does not depend on
padding, alignment, endianness, compiler ABI or pointer size. Implementation:
`Source/Script/Bytecode` (`BytecodeWriter`, `BytecodeReader`, `Disassembler`; constants in
`BytecodeFormat.h`, instruction set in `Opcode.h`).

## Versions

| Version | Meaning | Where |
|---|---|---|
| Format `major.minor` (3.0) | binary layout and instruction set | header bytes 4–7 |
| Language `major.minor` (3.0) | source language the compiler accepted ([Language.md](Language.md)) | header bytes 8–11 |
| Compiler version (0.4.0) | informational: `major << 16 \| minor << 8 \| patch` | header bytes 12–15 |

The runtime accepts format `3.x` with `x` ≤ its own minor version and language `3.x` likewise.
A newer minor may add opcodes or sections, so it is rejected rather than misread. The language
version changes when the meaning of source code changes; the format version when the encoding
changes. They are independent.

| Format | Changes |
|---|---|
| 1.0 | initial format: 6 sections |
| 1.1 | section 7 (Objects), type `object`, opcodes `EqObject`/`NeObject` |
| 2.0 | typed registers; section 8 (Enums); types `actor`, `entity`, `vec2`, enums, `coroutine`, `operation`; enum, vector and reference opcodes (`EqRef`/`NeRef` replace `EqObject`/`NeObject`); `Spawn` |
| 3.0 | one module for the scripts of a whole game: section 9 (Sources), a source per function, section 7 Behaviours (room, actor and object declarations) replacing Objects; the Debug section no longer holds the source name |

The compiler always writes the current version. Format 3.0 changed the encoding of functions
and sections, so **1.x and 2.x files are rejected** ("recompile the script from its source");
they are never reinterpreted. Language 2.0 added `var`, entity types, enums, `vec2` and native
interfaces; language 3.0 compiles several source files into one program.

## Layout

All integers are little-endian. Offsets are from the start of the file.

### Header (32 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `"SCHB"` |
| 4 | 2 | format major (2) |
| 6 | 2 | format minor (0) |
| 8 | 2 | language major (2) |
| 10 | 2 | language minor (0) |
| 12 | 4 | compiler version |
| 16 | 4 | flags (must be 0) |
| 20 | 4 | file size in bytes (must equal the actual size; at most 64 MiB) |
| 24 | 4 | section count (9) |
| 28 | 4 | CRC-32 (IEEE, as zlib) of bytes 32 … end |

### Section table (9 × 12 bytes, at offset 32)

Each entry: `id u32, offset u32, size u32`. Each id 1–9 must occur exactly once; sections must lie after the table, inside the file, and must not overlap. Every section must be consumed
exactly (no trailing bytes).

| Id | Section | Content |
|---|---|---|
| 1 | Strings | `count u32`, then per string `length u32, bytes` (UTF-8, validated) |
| 2 | Constants | `count u32`, then per constant `type u8` and `i64` (int, type 2), `f64` bits (float, type 3) or `string index u32` (string, type 4) |
| 3 | Imports | `count u32`, then per native `name u32, return type, parameter count u8, parameter types…` |
| 4 | Functions | `count u32`, then per function `name u32, source u32, return type, parameter count u8, parameter types…, register count u16, register types…, code offset u32, code length u32, debug offset u32` |
| 5 | Code | `count u32`, then `count` 8-byte instructions; each function owns `[code offset, code offset + code length)` |
| 6 | Debug | `count u32`, then per instruction `line u32, column u32` |
| 7 | Behaviours | `count u32`, then per behaviour `kind u8, source u32, name u32, handler count u32`, then per handler `name u32, function u32` |
| 8 | Enums | `count u32`, then per enum `name u32, qualifier u32, member count u32, member names u32…` |
| 9 | Sources | `count u32`, then per source file `path u32, role u8` |

### Sources

A module holds the scripts of a game (or a single script compiled on its own). The Sources
section lists the files it was compiled from: the path (the identity resources use to refer to a
script, e.g. `Rooms/Kitchen/Kitchen.sch`) and the role, 0 global, 1 room, 2 actor
([Language.md §1](Language.md#1-source-files)). Every function names its source, for errors and
debug information; `0xFFFFFFFF` marks a compiler-generated function (`spawn:walk_to`). A module
has at least one source, and paths are unique.

A **type** is a kind byte, followed by `enum index u16` for kind 10 (an index into the Enums
section). Kinds: 0 None, 1 bool, 2 int, 3 float, 4 string, 5 any (native parameters only),
6 object, 7 actor, 8 entity, 9 vec2, 10 enum, 11 coroutine, 12 operation.

### Enums

The Enums section lists the enum types the module uses, script enums and engine enums alike.
Values of an enum are its member ordinals, `0 … member count − 1`.
- **Script enums** have qualifier `0xFFFFFFFF`.
- **Engine enums** (declared by a native interface) have the interface name as qualifier, for
  example `adventure` for `adventure.Direction`.

`Program::link` looks up every engine enum by its qualified name in the engine's registry and
requires the same members in the same order. Ordinals therefore mean the same in the script and
the engine; a script compiled against another version of the enum is rejected and must be
recompiled.

### Typed registers

Every function lists the type of each of its registers. A register holds values of one type for
the whole function; parameters occupy registers `0 … n−1` with exactly the parameter types. No
register has type `any` or None (`any` exists only for native parameters).

The validator checks every instruction against these types, so a well-formed module never
applies an instruction to a value of the wrong type. A register declared `entity` may hold an
actor or an object reference: `Move`, call arguments and results, and `Return` accept a value
whose type is *assignable* to the destination. Assignable means: the same type, `actor` or
`object` into `entity`, or anything but None into an `any` parameter.

### Behaviours

A behaviour is a declaration of handlers: kind 2 for an object declaration
([Language.md §8](Language.md#8-objects-and-handlers)), with the object identifier as its name;
kinds 0 (room) and 1 (actor) are unnamed (name `0xFFFFFFFF`) and are reserved for the room and
actor declarations of later language versions. Each entry gives its source and, per handler, the
event name and the index of the function implementing it. The handler is an ordinary entry of
the function table whose name is the handler name (`look_at`) and whose **parameter 0 is the
hidden `self`** (type `object`; `actor` for actor behaviours), followed by the declared
parameters. Handlers are never the target of `Call`; the runtime starts them by behaviour and
event name. Global functions are the functions no handler refers to.

### Instructions (8 bytes)

```text
byte 0   opcode
byte 1   reserved, must be 0
bytes 2-3   a  (u16)
bytes 4-5   b  (u16)
bytes 6-7   c  (u16)
```

Jump targets use `b` and `c` together as one 32-bit absolute instruction index
(`b | c << 16`) within the function. Register operand `0xFFFF` means "no register" where an
operand is optional (call results and return values of `None` functions).

Registers are per call frame. Parameters arrive in `r0…rn−1`; all other registers start as
None (the compiler never reads a register before writing it). Calls pass arguments in a window
of consecutive registers starting at `c`.

| Opcode | Operands | Effect |
|---|---|---|
| `Nop` | — | nothing |
| `LoadConst` | a, k | `ra = constant k` |
| `LoadTrue`, `LoadFalse`, `LoadNone` | a | `ra = True / False / None` |
| `Move` | a, b | `ra = rb` |
| `NegInt`, `NegFloat`, `Not` | a, b | `ra = -rb` (checked for int) / `not rb` |
| `AddInt SubInt MulInt DivInt ModInt` | a, b, c | checked int arithmetic; `DivInt`/`ModInt` are floored |
| `AddIntSat` | a, b, c | int addition clamped to the int range (range() counter) |
| `AddFloat SubFloat MulFloat` | a, b, c | IEEE |
| `DivFloat FloorDivFloat ModFloat` | a, b, c | division by zero is an error |
| `Concat` | a, b, c | string concatenation |
| `EqInt … GeInt`, `EqFloat … GeFloat`, `EqBool NeBool`, `EqString … GeString` | a, b, c | `ra = rb op rc` (bool) |
| `IntToFloat FloatToInt BoolToInt IntToString FloatToString BoolToString` | a, b | conversions |
| `CheckRangeStep` | a | error if `ra == 0` |
| `Jump` | target | unconditional |
| `JumpIfTrue`, `JumpIfFalse` | a, target | conditional on bool `ra` |
| `Call` | a (opt), function, window | call a script function |
| `CallNative` | a (opt), import, window | call an engine function |
| `Return` | a (opt) | return `ra` (or nothing) |
| `Unreachable` | — | runtime error (end of a function with a result) |
| `EqRef`, `NeRef` | a, b, c | `ra = rb == rc` / `!=` for two references (`actor`, `object`, `entity`): equal when they refer to the same entity. An actor and an object are never equal. The validator rejects comparing an `actor` register with an `object` register |
| `LoadEnum` | a, enum, ordinal | `ra = member ordinal of enum` (the ordinal must exist) |
| `EqEnum`, `NeEnum` | a, b, c | equality of two values of the same enum |
| `MakeVec2` | a, b, c | `ra = vec2(rb, rc)` from two floats |
| `Vec2X`, `Vec2Y` | a, b | `ra = rb.x` / `rb.y` |
| `SetVec2X`, `SetVec2Y` | a, b | sets component x / y of the vector in `ra` to the float `rb` |
| `AddVec2`, `SubVec2` | a, b, c | componentwise |
| `MulVec2`, `DivVec2` | a, b, c | vector `rb` times / divided by float `rc`; `DivVec2` by zero is an error |
| `NegVec2` | a, b | `ra = -rb` |
| `EqVec2`, `NeVec2` | a, b, c | componentwise float equality |
| `Spawn` | a (opt), function, window | starts the function with the arguments in the window as a new coroutine; `a` (a `coroutine` register) receives its handle. The caller continues at once. A handler cannot be spawned |

The numeric opcode is the position in `SCHUIMM_SCRIPT_OPCODES` (`Opcode.h`). Opcodes are only
ever appended within a format major version.

## Validation

**Bytecode is never executed without validation.** `readBytecode()` parses and validates;
`Program::link()` validates again, so modules that come straight from the compiler in memory are
checked too. Validation never trusts a size or index:

* header: magic, versions, flags, declared size = actual size, size limit, section count, CRC-32;
* section table: known ids, no duplicates, every `offset + size` computed overflow-safely
  (`checkedAdd`) and inside the file, after the table, no overlaps;
* every element count is checked against the bytes remaining (`checkedMul`) *before* anything is
  allocated, so a forged count cannot cause a huge allocation;
* every read is bounds-checked; sections must be consumed exactly;
* strings are well-formed UTF-8 (no overlong forms, surrogates or values above U+10FFFF);
* constants have valid types, string indices exist;
* enums: names and members exist, at least one and at most 65 536 members, unique member
  names, unique enum names (engine enums per qualifier);
* imports and functions: names exist and are unique (function names among global functions;
  a handler's name only within its behaviour), types are valid (no None parameters, `any` only for
  native parameters, never as a return type, enum types refer to existing enums), at most 255
  parameters, at least as many registers as parameters, parameter registers typed like the
  parameters, no register of type `any`, code and debug ranges (overflow-checked) inside their
  sections, at least one instruction;
* sources: at least one, paths exist and are unique; every function's source exists (or is
  `0xFFFFFFFF`);
* behaviours: a known kind, an existing source of the matching role (room and object behaviours
  in room scripts, actor behaviours in actor scripts), at most one room and one actor behaviour
  per source, object names unique per source (they do not share the namespace of functions),
  handler names unique per behaviour, every handler an existing function of the same name and
  source, no function the handler of two events, and an object handler's parameter 0 of type
  `object` (an actor handler's of type `actor`);
* every instruction: known opcode, reserved byte 0, unused operands 0, register operands below
  the function's register count, constant/function/import/enum indices exist, branch targets
  inside the function, argument windows (`c + parameter count`) inside the register file, and no
  `Call` of a handler function (handlers are started only by dispatch);
* register types of every instruction: operands and results have the types the opcode requires
  (`AddInt` on int registers, `JumpIfTrue` on a bool, `LoadEnum` into a register of that enum
  with an existing ordinal, …); call arguments are assignable to the callee's parameters, and
  results and return values to their destinations;
* the last instruction of every function is a terminator (`Jump`, `Return`, `Unreachable`), so
  execution cannot run past the end.

The validator guarantees memory and execution safety plus the structural and namespace rules
above, which the runtime relies on. Purely lexical rules of the source language — identifier
syntax, reserved names such as `int`, `str` or `range` — are the compiler's and are not checked here.

Errors are `ErrorCode::InvalidData` (malformed) or `ErrorCode::Unsupported` (newer version) with
a message naming the problem and, where it applies, the byte offset or function and instruction:

```text
InvalidData: Invalid bytecode: function 'main' instruction 4 (Move): register 12 in a out of range (12 registers)
```

Static register types do not prove that a register was written before it is read: an
uninitialised register holds None. So the VM still checks the type tags of its operands and fails
with a controlled `InvalidBytecode` runtime error on a mismatch. Memory safety never depends on
the bytecode being well-typed. The tests flip bits at every position of a file (with a corrected
checksum), load the result and run whatever validates, under AddressSanitizer and
UndefinedBehaviorSanitizer.

## Disassembler

`disassemble(module)` (or `schuimmc --disasm`):

```text
; SCHUIMM bytecode 3.0, language 3.0, source "counter.sch"
constants:
  k0 string "Welcome to SCHUIMM!"
  k1 int 0
  ...
natives:
  n0 print(any) -> None
  n1 wait(float) -> None

function f0 main() -> None  ; 8 registers
  ; register types: r0:string r1:int r2:int ...
     0  LoadConst r0, k0 ("Welcome to SCHUIMM!")  ; line 3
     1  CallNative -, n0 (print), args r0
     2  LoadConst r1, k1 (0)                      ; line 5
     ...
```

Enums are listed after the natives (`e0 adventure.Direction: NORTH NORTH_EAST …`), then the
behaviours. Handler functions are shown as `object.handler` (their first parameter is `self`).
A module of several sources lists them first (`sources:`, e.g. `s0 room "Rooms/Kitchen.sch"`)
instead of the single `source` of the header line, and names the source of each behaviour and
function (`object door (s1): …`, `; 5 registers, s1`):

```text
behaviours:
  object blender: look_at -> f2 use -> f3 use_with -> f4
  object cupboard: look_at -> f5

function f4 blender.use_with(object, object) -> None  ; 7 registers
```
