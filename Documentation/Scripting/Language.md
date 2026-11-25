# SCHUIMM Script — language specification (version 1.1)

SCHUIMM Script is a small, statically typed scripting language with Python-inspired syntax,
made for 2D adventure games: readable scripts that call engine functions and wait without
blocking the game. It is **not** Python; see [Differences from Python](#differences-from-python).

This document describes the language as implemented by `schuimmc` and the SCHUIMM runtime.
The language version (1.1) is recorded in every bytecode file and is independent of the bytecode
format version ([Bytecode.md](Bytecode.md)). Version 1.1 added object declarations and `self`
([§8](#8-objects-and-handlers)); `object` and `self` became keywords.

```python
# A complete script.
extern func play_chime()

func main():
	print("Welcome to SCHUIMM!")

	counter = 0
	while counter < 3:
		print(counter)
		counter = counter + 1
		wait(1.0)

	play_chime()
	print("Finished!")
```

More examples: [Examples/](Examples/).

## 1. Source files

* Source files are UTF-8. Invalid UTF-8 is an error. Line endings may be `\n`, `\r\n` or `\r`.
* A file is one *module*: a sequence of function declarations (`func`), native function
  declarations (`extern func`) and object declarations (`object`, [§8](#8-objects-and-handlers)).
  There are no global variables and no statements at the top level.
* Diagnostics give `file:line:column`; lines and columns are 1-based and columns count bytes
  (a tab is one column).

## 2. Lexical structure

### 2.1 Lines and indentation

* A *logical line* ends at a line break, except inside parentheses, where line breaks are
  ignored (implicit line joining). There is no backslash continuation and no `;`.
* Blocks are delimited by indentation. **A tab is one indentation level, and only tabs may
  indent.** The leading whitespace of a line must consist of tabs only:
  * spaces in leading whitespace are an error ("Indentation must use tabs, not spaces"), and so
    is a mix of tabs and spaces ("Inconsistent indentation");
  * a line may be at most one level deeper than the previous line;
  * a line ending with `:` must be followed by a line one level deeper ("Expected an indented
    block"); a deeper line anywhere else is "Unexpected indentation";
  * a line may dedent any number of levels.
* Blank lines and lines containing only a comment do not affect indentation, whatever their
  leading whitespace.
* Continuation lines inside parentheses may start with tabs and spaces freely.
* A tab anywhere else (between tokens) is an error. Tabs inside string literals and comments are
  allowed.
* The lexer turns indentation into `INDENT` and `DEDENT` tokens and ends every logical line with
  `NEWLINE`. At the end of the file it closes all open blocks.

### 2.2 Comments

`#` starts a comment that runs to the end of the line (outside string literals).

### 2.3 Identifiers and keywords

* Identifiers: `[A-Za-z_][A-Za-z0-9_]*` (ASCII only). Use `snake_case` by convention.
* Keywords: `func extern object self if elif else while for in return pass and or not True False None`.
* Reserved names (not keywords, but they cannot be redefined or used as variable or function
  names): the type names `int float bool string`, the conversion functions `int float str`,
  `range`, and the built-in functions `print wait yield`.
* `true`, `false`, `none` are ordinary (undefined) names; the compiler suggests `True`, `False`
  or `None`.

### 2.4 Literals

| Literal | Syntax | Type |
|---|---|---|
| Integer | decimal digits, e.g. `0`, `42`, `9223372036854775807`; no leading zeros, no `_`, no hex | `int` |
| Float | digits `.` digits, optionally with exponent: `3.5`, `0.25`, `1e3`, `2.5E-2`. A digit is required on both sides of `.` (`1.` and `.5` are errors) | `float` |
| String | `'...'` or `"..."` on one line | `string` |
| Boolean | `True`, `False` | `bool` |
| None | `None` | `None` (see §3) |

* An integer literal must fit in `int`. The single exception is `-9223372036854775808`, accepted
  because the minus sign is part of the literal there.
* A float literal that overflows to infinity is an error.
* String escapes: `\n \t \r \0 \\ \' \"` and `\xHH` with `HH` ≤ `7F` (ASCII). Any other escape is an
  error. Non-ASCII text is written directly (UTF-8). There are no triple-quoted, raw or f-strings.

### 2.5 Operators and punctuation

`+ - * / // % == != < <= > >= = ( ) , : ->` and the keyword operators `and or not`.

## 3. Types

| Type | Values |
|---|---|
| `int` | signed 64-bit integers, −2⁶³ … 2⁶³−1 |
| `float` | IEEE 754 binary64 (including ±infinity and NaN, which can arise from arithmetic) |
| `bool` | `True`, `False` |
| `string` | immutable UTF-8 text |
| `object` | a handle of an engine object, e.g. an object of the current room ([§8.4](#84-self-and-the-object-type)) |
| `None` | the "no value" type: the result of a function without a return type |

`None` is not a value a variable can hold. It is the return type of functions declared without
`->`, and a call of such a function can only be used as a statement.

### 3.1 Type inference and annotations

* A variable gets the type of its initialising expression: `size = 100` is `int`,
  `speed = 3.5` is `float`, `has_key = True` is `bool`, `name = "Alex"` is `string`.
* An annotation states the type explicitly: `size: int = 100`. The initialiser must be
  assignable to it (§3.2). An annotation without an initialiser is an error.
* Parameter types are mandatory: `func f(amount: int, scale: float)`.
* The return type follows `->`; without it the function returns `None`.

### 3.2 Conversions

There is exactly one implicit conversion: **int → float** (widening). It applies where a `float`
is expected and an `int` is given:

* assignment to a `float` variable, argument for a `float` parameter, value of `return` in a
  function returning `float`;
* the `int` operand of an arithmetic or comparison operator whose other operand is `float`;
* both operands of `/`.

Integers beyond 2⁵³ may lose precision in this conversion (round to nearest).

Every other conversion is explicit:

| Call | Accepts | Result |
|---|---|---|
| `int(x)` | `int` (unchanged), `float` (truncates toward zero), `bool` (`True` → 1, `False` → 0) | `int` |
| `float(x)` | `int`, `float` | `float` |
| `str(x)` | `int`, `float`, `bool`, `string` | `string` (text as in §6.3) |

`int(x)` of a NaN, an infinity or a float outside the `int` range is a runtime error.
There is no conversion to `bool` and no truthiness: conditions must be `bool`.

## 4. Expressions

### 4.1 Precedence and associativity

From lowest to highest:

| Level | Operators | Associativity |
|---|---|---|
| 1 | `or` | left |
| 2 | `and` | left |
| 3 | `not` (prefix) | — |
| 4 | `== != < <= > >=` | **non-associative**: `a < b < c` is a syntax error |
| 5 | `+ -` | left |
| 6 | `* / // %` | left |
| 7 | `-` (prefix negation) | — |
| 8 | call `f(...)`, parentheses | — |

So `-a * b` is `(-a) * b`, `not a == b` is `not (a == b)`, and `a or b and c` is
`a or (b and c)`. Only named functions can be called (`f(x)`, not `f(x)(y)`).

### 4.2 Arithmetic

| Operator | Operands | Result |
|---|---|---|
| `+ - *` | `int, int` | `int`, checked |
| `+ - *` | numeric, at least one `float` | `float` |
| `/` | numeric | always `float` (true division) |
| `//` | `int, int` | `int`, floored division (rounds toward −∞), checked |
| `//` | numeric, at least one `float` | `float`, floored |
| `%` | `int, int` | `int`; the result has the sign of the divisor: `a == (a // b) * b + a % b` |
| `%` | numeric, at least one `float` | `float`, same rule |
| `-x` | `int` / `float` | same type; `int` negation is checked |
| `+` | `string, string` | concatenation |

**Integer overflow** — any `int` result outside −2⁶³ … 2⁶³−1 (including `-(-2⁶³)` and
`-2⁶³ // -1`) — is a **runtime error** ("Integer overflow"). Integers never wrap around.

**Division by zero** — `//`, `%` with an `int` divisor of 0 and `/`, `//`, `%` with a divisor of
0 or 0.0 — is a runtime error, for `int` and `float` alike.

Other `float` operations follow IEEE 754 (overflow gives ±infinity; `inf - inf` gives NaN).

### 4.3 Comparisons

* `==` and `!=` compare two values of the same type, or an `int` with a `float` (the `int` is
  widened). `bool`, `string` and `object` compare only with their own type; two `object` values
  are equal when they are handles of the same engine object. `None` cannot be compared.
* `< <= > >=` compare numbers (with widening) or two strings. Strings compare byte-wise by their
  UTF-8 encoding (which equals code point order). Booleans and objects are not ordered.
* Floats compare by IEEE rules: NaN is unequal to everything, including itself.
* The result is `bool`.

### 4.4 Logical operators

`and`, `or` and `not` take `bool` operands only and produce `bool`. `and` and `or`
**short-circuit**: the right operand is evaluated only if the left does not decide the result.

### 4.5 String concatenation

`+` joins two strings. A string and a number cannot be added; convert with `str(...)`:
`"Score: " + str(score)`. A string created at run time may be at most 16 MiB (runtime error
"String too long" otherwise).

### 4.6 Evaluation order

Expressions are evaluated **left to right**: both operands of a binary operator (left first),
and call arguments in order, before the call. Short-circuiting (§4.4) is the only exception.
Evaluating an expression cannot change a variable (assignment is a statement).

## 5. Statements

```text
statement := assignment | call | if | while | for | return | pass
```

| Statement | Form |
|---|---|
| Assignment | `name = expression` or `name: type = expression` |
| Call | `function(arguments)` — the only expression allowed as a statement |
| Conditional | `if c:` block, any number of `elif c:` blocks, optional `else:` block |
| Loop | `while c:` block |
| Counting loop | `for name in range(...):` block |
| Return | `return` or `return expression` |
| Empty | `pass` |

A statement that is just an expression other than a call (`x == 1`, `a + b`) is an error,
since it would have no effect.

### 5.1 Conditions

The condition of `if`, `elif` and `while` must be of type `bool`.

### 5.2 The for loop (iteration model)

`for` iterates over a range of integers only:

| Form | Values |
|---|---|
| `range(end)` | `0, 1, …, end − 1` |
| `range(start, end)` | `start, start + 1, …, end − 1` |
| `range(start, end, step)` | `start, start + step, …` while `< end` (step > 0) or `> end` (step < 0) |

* All arguments are `int`. They are evaluated **once, left to right, before the first
  iteration**; changing a variable used as a bound inside the body has no effect.
* `step` 0 is a runtime error. An empty range runs the body zero times.
* The loop variable is a new `int` variable, visible only in the body. Assigning to it inside
  the body does not change the iteration: the next iteration starts from the hidden counter.
* The hidden counter cannot overflow: `range(m - 3, m, 2)` with `m` the largest `int` works.
* There is no `break` or `continue` (see Limitations); leave a loop early with `return` or a
  condition.

## 6. Functions

```python
func calculate_damage(amount: int, multiplier: float) -> float:
	return amount * multiplier
```

* Functions are declared at the top level and may be called before their declaration and
  recursively. Names must be unique among functions, natives and reserved names.
* Arguments are passed **by value** (strings are immutable, so no copy is observable); the number
  and types of arguments must match the parameters (with int → float widening).
* `return expression` returns a value; in a function with a return type, every path must end
  with a `return` (or an endless `while True:` loop) — the compiler checks this.
  A function without a return type may use `return` without a value (or `return None`); at the
  end of its body it returns automatically.
* Recursion depth is limited (default 200 nested calls); exceeding it is a runtime error.

### 6.1 Native (engine) functions

Engine functions are declared with `extern func` and implemented in C++ by the game:

```python
extern func play_sound(name: string, volume: float)
extern func player_health() -> int
```

The declaration gives the signature the compiler checks calls against. When the bytecode is
loaded, every *called* native is looked up by name in the engine's registry and its signature must
match exactly; otherwise loading fails ([Runtime.md](Runtime.md#native-functions)).

### 6.2 Built-in functions

Always available without declaration:

| Function | Meaning |
|---|---|
| `print(value)` | Writes the text of any value (not `None`) to the script output (the engine log and, in the demo, the on-screen console). |
| `wait(seconds: float)` | Suspends the current coroutine for `seconds` of game time. `wait(0)` resumes at the next update. Negative, NaN, infinite or > 10⁹ s is a runtime error. |
| `yield()` | Suspends the current coroutine until the next scheduler update (normally the next frame). |
| `int(x)`, `float(x)`, `str(x)` | Conversions (§3.2). |

### 6.3 Text of values

`print` and `str` produce: integers in decimal (`-42`); floats as the shortest decimal that
reads back to the same value, always with `.` or an exponent (`1.0`, `0.1`, `2.5e-07`, `1e+20`,
`inf`, `-inf`, `nan`, `-0.0`), independent of the system locale; booleans as `True`/`False`;
strings unchanged. `print` shows an object handle as `object#` and its engine id (`object#7`);
`str()` does not accept objects.

## 7. Variables and scope

* A variable is **declared by its first assignment** (`x = 1`) or by an annotated assignment
  (`x: int = 1`). It always has a value: there are no uninitialised variables.
* Scopes are **lexical and block-based**: a variable declared in a block (function body, `if`
  branch, loop body) is visible from its declaration to the end of that block, including nested
  blocks. It is *not* visible after the block.
* `x = expression` where `x` is already visible **assigns** to that variable; the value must be
  assignable to its type (`count = "many"` for an `int` variable is an error).
* **No shadowing**: declaring a name that is already visible (an annotated assignment, a `for`
  loop variable or a parameter name) is an error. Sibling blocks may reuse a name.
* Variables cannot use the name of a function, native or reserved name.
* Each call has its own variables; coroutines never share variables.

## 8. Objects and handlers

Room scripts describe what happens when the player interacts with the objects of a room. An
`object` declaration groups the **handlers** of one object: the functions the engine runs for
events such as looking at or using it.

```python
extern func say(text: string)
extern func get_flag(o: object, name: string) -> bool
extern func set_flag(o: object, name: string, value: bool)
extern func play_sound(name: string, volume: float)


func play_blender_sound():
	play_sound("blender.wav", 1.0)


object blender:
	func look_at():
		say("An old blender.")

	func use():
		if get_flag(self, "is_running"):
			say("It's already running.")
		else:
			set_flag(self, "is_running", True)
			play_blender_sound()


object cupboard:
	func look_at():
		say("A wooden cupboard.")
```

The complete example is [Examples/kitchen.sch](Examples/kitchen.sch).

### 8.1 Syntax

```text
object NAME:
	func HANDLER(parameters) [-> type]:
		block
	...
```

* `object` declarations are top-level declarations, like `func`. They cannot be nested and
  cannot appear inside a function.
* The body is an indented block (one tab, §2.1) of one or more `func` declarations. It contains
  nothing else: no statements, `pass`, variables, `extern func` or nested `object`. An object
  without handlers is an error ("Expected an indented block of handler functions"); leave out the
  declaration instead.
* Handlers are ordinary functions: typed parameters, an optional return type, the usual
  statements, `wait()`/`yield()`, calls of room-level functions and natives.
* Object names follow the identifier rules (§2.3). They must be unique in the file and cannot be
  the name of a function, native or reserved name. Handler names must be unique within their
  object; the same handler name may appear in several objects and as a room-level function.

### 8.2 Room resources and room scripts

* A room's objects — their identifiers, sprites, positions and state — are defined by the **room
  resource**, not by the script. The object name of a declaration is the object's identifier in
  the room resource (`object blender:` belongs to the room object with id `"blender"`).
* An object declaration **does not create an object**. It is a named scope of functions, not a
  class: no instances, constructors, inheritance or member variables.
* One room script holds the room's functions and the handlers of all its objects. There are no
  per-object script files, and the room resource lists no events or handlers.
* An object of the room resource without a declaration (or without a handler for some event)
  simply has no script behaviour for it.

### 8.3 Dispatch

The engine (the adventure runtime) invokes a handler by **object identifier and event name**,
for example `("blender", "use")`, with the arguments the event provides and the engine's handle
of the object:

* The event name is the handler name. Any name can be used: the language and the VM have no
  fixed list of events (`look_at`, `use`, `use_with`, `inspect` … are conventions of the game).
* If the script declares no such object or handler, nothing happens. **A missing handler is not
  an error.**
* A handler runs as a coroutine (§9), so it may `wait()` or `yield()`. A runtime error inside a
  handler is reported like any other (§10), with the handler named `object.handler`.
* The arguments must match the handler's parameters, as for any call.
* There is no fallback: no default handler, and no automatic dispatch to the other object of a
  "use A with B" interaction. The engine decides which handler to invoke.

The engine API is `ScriptHost::dispatch` ([Runtime.md](Runtime.md#object-handlers-and-dispatch)).

### 8.4 self and the object type

* Inside a handler, `self` is the object the handler runs for: the engine's handle of that
  object, a value of type `object`. Different objects — also the "same" object in two instances
  of a room — have different handles, so handlers never share `self`.
* `self` is available only in handlers ("'self' is only available inside object handlers"
  elsewhere). It cannot be assigned, declared or used as a parameter name.
* `self` stays valid for the whole run of the handler, also across `wait()` and `yield()`. If the
  engine destroys the object meanwhile, the handle does not move to another object: the next
  native call with it is a runtime error.
* `object` is also the name of the type in annotations, parameters and native signatures:
  `func use_with(item: object):`, `extern func get_flag(o: object, name: string) -> bool`.
* Object values can be stored in variables, passed and returned, and compared with `==`/`!=`.
  They have no operators otherwise, no conversions, and no members: object state is owned by the
  engine and accessed through natives that take an `object` (`self.name` property syntax is not
  part of version 1.1).

### 8.5 Scopes

| Scope | Contains |
|---|---|
| Script (room) | room-level functions, natives (`extern func`, prelude) and object names |
| Object | the handlers of one object |
| Function | parameters and variables (§7) |
| Handler context | the implicit `self` |

A handler calls room-level functions and natives by name. Handlers are not in the script scope:
a room-level function cannot call a handler, and a handler cannot call another handler (not even
of its own object) — put shared code in a room-level function. Room-level functions are started
by the engine by name as before; handlers only through dispatch.

## 9. Coroutines

Script functions run as **coroutines** on the game thread, managed by the engine's scheduler
([Runtime.md](Runtime.md#scheduler)). The engine starts a coroutine by calling a room-level function
(usually `main`) or by dispatching an object handler (§8.3); several coroutines run concurrently, interleaved cooperatively:

* A coroutine runs until it calls `wait()` or `yield()`, finishes, fails, or uses up its
  instruction budget for the current frame. Then the next coroutine runs.
* Waiting never blocks the game: a waiting coroutine costs nothing until its time has come.
* `wait()` measures **game time**: the sum of the frame deltas the engine gives the scheduler
  (clamped like all game time, and paused while the application is suspended).
* A coroutine that waits or yields resumes in a later frame, never in the same one.
* Coroutines run in the order they were started; within one frame the order is deterministic.

## 10. Runtime errors

A runtime error stops the coroutine (other coroutines continue). The error names the script
file, function, line and column, and the call stack. Runtime errors are:

| Error | Cause |
|---|---|
| Integer overflow | `int` arithmetic out of range (§4.2) |
| Division by zero | §4.2 |
| Cannot convert float … to int | `int()` of NaN, infinity or out-of-range float |
| range() step must not be zero | §5.2 |
| Call depth limit exceeded | unbounded recursion (default limit 200) |
| String too long | a string beyond 16 MiB |
| Native function error | an engine function reported an error, e.g. `wait(-1.0)` |
| Execution limit exceeded | the coroutine executed more instructions in one frame than its budget (default 100 000), e.g. an endless loop without `wait`/`yield` |

There is no exception handling: errors cannot be caught by scripts.

## 11. Differences from Python

Deliberate, so that scripts stay predictable and fast to check:

* Static types, checked before running; no dynamic typing and no truthiness (`if count:` is an
  error, write `if count > 0:`).
* Only tabs indent, one tab per level.
* `True/False/None` as in Python, but `None` is only a return type, not a value.
* Integers are 64-bit and overflow is an error (Python integers are unbounded).
* Variables are block-scoped and must be declared before use; no shadowing.
* `for` only iterates over `range(...)`; no `break`, `continue`, `else` on loops.
* Comparisons do not chain; `and`/`or` return `bool`, not an operand.
* No classes (`object` declarations are named groups of handlers, §8), closures, generators,
  decorators, exceptions, imports, lists, dicts, `**`, augmented assignment (`+=`), conditional
  expressions, f-strings or `global`.
* Function parameters require types; there are no default or keyword arguments.
* `wait`/`yield` are functions provided by the engine, not keywords.

## 12. Limitations (version 1.1)

* No `break`/`continue`, no `+=`, no compound data types (lists, dictionaries, structures).
* No modules or imports: one file is one program. No global (script-level) variables or
  constants, including for room scripts.
* Objects (§8): no `self.property` access (object state goes through natives), no variables in
  object declarations, no calls between handlers, no default or fallback handlers.
* Up to 255 parameters per function, 65 535 registers per function and 65 536 distinct constants
  per file.
* `print` output is the only built-in output; everything else comes from engine natives.

## Appendix: grammar

```text
module      := (NEWLINE | function | extern | object)* EOF
function    := "func" NAME "(" [params] ")" ["->" type] ":" block
object      := "object" NAME ":" NEWLINE INDENT function+ DEDENT
extern      := "extern" "func" NAME "(" [params] ")" ["->" type] NEWLINE
params      := param ("," param)* [","]
param       := NAME ":" type
type        := "int" | "float" | "bool" | "string" | "object" | "None"
block       := NEWLINE INDENT statement+ DEDENT
statement   := NAME [":" type] "=" expr NEWLINE
             | expr NEWLINE                                   (a call)
             | "if" expr ":" block ("elif" expr ":" block)* ["else" ":" block]
             | "while" expr ":" block
             | "for" NAME "in" "range" "(" expr ["," expr ["," expr]] ")" ":" block
             | "return" [expr] NEWLINE
             | "pass" NEWLINE
expr        := or
or          := and ("or" and)*
and         := not ("and" not)*
not         := "not" not | comparison
comparison  := sum [("==" | "!=" | "<" | "<=" | ">" | ">=") sum]
sum         := term (("+" | "-") term)*
term        := unary (("*" | "/" | "//" | "%") unary)*
unary       := "-" unary | primary
primary     := INT | FLOAT | STRING | "True" | "False" | "None" | "self"
             | NAME | NAME "(" [expr ("," expr)* [","]] ")" | "(" expr ")"
```
