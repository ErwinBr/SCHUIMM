# Script runtime: VM, natives, coroutines

`schuimm_script_runtime` (`Source/Script/Runtime`) executes validated bytecode. It depends on
`schuimm_core` and `schuimm_script_bytecode` only — not on the compiler, and on no platform,
graphics or audio API.

```text
bytes ──Program::load──► Program (validated, linked, immutable, shared)
                            │
ScriptScheduler::start ─► Coroutine (registers + call frames) ──runCoroutine──► VM loop
         ▲                                                          │
         └──────── update(dt) once per frame ◄── Application ◄──────┘ natives (print, wait, ...)
```

## Programs

`Program::load(bytes, natives, name)` reads and validates the file ([Bytecode.md](Bytecode.md))
and links it: every native the script calls is looked up in the `NativeRegistry` by name, and its
signature must match the script's `extern func` declaration exactly. Errors:

```text
NotFound: Script 'Scripts/demo.sch' calls native function play_chime() -> None, which the engine does not provide
InvalidData: Script 'x.sch' declares native function say(string) -> int, but the engine provides say(string) -> None
```

A `Program` holds the module, its constants as runtime values and the resolved natives. It is
immutable and shared (`std::shared_ptr<const Program>`) by every coroutine running it, so the
bytecode lives exactly as long as it is needed.

## Values and strings

`Value` is a 16-byte tagged union: None, bool, int (`int64_t`), float (`double`) or string.
Strings are `StringObject`s: immutable, with an intrusive, **non-atomic** reference count; a
string `Value` holds one reference, so registers contain a pointer, not a `std::string`.
Ownership rules:

* the VM creates strings for concatenation and `str()`; `Program` owns its constant strings;
* copying a `Value` adds a reference, destroying it releases one; the last release frees it;
* strings reference nothing, so there are no cycles and no garbage collector is needed;
* all script execution is on the game thread; string values must not cross threads.

## Virtual machine

`runCoroutine(coroutine, budget, context, trace)` (`Interpreter.h`) is a register VM with a
plain `switch` dispatch (no computed goto or platform-specific tricks).

* **Call frames** (`CallFrame`): function, instruction pointer, base index of the frame's
  registers, and the absolute register receiving the result. All frames' registers live in one
  per-coroutine `std::vector<Value>`; frames address them by index, never by pointer, because the
  vector grows on calls.
* **Calls** push a frame inside the same loop — no C++ recursion — so any script frame can
  suspend, and the instruction budget covers nested calls. Arguments are copied from the
  caller's argument window into the callee's first registers. `Return` copies the value to the
  caller and shrinks the register stack (releasing the frame's values).
* **Limits** (`CoroutineLimits`): call depth (200), register stack (2²⁰ values), string length
  (16 MiB). Exceeding one is a runtime error.
* **Safety:** typed instructions check the tags of their operands (`AddInt` needs two ints);
  a mismatch — impossible for compiler output, possible for hand-made bytecode — is an
  `InvalidBytecode` error. Uninitialised registers are None, which no typed instruction accepts.
* **Arithmetic** uses `Script/Bytecode/Semantics.h`, shared with the optimizer:
  checked int operations, floored division/modulo, IEEE float, the conversions and the text of
  values ([Language.md §4.2, §6.3](Language.md#42-arithmetic)). Float text uses streams with the
  classic locale (no `std::to_chars`, which needs macOS 13.3).

### Runtime errors

`ScriptError` has a kind (`Runtime`, `Native`, `ExecutionLimit`, `InvalidBytecode`), the
message, the program name (the source file), function, line and column of the failing
instruction (from the bytecode's debug section), and the call stack:

```text
Scripts/demo.sch:4:8: Runtime error in 'inner': Division by zero
    called from 'outer' (line 2)
    called from 'main' (line 6)
```

The `ScriptHost` logs it through the engine `Logger` (category `Script`, level error).

### Tracing

A `TraceCallback` passed to `runCoroutine` (or `ScriptScheduler::setTraceCallback`) is called
before each instruction; `formatTrace` renders function, instruction index, source line, the
disassembled instruction and its input registers:

```text
main:11 (line 7)  LtInt r7, r5, r6  r5=1  r6=3
```

Tracing is off unless a callback is set; `schuimm-run --trace` enables it.

## Native functions

The VM knows how to call an engine function, not what it does. A native is registered with a
name, parameter types, return type, implementation and documentation:

```cpp
host.natives().add({"play_sound", {ScriptType::String, ScriptType::Float}, ScriptType::None,
    [&audio](NativeContext& context, std::span<const Value> arguments) {
        audio.play(arguments[0].asString(), arguments[1].asFloat());
        return NativeResult::returnValue();
    },
    "Plays a sound effect."});
```

```python
extern func play_sound(name: string, volume: float)   # in the script
```

* Arguments arrive already checked against the declared types; the return value is checked
  against the declared return type (a mismatch is a `Native` error).
* `ScriptType::Any` accepts every value but None (used by `print`).
* An implementation returns `NativeResult::returnValue(v)`, `waitFor(nanoseconds)`,
  `yieldNow()` or `error(message)`. `waitFor` and `yieldNow` suspend the coroutine — this is how
  `wait`/`yield` are implemented, and how future natives such as `wait_until`, `say` or
  `move_character` can suspend until something happens.
* **Only natives returning None may suspend.** The coroutine resumes *after* the call, so a
  native with a result would leave its destination register without a value; `waitFor` or
  `yieldNow` from such a native is a `Native` runtime error ("a native function with a result
  cannot suspend"). A native that waits and then produces a value (e.g. a dialogue choice) needs
  a "resume with result" mechanism, which does not exist yet.
* An exception thrown by an implementation is caught at the call and fails the calling coroutine
  with a `Native` error ("threw an exception: …"); other coroutines are unaffected.
* `NativeContext` tells the implementation which coroutine and program call it and the current
  scheduler time.
* Natives must not call back into the VM. They typically capture engine objects by reference;
  those must outlive every program using the native (destroy or `stopAll()` the host first).

The prelude (`registerStandardNatives`): `print(any)`, `wait(float)`, `yield()`.

## Scheduler

`ScriptScheduler` runs coroutines cooperatively on the calling (game) thread. No OS threads.

### States

```text
            start()
               │
               ▼        update(): run until...
           ┌─Ready ──────────────► Running ──► finished ───► Finished
           │   ▲                    │    │
  yield()  │   │ deadline reached   │    ├──► error / budget ─► Failed
  wait(0)  └───┤                    │    │
               └──── Waiting ◄──────┘    └──► (cancel) ─────► Cancelled
                       wait(d)
```

`Running` is only observable from inside natives. `Finished`, `Failed` and `Cancelled` are
terminal; the coroutine's frames and values are released at once, but its state, error and
result stay queryable until `release()` / `releaseFinished()`. Handles are generation-checked,
so a stale handle never refers to a new coroutine.

### Update

`update(deltaSeconds)`:

1. Advances scheduler time: `time += llround(deltaSeconds × timeScale × 10⁹)` nanoseconds.
   Time is an `int64_t` count of nanoseconds, so it does not drift through repeated
   floating-point additions, and a wait deadline is `time + llround(seconds × 10⁹)`, compared
   exactly. Negative or non-finite deltas count as 0.
2. Runs, in start order, every coroutine that is Ready, or Waiting with `deadline ≤ time`, until
   it waits, yields, finishes, fails or exhausts its instruction budget. Each coroutine runs at
   most once per update. A coroutine started during an update (e.g. from a callback) first runs in
   the next update. A coroutine never resumes in the update in which it suspended.
3. Never blocks: waiting coroutines cost nothing (they are not polled).

Completion, failure, cancellation and budget warnings are reported to an event callback.

**Exceptions.** Native exceptions never leave the VM (see above). The event and trace callbacks
are host code: an exception from them propagates out of `update()`, but a guard restores the
scheduler's internal state on the way out, and a coroutine interrupted in the middle of its run
is marked `Failed` ("Aborted by an exception thrown from a scheduler callback"), so the scheduler
stays usable.

### Time

Waits use **scaled game time**: the `Application` passes the same frame delta it gives
`ApplicationListener::onUpdate` — clamped to 0.25 s, and 0 for the first frame after a resume —
and nothing is updated while the application is suspended. `SchedulerSettings::timeScale`
scales it (slow motion, pause with 0). For waits in real time (e.g. UI that runs during a game
pause), run a second scheduler fed with unscaled deltas.

### Instruction budget

Each coroutine may execute `SchedulerSettings::instructionBudget` instructions (default 100 000)
per update. Every instruction counts, including calls and returns in nested script functions,
and natives cannot re-enter the VM, so no script can run longer. When a coroutine exceeds it:

* `BudgetPolicy::Fail` (**default**): the coroutine fails with an `ExecutionLimit` error naming
  the script, function and line it was executing (an endless loop shows up immediately, with its
  location);
* `BudgetPolicy::Preempt`: the coroutine is paused and continues next update; one warning is
  logged. Use it for long computations that are meant to span frames.

Either way the game loop continues.

## ScriptHost and the game loop

`ScriptHost` bundles the registry (with the prelude), program loading and the scheduler and
reports through the engine `Logger`:

* `natives()` — register game natives before loading programs that use them;
* `load(bytes, name)` — read, validate and link; logs success or the reason for failure;
* `start(program, "main")` — start a coroutine;
* `update(dt)` — one scheduler step, then logs and releases ended coroutines;
* `setOutputCallback` — receives `print()` output (also logged at Info, category `Script`);
* `setEventCallback`, `statistics()`, `stopAll()`.

The `Application` takes it as an optional service and updates it once per frame:

```cpp
script::ScriptHost scripts(logger);
Application application({logger, platform, window, graphics, audio, &scripts}, listener);
```

```text
listener.onUpdate(dt)  →  scripts.update(dt)  →  render
```

The order means a listener can start coroutines in `onUpdate` and they run in the same frame;
scripts see the world state after the game update and before rendering. The engine loads only
precompiled bytecode; the compiler is not linked into the game.

Lifetime: the `ScriptHost` must outlive the `Application`; game code whose objects natives
capture calls `stopAll()` in `onShutdown()` (as the demo does). Destroying the host cancels all
coroutines and releases everything.

## Running scripts headlessly: `schuimm-run`

```bash
schuimm-run [--entry NAME]... [--dt SECONDS] [--frames N] [--budget N] [--preempt] [--trace] [--disasm] [--frame-log] script.schb
```

Loads a `.schb` with the runtime only (no compiler), starts the entry functions (default
`main`; repeat `--entry` for concurrent coroutines) and simulates frames of `--dt` seconds
(default 0.016) until all coroutines end. `print` goes to stdout, errors to stderr. Exit code
0: all finished; 1: load/start failure or a coroutine failed; 3: frame limit reached.

```bash
schuimmc Documentation/Scripting/Examples/coroutines.sch -o coroutines.schb
schuimm-run coroutines.schb --entry narrator --entry clock --dt 0.25 --frame-log
```
