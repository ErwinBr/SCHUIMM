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

## Script state: global and instance variables

A `Program` is immutable; the variables its code writes live outside it, in two objects
(`Runtime/ScriptState.h`):

* `GlobalState::create(program)`: the global variables of one **game session**, at their
  initial values. Every coroutine of the session reads and writes the same ones.
* `BehaviourInstance::create(globals, behaviour, self)`: the instance variables of one entity's
  behaviour (an object of a room instance), at their initial values, and its `self`. The engine
  creates one per entity and dispatches its events through it, so the variables persist between
  events and two entities never share them. `expire()` marks an instance whose entity is gone:
  dispatching to it fails with `InvalidState`.

A coroutine holds its session's `GlobalState` and, when it runs a handler, the
`BehaviourInstance` (shared ownership): `LoadGlobal`/`StoreGlobal` and
`LoadInstance`/`StoreInstance` reach them by index, without names, and the storage can never be
freed while a frame uses it. This keeps *script storage* alive, never the engine's entity. The
state is set when a coroutine is created:

| Created by | Globals | Instance |
|---|---|---|
| `start(globals, function)` | the session's | none |
| `start(program, function)` | a fresh `GlobalState` (a run of its own, e.g. `schuimm-run`) | none |
| `dispatch(instance, event)` | the instance's session | the instance |
| `dispatch(program, object, event, self)` | fresh ones for this call | a fresh one |
| `Spawn` | the spawner's | the spawner's, for a function of the same behaviour |

An ended coroutine releases both with its frames. Reading a variable without the matching state
(only possible with hand-made coroutines) is an `InvalidBytecode` runtime error.

## Values and strings

`Value` is a 24-byte tagged union:
- None, bool, int (`int64_t`), float (`double`), string;
- `vec2`: two doubles (`Value::fromVec2`, `vec2X()`, `vec2Y()`), a plain value;
- enum: the enum's index in the module's enum table and the member ordinal
  (`Value::fromEnum`, `enumIndex()`, `enumOrdinal()`);
- references: `actor` (`Value::fromActor`) or `object` (`Value::fromObject`), each holding an
  `ObjectRef { std::uint64_t id; }`. That is an opaque handle chosen by the engine (see
  [Object handlers](#behaviour-handlers-and-dispatch)); it owns nothing, and id 0 is the null
  reference. A register declared `entity` holds either kind.

`Value::matches(TypeRef)` decides whether a value fits a declared type: `any` takes everything
but None, `entity` takes actor and object references, and an enum value must belong to the
declared enum.
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
* **Safety:** the validator checks every instruction against the function's register types
  ([Bytecode.md](Bytecode.md#typed-registers)). The VM also checks the tags of the operands
  (`AddInt` needs two ints); a mismatch is an `InvalidBytecode` error. That is impossible for
  compiler output, but it catches a read of an uninitialised register, which holds None and is
  accepted by no typed instruction.
* **Vectors** follow float arithmetic per component; `DivVec2` by zero is the "Division by zero"
  error, like float division.
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

The script of the error is the source file of the failing function (its entry in the program's
Sources table). A caller in another file is shown with its file:
`called from 'use' (Rooms/Kitchen.sch:12)`.

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

The VM knows how to call an engine function, not what it does.

### Native interfaces

Engine APIs are declared once, as a `NativeInterface` (`Script/Bytecode/NativeInterface.h`):
- a name, which also qualifies its enums (`adventure.Direction`);
- its enums (`EnumDeclaration`: a name and members);
- its functions (`NativeDeclaration`):
  - a name;
  - typed parameters and result (`NativeType`, which names an enum of the interface for enum
    types);
  - an execution category: `Immediate`, `Suspending` or `Background`;
  - whether it is pure (no side effects; the optimizer may drop unused calls);
  - documentation.

An interface holds no implementations and needs no engine subsystem. The compiler reads it to
resolve calls (`CompileOptions::interfaces`, `schuimmc --interface NAME`), and the runtime binds
implementations to it. Both work from the same declaration, so they cannot disagree:

```cpp
std::unordered_map<std::string, NativeImplementation> implementations;
implementations["describe_mood"] = [](NativeContext&, std::span<const Value> arguments) { ... };
// ... one per declared function
REQUIRE(registry.bind(myInterface(), std::move(implementations)));
```

`NativeRegistry::bind` fails, and registers nothing, if:
- the interface is malformed (`checkInterface`);
- a declaration has no implementation, or an implementation matches no declaration;
- a function name is already registered;
- an enum of that qualified name is registered with different members.

Enum values cross the boundary as ordinals:
- an enum argument arrives as a `Value` whose `enumOrdinal()` is the member;
- a native returns `Value::fromEnum(0, ordinal)`. The VM sets the calling module's enum index and
  fails the call ("invalid enum ordinal") if the ordinal is out of range.

**The core interface** (`coreInterface()`, bound by `registerStandardNatives`) is what every
script sees: `print(any)`, `wait(float)`, `yield()`, `cancel(coroutine)`, the vector functions
`length`, `distance`, `normalize` and `lerp` ([Language.md §6.2](Language.md#62-built-in-functions)),
and the hidden `await_operation(operation)`. Hidden declarations cannot be called by name; the
compiler turns `wait(op)` with an `operation` argument into a call of it. That is the only
overload in the language.

### Single natives

A native that no interface declares is registered on its own, with a name, parameter types,
return type, implementation and documentation. Scripts declare it with `extern func`:

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
* `ScriptType::Actor` and `ScriptType::Object` parameters and results carry `ObjectRef` handles
  (`arguments[0].asReference()`, `Value::fromActor(ref)`, `Value::fromObject(ref)`). An `Entity`
  parameter accepts either; `value.type()` tells which. This is how scripts reach entity state:
  the engine resolves the handle to its own entity. A native cannot return `entity` or `any`.
* An implementation returns `NativeResult::returnValue(v)`, `waitFor(nanoseconds)`,
  `yieldNow()`, `await(token, abandonCancels)` or `error(message)`. `waitFor`, `yieldNow` and
  `await` suspend the coroutine. This is how `wait` and `yield` are implemented, and how natives
  wait for gameplay operations ([Completion tokens](#completion-tokens)). A suspended call
  resumes at the next instruction; the native is never called again for it.
* `NativeContext::scheduler` is the scheduler running the coroutine (null when a test runs the
  VM directly). The core `cancel()` uses it.
* Every native declares an **execution category** (`NativeCategory`), part of its contract;
  scripts call all three alike:
  - `Immediate`: performs an action and returns (`set_position`);
  - `Suspending`: suspends the caller until the action it started completes (`walk_to`, `wait`);
  - `Background`: starts an operation and returns at once, possibly with an `operation`
    reference (`play_animation`).
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

### Linking

`Program::link` resolves every native import by name and compares its signature with the
registered function, including the identity of enum types. It also checks every engine enum of
the module against the registry: same qualified name, same members in the same order.
- A missing function or enum fails with `NotFound`.
- A different signature, or an enum with other members, fails with `InvalidData` ("recompile the
  script from its source").

## Behaviour handlers and dispatch

A behaviour declaration — `room:`, `actor:` or `object NAME:`
([Language.md §8](Language.md#8-behaviours-rooms-actors-and-objects)) — compiles to ordinary
functions plus an entry in the bytecode's Behaviours table. The engine — the room instance,
later the adventure runtime — creates a `BehaviourInstance` per room, actor or object
([Script state](#script-state-global-and-instance-variables)) and invokes a handler of it by
event name:

```cpp
// The instance's self is the engine's handle of the object.
auto result = host.dispatch(blenderInstance, "use_with", {Value::fromObject(ObjectRef{bananaId})}, roomOwner);
if (!result) {
    // InvalidArgument: no instance, or the arguments do not match the handler (logged);
    // InvalidState: the instance has expired (its entity is gone).
} else if (!result->handled) {
    // The behaviour has no such handler: a normal no-op (default behaviour is the caller's
    // business).
} else {
    // result->handle: a Ready coroutine; it runs in the next update like any other.
}
```

* `Program::findBehaviour(kind, source, name)` finds a behaviour of a program that holds several
  scripts: the object `door` of `Rooms/Kitchen.sch` is not the `door` of `Rooms/Garden.sch`; the
  `room:` of a room script and the `actor:` of an actor script are found by source alone.
  `findHandler(behaviour, event)` finds a handler of it. `findHandler(object, event)`,
  `hasHandler` and `hasObject` look an object's handler up by object name alone (in the first
  room script declaring that object). None of them runs anything. Event names are arbitrary
  strings; nothing in the compiler or VM knows `look_at` or `use`.
* `ScriptScheduler::dispatch` (and `ScriptHost::dispatch`, which adds logging) returns
  `Result<DispatchResult>`; `handled == false` for a missing handler, without creating a
  coroutine; otherwise the coroutine's handle. Its outcome is observed as for every coroutine:
  `state()`, `result()`, `error()` and the event callback. A runtime error inside the handler is
  a `Failed` coroutine with a normal `ScriptError`; handlers appear as `blender.use`,
  `room.ready` or `actor.talk_to` in errors, stacks and traces.
* `dispatch(program, object, event, self)` finds the object behaviour by name and runs the
  handler with fresh state of its own (tools, tests, scripts without variables).
* `Program::findFunction` (and so `start()`) only finds global functions: a handler cannot be
  started without its instance. Global functions stay startable by name (`start(globals,
  "enter")`), whichever file declares them.

**Context: the behaviour instance.** A handler runs *for* an instance: the coroutine holds the
`BehaviourInstance`, and `LoadSelf`, `LoadInstance` and `StoreInstance` read it. `self` is not a
parameter. This matters because handlers call each other: a call or spawn of a function of the
same behaviour runs for the same instance, through any depth of calls and across `wait()`, with
no propagation through call frames. The validator guarantees what makes one context per
coroutine enough: a behaviour's functions are only called or spawned by functions of the same
behaviour (and started by dispatch, which sets the instance), and global functions use neither
`self` nor instance variables. So every frame running a behaviour's code is in a coroutine
running for an instance of that behaviour, and a global function called from a handler cannot
reach the caller's private state. The VM checks the instance again at each access
(`InvalidBytecode` for a hand-made coroutine without one).

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

With variables in scripts, a game dispatches through a `BehaviourInstance` per object
(`ScriptHost::dispatch(instance, event, arguments, owner)`), so the object's instance
variables persist between its events. The by-name `dispatch(program, object, ...)` gives each
call fresh state and suits scripts without variables and tools.

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
                 wait(d), await(token)
```

A coroutine waiting for a completion token stays Waiting until the token is resolved: completed
makes it Ready, cancelled makes it Cancelled.

`Running` is only observable from inside natives. `Finished`, `Failed` and `Cancelled` are
terminal; the coroutine's frames and values are released at once, but its state, error and
result stay queryable until `release()` / `releaseFinished()`. Handles are generation-checked,
so a stale handle never refers to a new coroutine.

**Reclamation policy.** An ended coroutine keeps a small record (state, error, result) until it is
released, and freed slots are reused.
- `ScriptHost::update` calls `releaseFinished()` after every scheduler update. That includes
  spawned coroutines whose handles a script discarded (`spawn blink()` in a loop). A host's
  storage is therefore bounded by the number of coroutines alive at once.
  `scheduler().slotCapacity()` reports it.
- A bare `ScriptScheduler` reclaims only what its owner releases, so its owner calls
  `releaseFinished()` regularly.

Scripts cannot inspect an ended coroutine (there is no joining), so nothing is lost.

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

### Spawn

The `Spawn` instruction calls `ScriptScheduler::spawn(parent, program, function, arguments)`:
- it adds a Ready coroutine for the function with the arguments the caller evaluated;
- the new coroutine inherits the parent's execution owner;
- like every coroutine added during an update, it first runs in the next update, after the
  existing coroutines, in spawn order;
- the caller continues immediately, with the new handle in its destination register.

There is one VM path: spawning an engine function runs a compiler-generated wrapper,
`spawn:walk_to`, which calls the native ([Compiler.md](Compiler.md#semantic-analysis)).
`Program::findFunction` does not find these wrappers by name.

### Cancellation

`cancel(handle)` (and `cancelOwner`, `clear`, and the script's `cancel()`):
- A coroutine that is not running ends as Cancelled at once. Its frames and values are released,
  and its subscription to a completion token is removed. If it waited with
  `abandonCancels`, the token's abandon handler runs afterwards.
- The **running** coroutine is never torn down under the VM. Cancelling it sets a request,
  which the VM checks after every native call. The coroutine then ends as Cancelled at that call
  boundary, and none of its later instructions run. This is how a coroutine is stopped by a
  native it called itself.
- A stale or ended handle is ignored (`false`); handles are generation-checked.
- Cancelling a coroutine does not cancel the coroutines it spawned.

### Execution owners

An `ExecutionOwner` is an opaque id (`ScriptScheduler::createOwner`):
- `start` and `dispatch` take an owner, and spawned coroutines inherit it;
- `cancelOwner(owner)` cancels every coroutine of the owner that has not ended, including the
  running one, as a request;
- `activeCount(owner)` counts them.

A `RoomInstance` owns the coroutines it starts and cancels its owner when it is destroyed
([Rooms.md](../Architecture/Rooms.md)).

### Completion tokens

A `CompletionToken` (`Runtime/Completion.h`) represents **one** accepted engine operation, such
as a walk request or an animation playback. It is never "whatever the actor is doing now".
- **Resolution, in two steps** that an engine system may separate:
  - `recordCompleted()` / `recordCancelled(reason)` fix the outcome the moment the operation
    ends. The first record wins, and reasons are such as "stopped", "replaced" or "destroyed".
    `state()` reports the outcome at once, and a new wait sees it, but nobody is notified.
  - `deliver()` notifies the waiters, once, at a safe point; the adventure layer does it in
    `publish()`.

  `complete()` and `cancel(reason)` do both.
- **Lifetime.** It is reference-counted (non-atomic; game thread only). It is held by the engine,
  by `operation` values in scripts and by waiting coroutines, and freed with its last reference.
  There is no history and no registry.
- **Retained outcome.** A waiter whose operation completed sees that completion even if the
  entity started another operation before the waiter resumed.
- **Waiting.** A native suspends its caller with `NativeResult::await(token, abandonCancels)`
  ([Native functions](#native-functions)). The scheduler subscribes the coroutine to the token.
  Delivering notifies each subscribed coroutine exactly once:
  - completed makes it Ready, and it runs from the scheduler loop. Delivering never runs script
    code, so a native or engine system may deliver at any time without recursion;
  - cancelled cancels it.
- **Before the wait.** A token recorded before the wait, delivered or not, is not lost: completed
  returns at once, cancelled cancels the caller.
- **Not awaitable.** `setNotAwaitable(message)` marks an operation that never ends by itself;
  waiting for it is a Native error with that message.
- **Abandoning.** `setAbandonHandler` lets the engine stop an operation whose waiter gave up
  (`walk_to` stops the walk when its coroutine is cancelled).
- **Outliving the scheduler.** A token may outlive the scheduler; the scheduler unsubscribes its
  waiters when it is destroyed.

The VM knows nothing about what a token stands for: movement and animation reach scripts only
through natives that create and resolve tokens.

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

`ScriptHost` bundles the registry (with the core interface), program loading and the scheduler and
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

`schuimm-run` registers only the core interface, so scripts using game natives (like
`kitchen.sch`) cannot be run with it.
