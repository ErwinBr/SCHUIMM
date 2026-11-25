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

`Value` is a 16-byte tagged union: None, bool, int (`int64_t`), float (`double`), string or
object. An object value is an `ObjectRef { std::uint64_t id; }`: an opaque handle chosen by the
engine (see [Object handlers](#object-handlers-and-dispatch)); it owns nothing.
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
* `ScriptType::Object` parameters and results carry `ObjectRef` handles (`arguments[0].asObject()`,
  `Value::fromObject(ref)`). This is how scripts reach object state: the engine resolves the
  handle to its own object.
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

## Object handlers and dispatch

An `object` declaration ([Language.md §8](Language.md#8-objects-and-handlers)) compiles to
ordinary functions plus an entry in the bytecode's Objects table. The engine — later, the
adventure runtime — invokes a handler by object identifier and event name:

```cpp
// ObjectRef: the engine's handle of the room object; it becomes "self".
auto result = host.dispatch(roomScript, "blender", "use_with", ObjectRef{blenderId},
                            {Value::fromObject(ObjectRef{bananaId})});
if (!result) {
    // InvalidArgument: the arguments do not match the handler's parameters (logged).
} else if (!result->handled) {
    // The script has no such object or handler: a normal no-op (default behaviour is the
    // caller's business).
} else {
    // result->handle: a Ready coroutine; it runs in the next update like any other.
}
```

* `Program::findHandler(object, event)`, `hasHandler`, `hasObject` look handlers up without
  running anything. Event names are arbitrary strings; nothing in the compiler or VM knows
  `look_at` or `use`.
* `ScriptScheduler::dispatch` (and `ScriptHost::dispatch`, which adds logging) returns
  `Result<DispatchResult>`: an error only for invalid arguments, a null `self` (id 0, checked
  first) or no program;
  `handled == false` for a missing object or handler, without creating a coroutine; otherwise the
  coroutine's handle. Its outcome is observed as for every coroutine: `state()`, `result()`,
  `error()` and the event callback. A runtime error inside the handler is a `Failed` coroutine
  with a normal `ScriptError`; handlers appear as `blender.use` in errors, stacks and traces.
* `Program::findFunction` (and so `start()`) only finds room-level functions: a handler cannot
  be started without its object.
* Room-level functions of the same script stay startable by name (`start(program, "enter")`).

**Context: a hidden parameter.** A handler's parameter 0 is `self`; dispatch passes
`[Value::fromObject(self), arguments...]` to an ordinary entry frame (`Coroutine::createHandler`).
This reuses everything that exists: the calling convention and argument checks, the bytecode
validation of argument windows, and the coroutine's register stack. `self` is just register 0
of the handler's frame, so it is valid for exactly as long as the coroutine — across `wait()`,
`yield()` and budget preemption — and every invocation has its own. The alternative, a context
field in `CallFrame`, would have needed a new call path and opcode to read it, and propagation
rules for nested calls; there is no need for that, because a handler's callees are room-level
functions without `self`.

**Integration boundary.** The script side ends at the `ObjectRef`. The engine:

* owns the room objects and their state, and assigns the ids. **An id is never reused during the
  process lifetime**, so two instances of the same room script never share an id (or state), and
  a handle stays bound to the object it was issued for. Id 0 is the null handle and never refers
  to an object (dispatch rejects it). A monotonic 64-bit counter satisfies this, and so does
  `{index:32, generation:32}` packed into the id, if the engine wants to reuse slots;
* fails natives given a stale or unknown handle with an error (`NativeResult::error`), which is a
  runtime error of the calling coroutine — never acts on whatever object holds that slot now;
* provides natives taking `object` for whatever state scripts may read or change (for now
  `get_flag(o: object, name: string) -> bool` and similar, as in
  [Examples/kitchen.sch](Examples/kitchen.sch); the tests implement them with a map);
* decides which handler to dispatch for an interaction (no fallback or reverse dispatch here).

**Stale handles.** A handler can keep a handle while it is suspended — `self`, or an `object`
it got from a native. If the engine destroys that object meanwhile (the room is left, the item
is consumed) and later creates another one, the new object gets a new id, so the resumed handler
cannot reach it through the old handle: its next native call with the old handle fails with a
runtime error naming the handler and line. Equality of handles is equality of ids, which under
this contract is exactly object identity. Bytecode has no `object` constants, so scripts can only
hold handles the engine issued.

Room resources and room instances, which own the objects and allocate their handles as this
contract requires, are described in [Rooms.md](../Architecture/Rooms.md): `RoomInstance::dispatch`
calls `ScriptHost::dispatch` with the object's handle. The adventure runtime (interaction,
natives over object state) and `self.property` syntax are later milestones.

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
* `dispatch(program, object, event, self, arguments)` — invoke an object handler (see above);
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
schuimm-run [--entry NAME]... [--dispatch OBJECT.EVENT[=ID]]... [--dt SECONDS] [--frames N] [--budget N] [--preempt] [--trace] [--disasm] [--frame-log] script.schb
```

Loads a `.schb` with the runtime only (no compiler), starts the entry functions (default
`main`; repeat `--entry` for concurrent coroutines), dispatches the given handlers without
arguments with `self` = `object#ID` (default 1; `main` then only runs if named with `--entry`;
a missing handler is reported and skipped) and simulates frames of `--dt` seconds
(default 0.016) until all coroutines end. `print` goes to stdout, errors to stderr. Exit code
0: all finished; 1: load/start failure or a coroutine failed; 3: frame limit reached.

```bash
schuimmc Documentation/Scripting/Examples/coroutines.sch -o coroutines.schb
schuimm-run coroutines.schb --entry narrator --entry clock --dt 0.25 --frame-log
schuimmc Tests/Script/Data/objects.sch -o objects.schb
schuimm-run objects.schb --dispatch lamp.look_at=7 --dispatch lamp.use --dt 0.25
```

`schuimm-run` registers only the prelude natives, so scripts using game natives (like
`kitchen.sch`) cannot be run with it.
