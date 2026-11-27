# Scripting

SCHUIMM runs game logic in **SCHUIMM Script**, a small, statically typed language with
Python-inspired syntax. Scripts are compiled ahead of time into validated, platform-independent
bytecode and executed by a register VM as cooperative coroutines driven by the game loop.

Room scripts group the event handlers of room objects in `object` declarations (Milestone 4.1):
the engine dispatches an event by object identifier and event name, and the handler runs with
the object as `self` ([Language.md §8](Language.md#8-behaviours-rooms-actors-and-objects),
[Runtime.md](Runtime.md#behaviour-handlers-and-dispatch), [Examples/kitchen.sch](Examples/kitchen.sch)).

Language 3.0 (Milestone 4.4 Part II) compiles the scripts of a game into one program: variables,
functions, natives and enums declared in any file are usable in every other file, without imports
([Language.md §1](Language.md#1-source-files), [Compiler.md](Compiler.md#project-compilation)).
Rooms, actors and objects have behaviours (`room:`, `actor:`, `object NAME:`) with their own
private variables ([Language.md §7, §8](Language.md#7-variables-and-scope)).

### A game's scripts in short

Put each declaration where it belongs; the game finds it:

```python
# Scripts/Global.sch: game-wide things
var score: int = 0

func award_points(amount: int):
	score += amount
```

```python
# Rooms/Kitchen/Kitchen.sch: the kitchen's script
var oven_repaired: bool = False          # global, though declared here: every script sees it

room:
	var visits: int = 0                   # this kitchen visit's own
	func ready():
		visits += 1

object oven:
	func use():
		if not oven_repaired:
			oven_repaired = True
			award_points(10)              # declared in another file
```

```python
# Actors/Cook/Cook.sch: every actor using the cook's resource
actor:
	var conversations: int = 0            # each cook's own
	func talk_to():
		conversations += 1
		if oven_repaired:
			print("Thank you for repairing the oven.")
```

| Where a `var` is | It is | It lasts |
|---|---|---|
| at the top of any file | global | the whole game session |
| in `room:`, `actor:` or `object` | the room's, actor's or object's own | until the room is left |
| in a function | local | one call |

The game resource (`*.game.json`) lists the global scripts and the rooms; the build finds the
room and actor scripts through them and compiles everything into one program
([ResourceFormats.md](../Architecture/ResourceFormats.md#the-game-resource),
[Adventure.md](../Architecture/Adventure.md#game-sessions-adventureruntime)).

Language 2.0 (Milestone 4.4) added:
- required `var` declarations;
- the entity reference types `actor`, `object` and `entity`;
- scoped enums;
- the `vec2` type;
- native API interfaces: engine functions and enums declared once in C++ and used by both the
  compiler and the runtime ([Runtime.md](Runtime.md#native-interfaces));
- `spawn`, coroutine references and `cancel()`, and waiting for engine operations through
  completion tokens ([Language.md §9](Language.md#9-coroutines),
  [Runtime.md](Runtime.md#completion-tokens)).

Bytecode is format 3.0: typed registers, and one module for all the scripts of a game.

| Document | Contents |
|---|---|
| [Language.md](Language.md) | Language specification: syntax, types, operators, scopes, functions, objects and handlers, coroutines, runtime errors, differences from Python |
| [Compiler.md](Compiler.md) | Compiler architecture: lexer, parser, semantic analysis, IR, optimizer, code generation, diagnostics, `schuimmc` |
| [Bytecode.md](Bytecode.md) | The `.schb` format, instruction set, versioning and validation |
| [Runtime.md](Runtime.md) | VM, values and strings, native functions, object handler dispatch, scheduler, time, instruction budgets, `ScriptHost`, `schuimm-run` |
| [Examples/](Examples/) | Example scripts (all compiled and run by the tests; `kitchen.sch` shows object handlers, `spawn.sch` spawn and cancel) |

## Libraries

```text
schuimm_script_bytecode  <- schuimm_core               format, opcodes, shared arithmetic, reader/validator, writer, disassembler
schuimm_script_compiler  <- schuimm_script_bytecode    source -> bytecode (tools and tests only)
schuimm_script_runtime   <- schuimm_script_bytecode    VM, natives, scheduler, ScriptHost (linked by the game)
schuimm_application      <- ... schuimm_script_runtime (optional ScriptHost service)
```

None of them uses a platform, graphics or audio API. The game links the runtime only.

## Quick start

```bash
cmake --preset macos-clang && cmake --build --preset macos-clang-debug
B=Build/macos-clang/bin/Debug        # Windows: Build/windows-msvc/bin/Debug, Linux: Build/linux-gcc/bin/Debug

# Compile (with debug dumps) and run headlessly with simulated 0.25 s frames:
$B/schuimmc Documentation/Scripting/Examples/counter.sch -o counter.schb --ir-opt --disasm
$B/schuimm-run counter.schb --dt 0.25 --frame-log

# Diagnostics:
$B/schuimmc Tests/Script/Data/type_error.sch
```

In the game, the `Application` updates a `ScriptHost` every frame; the EngineDemo compiles
`Applications/EngineDemo/Scripts/demo.sch` at build time and runs two coroutines from it while
the scene keeps rendering (see the demo's on-screen "Scripts" panel).

## Debugging

| What | How |
|---|---|
| Tokens, AST, typed AST | `schuimmc --tokens`, `--ast`, `--types` |
| IR, optimized IR | `schuimmc --ir`, `--ir-opt` (`-O0` disables the optimizer) |
| Bytecode | `schuimmc --disasm`, `schuimm-run --disasm` |
| Execution trace | `schuimm-run --trace` (or `ScriptScheduler::setTraceCallback`) |
| Runtime errors | logged with script, function, line, column and call stack (category `Script`) |
