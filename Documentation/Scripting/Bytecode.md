# Bytecode format (.schb), version 1.0

Compiled scripts are stored in a platform-independent binary format. It is written and read
**byte by byte**: no C++ structure is ever serialised, so the format does not depend on
padding, alignment, endianness, compiler ABI or pointer size. Implementation:
`Source/Script/Bytecode` (`BytecodeWriter`, `BytecodeReader`, `Disassembler`; constants in
`BytecodeFormat.h`, instruction set in `Opcode.h`).

## Versions

| Version | Meaning | Where |
|---|---|---|
| Format `major.minor` (1.0) | binary layout and instruction set | header bytes 4–7 |
| Language `major.minor` (1.0) | source language the compiler accepted ([Language.md](Language.md)) | header bytes 8–11 |
| Compiler version (0.1.0) | informational: `major << 16 \| minor << 8 \| patch` | header bytes 12–15 |

The runtime accepts format `1.x` with `x` ≤ its own minor version and language `1.x` likewise.
A newer minor may add opcodes or sections, so it is rejected rather than misread. The language
version changes when the meaning of source code changes; the format version when the encoding
changes. They are independent.

## Layout

All integers are little-endian. Offsets are from the start of the file.

### Header (32 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `"SCHB"` |
| 4 | 2 | format major (1) |
| 6 | 2 | format minor (0) |
| 8 | 2 | language major (1) |
| 10 | 2 | language minor (0) |
| 12 | 4 | compiler version |
| 16 | 4 | flags (must be 0) |
| 20 | 4 | file size in bytes (must equal the actual size; at most 64 MiB) |
| 24 | 4 | section count (6) |
| 28 | 4 | CRC-32 (IEEE, as zlib) of bytes 32 … end |

### Section table (6 × 12 bytes, at offset 32)

Each entry: `id u32, offset u32, size u32`. Each id 1–6 must occur exactly once; sections must
lie after the table, inside the file, and must not overlap. Every section must be consumed
exactly (no trailing bytes).

| Id | Section | Content |
|---|---|---|
| 1 | Strings | `count u32`, then per string `length u32, bytes` (UTF-8, validated) |
| 2 | Constants | `count u32`, then per constant `type u8` and `i64` (int, type 2), `f64` bits (float, type 3) or `string index u32` (string, type 4) |
| 3 | Imports | `count u32`, then per native `name u32, return type u8, parameter count u8, parameter types u8…` |
| 4 | Functions | `count u32`, then per function `name u32, return type u8, parameter count u8, parameter types u8…, register count u16, code offset u32, code length u32, debug offset u32` |
| 5 | Code | `count u32`, then `count` 8-byte instructions; each function owns `[code offset, code offset + code length)` |
| 6 | Debug | `source name u32` (string index), `count u32`, then per instruction `line u32, column u32` |

Type bytes: 0 None, 1 bool, 2 int, 3 float, 4 string, 5 any (native parameters only).

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
None. Calls pass arguments in a window of consecutive registers starting at `c`.

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
* imports and functions: names exist and are unique, types are valid (no None parameters, `any`
  only for native parameters, never as a return type), at most 255 parameters, at least as many
  registers as parameters, code and debug ranges (overflow-checked) inside their sections, at
  least one instruction;
* every instruction: known opcode, reserved byte 0, unused operands 0, register operands below
  the function's register count, constant/function/import indices exist, branch targets inside the
  function, argument windows (`c + parameter count`) inside the register file, result and return
  operands consistent with the callee's or function's return type;
* the last instruction of every function is a terminator (`Jump`, `Return`, `Unreachable`), so
  execution cannot run past the end.

Errors are `ErrorCode::InvalidData` (malformed) or `ErrorCode::Unsupported` (newer version) with
a message naming the problem and, where it applies, the byte offset or function and instruction:

```text
InvalidData: Invalid bytecode: function 'main' instruction 4 (Move): register 12 in a out of range (12 registers)
```

What validation does *not* prove is that registers hold the right *types* (that would need a
data-flow verifier). Instead every VM instruction checks the type tags of its operands and fails
with a controlled `InvalidBytecode` runtime error on a mismatch — memory safety never depends on
the bytecode being well-typed. The tests flip bits at every position of a file (with a corrected
checksum), load the result and run whatever validates, under AddressSanitizer and
UndefinedBehaviorSanitizer.

## Disassembler

`disassemble(module)` (or `schuimmc --disasm`):

```text
; SCHUIMM bytecode 1.0, language 1.0, source "counter.sch"
constants:
  k0 string "Welcome to SCHUIMM!"
  k1 int 0
  ...
natives:
  n0 print(any) -> None
  n1 wait(float) -> None

function f0 main() -> None  ; 8 registers
     0  LoadConst r0, k0 ("Welcome to SCHUIMM!")  ; line 3
     1  CallNative -, n0 (print), args r0
     2  LoadConst r1, k1 (0)                      ; line 5
     ...
```
