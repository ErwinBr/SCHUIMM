# Scripting (Milestone 3)

SCHUIMM runs game logic in **SCHUIMM Script**, a small, statically typed language with
Python-inspired syntax. Scripts are compiled ahead of time into validated, platform-independent
bytecode and executed by a register VM as cooperative coroutines driven by the game loop.

Room scripts group the event handlers of room objects in `object` declarations (Milestone 4.1):
the engine dispatches an event by object identifier and event name, and the handler runs with
the object as `self` ([Language.md §8](Language.md#8-objects-and-handlers),
[Runtime.md](Runtime.md#object-handlers-and-dispatch), [Examples/kitchen.sch](Examples/kitchen.sch)).

| Document | Contents |
|---|---|
| [Language.md](Language.md) | Language specification: syntax, types, operators, scopes, functions, objects and handlers, coroutines, runtime errors, differences from Python |
| [Compiler.md](Compiler.md) | Compiler architecture: lexer, parser, semantic analysis, IR, optimizer, code generation, diagnostics, `schuimmc` |
| [Bytecode.md](Bytecode.md) | The `.schb` format, instruction set, versioning and validation |
| [Runtime.md](Runtime.md) | VM, values and strings, native functions, object handler dispatch, scheduler, time, instruction budgets, `ScriptHost`, `schuimm-run` |
| [Examples/](Examples/) | Example scripts (all compiled and run by the tests; `kitchen.sch` shows object handlers) |

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
