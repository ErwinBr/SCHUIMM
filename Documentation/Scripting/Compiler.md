# Script compiler

`schuimm_script_compiler` (`Source/Script/Compiler`) turns source text into a bytecode
[module](Bytecode.md). It depends only on `schuimm_core` and `schuimm_script_bytecode`: no
platform, graphics or audio code, no file I/O, and nothing of the game runtime. The game does not
link it; scripts are compiled ahead of time with `schuimmc`.

## Pipeline

```text
SourceFile ──tokenize──► tokens ──parse──► AST ──checkTypes──► SemanticModel
                                                              │
        Module ◄──generateBytecode── IR ◄──[optimize]── IR ◄──buildIr
          │
          └──validateModule (the runtime's own validator, as a self-check)
```

`compile()` (`Compiler.h`) runs all stages and returns a `CompileResult` with every
intermediate product (tokens, AST, semantic model, IR before and after optimisation, module) for
dumps and tests. Each stage is also a separate function with its own header and tests.

| Stage | Files | Output |
|---|---|---|
| Lexer | `Lexer.h`, `Token.h` | `std::vector<Token>` with `INDENT`/`DEDENT`/`NEWLINE`, decoded literals and a `SourceRange` (file id, offset, line, column, end) per token |
| Parser | `Parser.h`, `Ast.h` | `SourceUnit`: functions, object and enum declarations, statements and expressions with source ranges. Purely syntactic: a type written as a name that is not built in stays a name |
| Semantic analysis | `TypeChecker.h`, `Types.h` | `SemanticModel`: per expression its type, implicit widening, resolved variable, call target, enum member or vector component; per function its resolved signature, variables and owning object; the objects and their handlers; the enums; the native signatures |
| IR builder | `IrBuilder.h`, `Ir.h` | `IrModule` |
| Optimizer | `Optimizer.h` | `IrModule` (optional) |
| Code generator | `CodeGen.h` | `Module` |

### Lexer

Hand-written, single pass. Indentation is measured in tabs at the start of each logical line
and compared with the current level, producing `INDENT`/`DEDENT`. Blank and comment lines are
skipped; inside parentheses line breaks and indentation are ignored. Every lexical error is
reported, the offending character is skipped, and lexing continues (an `Invalid` token stands in,
so the parser does not report it again).

### Parser

Recursive descent for declarations and statements; expressions use precedence climbing (a Pratt
parser) with the binding powers of [Language.md §4.1](Language.md#41-precedence-and-associativity).
Comparisons are parsed non-associatively.

**Error recovery:** a failed statement reports one error (further errors in the same statement
are suppressed as likely consequences), then the parser skips to the end of the logical line,
including an indented block that belongs to it, and continues with the next statement. At the
top level it resynchronises at the next `func`/`extern`/`object`/`enum`; inside an object body,
at the next `func`. One run therefore reports independent
syntax errors throughout the file. Semantic analysis runs only on syntactically correct files
(on a recovered tree it would report confusing follow-up errors).

### Semantic analysis

A separate pass over the AST, never mixed into parsing:

1. Declare the native interfaces: the core interface and those in `CompileOptions::interfaces`
   (each at most once). Their functions become natives and their enums become engine enums,
   qualified by the interface name. Two interfaces declaring the same function or enum name are
   an error.
2. Declare the script's enums. A name already used by an interface, function or other enum is
   an error, as are duplicate members and empty enums.
3. Collect all function and `extern` signatures (functions may be called before their
   declaration), resolving type names to enums. Duplicate and reserved names, and names an
   interface declares, are reported here.
4. Collect the object declarations ([Language.md §8](Language.md#8-objects-and-handlers)): object
   names (unique, not clashing with functions, natives or reserved names) and per object its
   handlers (unique within the object). Handlers are appended to the function list after the
   room-level functions, each with the index of its object; handler names are kept in the
   object's scope only, never in the room scope used to resolve calls.
5. Check every function body with a stack of block scopes:
   - `var` declarations and assignments, and whether values are assignable;
   - lookups; operator typing, including vectors, enums and references;
   - `Enum.MEMBER` and `vector.x`;
   - `spawn f(args)`: the call is checked as usual and the expression has type `coroutine`. A
     spawned native gets a compiler-generated wrapper function, `spawn:<name>`, which calls it
     and returns its result (`SemanticModel::spawnWrappers`). The wrappers come after all other
     functions; `:` keeps their names apart from every identifier;
   - `wait(x)` with an `operation` argument resolves to the hidden core native
     `await_operation`;
   - call arity and argument types; `bool` conditions;
   - return types, and "every path returns" (a function with a result whose body can fall off
     the end).

In a handler, variable 0 is the hidden parameter `self` (type `object`); the `self` expression
resolves to it, and is an error in room-level functions.

Compiler types (`Types.h`) are a `Type` enumeration with one value per built-in type. Enum types
are `Type::EnumBase + i`, where `i` indexes `SemanticModel::enums()`: engine enums first, then the
script's own.

The type checker depends only on interface *declarations* (`NativeInterface`, in the bytecode
library). The compiler is independent of the adventure layer: the adventure API reaches it as an
interface in the options.

Types of the form *Error* mark expressions whose type is unknown after an error; they are
compatible with everything, so one mistake produces one diagnostic, not a cascade.

### Diagnostics

Stages never format strings ad hoc: they report a structured `Diagnostic` (severity, phase,
source range, message, notes) to a `Diagnostics` collector. `formatDiagnostic` renders it once:

```text
scripts/main.sch:8:10: error: Undefined variable 'player_speed'.
    	print(player_speed)
    	      ^^^^^^^^^^^^
```

The location is `file:line:column`. Notes add hints where the compiler knows a likely fix, e.g.
`note: Did you mean 'True'?` for `true`, or `note: Convert the other operand with str(...)` for
`"Score: " + 5`.

## Intermediate representation

`Ir.h`. A simple, register-oriented three-address code, independent of the bytecode encoding:

* **Functions** with typed parameters (virtual registers `r0…rn−1`) and a return type.
* **Virtual registers**: one per variable (reassigned; the IR is not SSA) plus a fresh one per
  temporary. Each register has a static type.
* **Instructions**: `dest = op operands` with explicit, typed operations (`AddInt`,
  `AddFloat`, `Concat`, `LtString`, `IntToFloat`, `AddVec2`, `EqEnum`, `EqRef`, …), `Load*`
  immediates (`LoadEnum` carries the enum and ordinal), `Call` (script function), `CallNative`
  (engine function) and `CheckRangeStep`. `SetVec2X dest, v, x` produces `v` with a new x; when
  `dest` is `v` (the usual case) it becomes one in-place bytecode instruction. Each instruction
  records its source line and column.
* **Basic blocks** end in exactly one terminator: `Jump`, `Branch cond, then, else`,
  `Return [value]` or `Unreachable` (end of a function with a result that the type checker
  proved cannot be reached). Block 0 is the entry.
* Control flow is explicit: `if`/`elif`/`else`, `while`, `for` and the short-circuit operators
  all become blocks and branches.
* A table of IR operation properties says which operations may *trap* (raise a runtime error)
  and which are *removable* (no side effect, cannot fail) — the optimizer relies on it.

`dumpIr()` prints it (also `schuimmc --ir` / `--ir-opt`):

```text
func main() -> None
bb0:
  r1 = LoadInt 0
  r0 = Move r1
  Jump bb1
bb1:
  r2 = LoadInt 3
  r3 = LtInt r0, r2
  Branch r3, bb2, bb3
...
```

`verifyIr()` checks the structural rules (used after building and in tests).

Object declarations are an `IrModule::objects` table (object name, handler name → function
index). A handler is an ordinary `IrFunction` whose `object` field names its owner and whose
`r0` is `self`; `dumpIr` prints it as `func blender.use(r0: object) -> None`. The relationship is
data, not a concatenated name: `object.handler` is only produced when formatting.

## Optimizer

`Optimizer.h`. Optional (`CompileOptions::optimize`, `schuimmc -O0`), run per function until
nothing changes (at most 8 rounds). Each pass is a separate function with its own tests:

| Pass | What it does |
|---|---|
| `propagateConstants` | Sparse conditional constant propagation over the CFG (a forward data-flow analysis that ignores edges of branches on known constants). Registers known to hold one constant on every path become `Load`s of it (*propagation*); operations on constants are evaluated at compile time (*folding*); branches on constants become jumps; `range()` step checks on known non-zero steps are dropped. |
| `removeUnreachableBlocks` | Deletes blocks not reachable from the entry. |
| `simplifyControlFlow` | Jump threading through empty blocks, branches whose two targets coincide become jumps, and a block is merged into its only predecessor when that predecessor jumps to it (*removal of unnecessary jumps*). |
| `eliminateDeadCode` | Liveness analysis; removes *removable* instructions whose result is never used. |

**Behaviour is preserved:**

* Folding uses the VM's own arithmetic, conversion and formatting functions
  (`Script/Bytecode/Semantics.h`), so a folded value is bit-identical to the runtime result.
* An operation that may trap is never removed, and is folded only when its operands are known not
  to trap. `9223372036854775807 + 1`, `1 // 0` or `int(1e300)` stay in the code and fail at run
  time with the right message and line.
* Calls are never folded. Calls of script functions are never removed. Calls of natives are
  removed only when their result is unused and the native is declared pure (the core vector
  functions are).
* Enum, vector and reference operations are not folded; the pure ones may be removed when unused.
* Code generation also omits jumps to the immediately following block, with or without the
  optimizer.

Tests compare optimized and unoptimized execution of every example, the demo script and a set of
programs, including the text and location of runtime errors.

Out of scope for this milestone: inlining, register allocation beyond dense renumbering, loop
transformations, common subexpression elimination.

## Code generation

`CodeGen.h`. IR → `Module`:

* Blocks are laid out in order; branch targets are patched to instruction indices.
* Virtual registers are renumbered densely in order of first use; parameters stay in `r0…`.
  Every register keeps the static type of its virtual register, which becomes the function's
  register type table ([Bytecode.md](Bytecode.md#typed-registers)).
* Call arguments must be consecutive registers (the bytecode's argument window). If they are not,
  they are copied into a scratch window above the locals. Calls with the same argument types
  share a window, so each window register has one type.
* Only the enums the module uses are written to the Enums section, in order of first use.
* Constants and strings are deduplicated. Only natives that are actually called become imports.
* Every instruction gets its source line and column as debug information.
* Format limits (65 535 registers per function, 65 536 constants) are reported as diagnostics.
* The objects table becomes the bytecode's Objects section; handler functions keep their plain
  names in the function table.

## Command-line compiler: `schuimmc`

```bash
schuimmc [options] input.sch
```

| Option | |
|---|---|
| `-o FILE` | write the bytecode (without `-o` the file is only checked) |
| `-O0` / `-O1` | disable / enable (default) the optimizer |
| `--name NAME` | the source name stored in diagnostics and debug information |
| `--interface NAME` | let the script use the native interface `NAME` besides the core interface (repeatable) |
| `--list-interfaces` | print the interfaces the compiler knows, with their enums and signatures |
| `--tokens` | dump the token stream |
| `--ast` | dump the syntax tree |
| `--types` | dump the syntax tree with types and implicit conversions |
| `--ir`, `--ir-opt` | dump the IR before / after optimization |
| `--disasm` | dump the bytecode disassembly |
| `--verify` | re-read the written file and validate it |

Exit code 0 on success, 1 on compile errors (all diagnostics go to stderr), 2 for invalid
arguments. Example:

```bash
Build/macos-clang/bin/Debug/schuimmc Documentation/Scripting/Examples/counter.sch -o counter.schb --ir-opt --disasm
```

The runner `schuimm-run` executes bytecode headlessly; see
[Runtime.md](Runtime.md#running-scripts-headlessly-schuimm-run).
