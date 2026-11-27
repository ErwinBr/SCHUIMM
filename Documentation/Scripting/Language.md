# SCHUIMM Script — language specification (version 3.0)

SCHUIMM Script is a small, statically typed scripting language with Python-inspired syntax,
made for 2D adventure games: readable scripts that call engine functions and wait without
blocking the game. It is **not** Python; see [Differences from Python](#differences-from-python).

This document describes the language as implemented by `schuimmc` and the SCHUIMM runtime.
The language version (3.0) is recorded in every bytecode file and is independent of the bytecode
format version ([Bytecode.md](Bytecode.md)).

Version 3.0 compiles the scripts of a game into one program: functions, `extern func`
declarations and enums declared in any file can be used in every other file, without imports
(§1).

Version 2.0 added:
- `var` declarations, which are now required (§7);
- the entity reference types `actor`, `object` and `entity` (§3.3);
- scoped enums (§3.4);
- the vector type `vec2` (§3.5);
- native API interfaces: engine functions and enums a script can use without declaring them
  (§6.1);
- `spawn`, coroutine references, `cancel()` and waiting for engine operations (§9).

Scripts written for 1.x must be updated: add `var` to declarations and recompile them.

```python
# A complete script.
extern func play_chime()

func main():
	print("Welcome to SCHUIMM!")

	var counter = 0
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
* A file is a sequence of function declarations (`func`), native function declarations
  (`extern func`), object declarations (`object`, [§8](#8-objects-and-handlers)) and enum
  declarations (`enum`, [§3.4](#34-enums)). There are no global variables and no statements at
  the top level.
* **The scripts of a game form one program.** Every function, `extern func` and enum declared at
  the top level of any participating file is visible in all of them. There is no `import`,
  `export` or `global`, and no file of declarations: put a declaration where it belongs, for
  example a function about the kitchen in the kitchen's script.

  ```python
  # Scripts/Global.sch
  func award_points(amount: int):
  	print("Points: " + str(amount))
  ```

  ```python
  # Rooms/Kitchen/Kitchen.sch
  object oven:
  	func use():
  		award_points(10)        # declared in another file
  ```

  * Declarations may be used before or after their place, in the same or another file; the
    order of the files does not matter.
  * A name can be declared once in the whole game. A second declaration is an error naming both
    places, never "the later file wins". Identical `extern func` declarations in several files
    are fine; differing ones are an error.
* Every file has a **role**, given by whatever refers to it (the build):
  * a *global* script holds game-wide declarations only (`func`, `extern func`, `enum`);
  * a *room* script may also hold the `object` declarations of its room;
  * an *actor* script holds the declarations of an actor's script.
  A file compiled on its own (`schuimmc file.sch`) is a room script.
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
* Keywords: `func extern object enum var self if elif else while for in return pass and or not
  True False None spawn`.
* Reserved names (not keywords, but they cannot be redefined or used as variable or function
  names): the type names `int float bool string vec2 entity actor coroutine operation`, the
  conversion functions `int float str`, the constructor `vec2`, and `range`.
* The functions and enums of the native interfaces the script is compiled with (§6.1), such as
  `print`, `wait` and `length`, cannot be redeclared either.
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

`+ - * / // % == != < <= > >= = ( ) , : -> .` and the keyword operators `and or not`.

## 3. Types

| Type | Values |
|---|---|
| `int` | signed 64-bit integers, −2⁶³ … 2⁶³−1 |
| `float` | IEEE 754 binary64 (including ±infinity and NaN, which can arise from arithmetic) |
| `bool` | `True`, `False` |
| `string` | immutable UTF-8 text |
| `vec2` | a 2D vector of two `float` components, `x` and `y` (§3.5) |
| `actor` | a reference to an actor (§3.3) |
| `object` | a reference to a room object, e.g. `self` in a handler (§3.3, [§8.4](#84-self-and-the-object-type)) |
| `entity` | a reference to an actor or a room object (§3.3) |
| enum types | the members of a scoped enum, e.g. `Direction.NORTH` (§3.4) |
| `coroutine` | a reference to one running invocation, made by `spawn` (§9.2) |
| `operation` | a reference to one engine operation, e.g. an animation playback, returned by engine functions (§9.4) |
| `None` | the "no value" type: the result of a function without a return type |

`None` is not a value a variable can hold. It is the return type of functions declared without
`->`, and a call of such a function can only be used as a statement.

### 3.1 Type inference and annotations

* A variable declared without a type gets the type of its initialising expression:
  `var size = 100` is `int`, `var speed = 3.5` is `float`, `var has_key = True` is `bool`,
  `var name = "Alex"` is `string`, `var spot = vec2(10, 20)` is `vec2`.
* An annotation states the type explicitly: `var size: float = 100`. The initialiser must be
  assignable to it (§3.2). A declaration without an initialiser is an error.
* Parameter types are mandatory: `func f(amount: int, scale: float)`.
* The return type follows `->`; without it the function returns `None`.

### 3.2 Conversions

There are two implicit conversions, both widening: **int → float**, and **actor → entity** and
**object → entity** (§3.3). They apply where the wider type is expected and the narrower one is
given: assignment, arguments and `return`. There is no implicit narrowing.

Int → float also applies to operands. It applies where a `float` is expected and an `int` is
given:

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
There is no conversion to `bool` and no truthiness: conditions must be `bool`. References, enums
and vectors have no conversions to other types.

### 3.3 Entity references: actor, object and entity

Scripts refer to actors and room objects through references, which the engine hands out: `self`
in a handler, or the result of an engine function. A script cannot make a reference up.

| Type | Refers to |
|---|---|
| `actor` | an actor |
| `object` | a room object |
| `entity` | an actor or a room object |

* `actor` and `object` widen implicitly to `entity`. An `entity` never narrows: it cannot be
  assigned to an `actor` or `object` variable or passed for such a parameter.
* An `actor` and an `object` are different types; neither is assignable to the other.
* References can be stored in variables, passed, returned and compared with `==` and `!=`
  (§4.3). They have no other operators, no members and no conversions.
* A reference only names an entity. The entity's state belongs to the engine and is read and
  changed through engine functions. A reference to an entity that no longer exists does not move
  to another entity: an engine function given it reports an error.

### 3.4 Enums

```python
enum Light:
	RED
	YELLOW
	GREEN

func next(light: Light) -> Light:
	if light == Light.RED:
		return Light.GREEN
	return Light.RED
```

* `enum Name:` declares a scoped enum at the top level, followed by an indented list of member
  names, one per line. An enum has at least one member; member names are unique within it.
* Members are always written with the enum's name: `Light.RED`. There are no unqualified
  members.
* An enum is a type of its own. Its values compare with `==` and `!=` to values of the same enum
  only. They have no order, no arithmetic and no conversion to or from `int`.
* Enums may be used before their declaration. Their names share the top-level namespace with
  functions, natives and objects.
* Native interfaces can declare enums too (engine enums, §6.1), for example `Direction` of the
  adventure API. Scripts use them like their own enums but cannot redeclare them. The engine
  checks an engine enum's identity (its interface, name and members) when the script is loaded,
  so its members always mean the same thing.

### 3.5 vec2

`vec2` is a value type of two `float` components:

```python
var start = vec2(100, 240)        # ints widen to float
var step = vec2(2.5, 0) * 4
var target = start + step
target.y = target.y - 10
print(target)                     # vec2(110.0, 230.0)
print(distance(start, target))
```

* `vec2(x, y)` makes a vector; both arguments are numbers (ints widen).
* `v.x` and `v.y` read the components (type `float`). `v.x = expression` sets a component of a
  `vec2` variable.
* Vectors are values: assigning or passing one copies it, so changing a copy never changes the
  original.
* Operators:

  | Expression | Result |
  |---|---|
  | `a + b`, `a - b` | componentwise, both `vec2` |
  | `v * s`, `s * v`, `v / s` | each component times / divided by the number `s` |
  | `-v` | both components negated |
  | `a == b`, `a != b` | componentwise float equality |

  `v / 0` is the float division-by-zero error. Other results follow float arithmetic (they may
  be infinite or NaN). There is no `v * v`, `v // s` or ordering.
* The core interface provides `length(v)`, `distance(a, b)`, `normalize(v)` and
  `lerp(a, b, t)` (§6.2).

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
| 8 | call `f(...)`, member `.name`, parentheses | left |

So `-a * b` is `(-a) * b`, `-v.x` is `-(v.x)`, `not a == b` is `not (a == b)`, and
`a or b and c` is `a or (b and c)`. Only named functions can be called (`f(x)`, not `f(x)(y)`),
and values have no methods (`v.length()` is an error; write `length(v)`).

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
| `-x` | `int` / `float` / `vec2` | same type; `int` negation is checked |
| `+` | `string, string` | concatenation |
| `+ -` | `vec2, vec2` | `vec2` (§3.5) |
| `*` | `vec2` and a number, either order | `vec2` |
| `/` | `vec2, number` | `vec2` |

**Integer overflow** — any `int` result outside −2⁶³ … 2⁶³−1 (including `-(-2⁶³)` and
`-2⁶³ // -1`) — is a **runtime error** ("Integer overflow"). Integers never wrap around.

**Division by zero** — `//`, `%` with an `int` divisor of 0 and `/`, `//`, `%` with a divisor of
0 or 0.0 — is a runtime error, for `int` and `float` alike.

Other `float` operations follow IEEE 754 (overflow gives ±infinity; `inf - inf` gives NaN).

### 4.3 Comparisons

* `==` and `!=` compare two values of the same type, or an `int` with a `float` (the `int` is
  widened). `bool`, `string`, `vec2` and each enum compare only with their own type. `None`,
  `coroutine` and `operation` values cannot be compared.
* References (§3.3) compare with references: two references are equal when they refer to the
  same entity. An `entity` compares with an `actor`, an `object` or an `entity`. Comparing an
  `actor` with an `object` is an error, since they can never be the same entity.
* `< <= > >=` compare numbers (with widening) or two strings. Strings compare byte-wise by their
  UTF-8 encoding (which equals code point order). Booleans, references, enums and vectors are not
  ordered.
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
| Declaration | `var name = expression` or `var name: type = expression` (§7) |
| Assignment | `name = expression`; `name.x = expression` or `name.y = …` for a `vec2` variable |
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

Engine functions are implemented in C++ by the game. A script sees them in two ways.

**Native interfaces.** The engine declares groups of functions, with their enums, as *native
interfaces*. Their functions and enums can be used without any declaration:
- the **core interface** is available to every script (§6.2);
- further interfaces are given to the compiler for the scripts that need them, for example the
  adventure API for room scripts (`schuimmc --interface adventure`). The build passes them
  automatically for the scripts of a game.

The compiler and the engine use the same declaration of each interface, so their signatures
always agree. A script cannot redeclare an interface function or enum, with `func`, `extern func`,
`object` or `enum`.

**`extern func`.** A script may also declare an engine function that no interface provides:

```python
extern func play_sound(name: string, volume: float)
extern func player_health() -> int
```

The declaration gives the signature the compiler checks calls against.

**Loading.** When the bytecode is loaded, every *called* native is looked up by name in the
engine's registry, and its signature must match exactly. Every engine enum the script uses must
exist in the engine with the same members. Otherwise loading fails
([Runtime.md](Runtime.md#native-functions)).

### 6.2 Built-in functions

Always available without declaration. `int`, `float`, `str` and `vec2` are built into the
language; the others are the core interface:

| Function | Meaning |
|---|---|
| `print(value)` | Writes the text of any value (not `None`) to the script output (the engine log and, in the demo, the on-screen console). |
| `wait(seconds: float)` | Suspends the current coroutine for `seconds` of game time. `wait(0)` resumes at the next update. Negative, NaN, infinite or > 10⁹ s is a runtime error. |
| `yield()` | Suspends the current coroutine until the next scheduler update (normally the next frame). |
| `wait(op: operation)` | Suspends the current coroutine until the engine operation `op` completes (§9.4). |
| `cancel(task: coroutine)` | Stops a coroutine (§9.3). |
| `length(v: vec2) -> float` | The length of `v`. |
| `distance(a: vec2, b: vec2) -> float` | `length(b - a)`. |
| `normalize(v: vec2) -> vec2` | `v` scaled to length 1; `vec2(0, 0)` if `v` has length 0 or a non-finite length (never NaN). |
| `lerp(a: vec2, b: vec2, t: float) -> vec2` | `a + (b - a) * t`; `t` is not clamped. |
| `int(x)`, `float(x)`, `str(x)` | Conversions (§3.2). |
| `vec2(x, y)` | A vector (§3.5). |

### 6.3 Text of values

`print` and `str` produce:
- integers in decimal (`-42`);
- floats as the shortest decimal that reads back to the same value, always with `.` or an
  exponent, independent of the system locale. Decimal exponents from −4 to 15 are written
  positionally, others with an exponent: `1.0`, `10.0`, `0.1`, `2.5e-07`, `1e+20`, `inf`,
  `-inf`, `nan`, `-0.0`;
- booleans as `True`/`False`;
- strings unchanged.

Only `print` accepts these:
- vectors as `vec2(1.0, 2.5)`;
- enum values as `Light.RED`;
- references as `actor#` or `object#` followed by the engine id (`object#7`);
- coroutine references as `coroutine#` and an internal id, operations by their state
  (`operation(pending)`, `operation(completed)`).

`str()` does not accept them.

## 7. Variables and scope

* A variable is **declared with `var`**: `var x = 1` or `var x: float = 1`. It always has a value:
  there are no uninitialised variables.
* `x = expression` without `var` assigns an existing variable. Assigning a name that is not
  declared is an error ("Undefined variable 'x'"), with a hint to declare it with `var`.
* Scopes are **lexical and block-based**: a variable declared in a block (function body, `if`
  branch, loop body) is visible from its declaration to the end of that block, including nested
  blocks. It is *not* visible after the block.
* The value of an assignment must be assignable to the variable's type (`count = "many"` for an
  `int` variable is an error).
* **No shadowing**: declaring a name that is already visible (a `var` declaration, a `for` loop
  variable or a parameter name) is an error. Sibling blocks may reuse a name.
* Variables cannot use the name of a function, native, enum or reserved name.
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
  statements, `wait()`/`yield()`, calls of global functions and natives.
* `object` declarations appear only in room scripts (§1).
* Object names follow the identifier rules (§2.3) and cannot be a reserved name. They must be
  unique in the file. They belong to their room: they are not game-wide names, so another room
  may have an object of the same name, and an object may be named like a function. Handler
  names must be unique within their object; the same handler name may appear in several objects
  and as a global function.

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
* `object` is the type of room-object references (§3.3) in annotations, parameters and native
  signatures: `func use_with(item: object):`, `extern func get_flag(o: object, name: string) -> bool`.
  A handler may pass `self` where an `entity` is expected.
* Object references follow §3.3: they can be stored, passed, returned and compared, and have no
  members. Object state is owned by the engine and accessed through engine functions
  (`self.name` property syntax is not part of version 3.0).

### 8.5 Scopes

| Scope | Contains |
|---|---|
| Game | the global functions, natives (interfaces and `extern func`) and enums of every file (§1) |
| Script (room) | the room's object names |
| Object | the handlers of one object |
| Function | parameters and variables (§7) |
| Handler context | the implicit `self` |

A handler calls global functions and natives by name. Handlers are not in the game scope: a
global function cannot call a handler, and a handler cannot call another handler (not even of
its own object) — put shared code in a global function. Global functions are started by the
engine by name; handlers only through dispatch.

## 9. Coroutines

Script functions run as **coroutines** on the game thread, managed by the engine's scheduler
([Runtime.md](Runtime.md#scheduler)). The engine starts a coroutine by calling a global function
(usually `main`) or by dispatching an object handler (§8.3); several coroutines run concurrently, interleaved cooperatively:

* A coroutine runs until it calls `wait()` or `yield()`, finishes, fails, or uses up its
  instruction budget for the current frame. Then the next coroutine runs.
* Waiting never blocks the game: a waiting coroutine costs nothing until its time has come.
* `wait()` measures **game time**: the sum of the frame deltas the engine gives the scheduler
  (clamped like all game time, and paused while the application is suspended).
* A coroutine that waits or yields resumes in a later frame, never in the same one.
* Coroutines run in the order they were started; within one frame the order is deterministic.

### 9.1 Calls are sequential

An ordinary call runs the function to its end before the caller continues. If the function waits,
the caller waits with it: a call never creates a coroutine.

```python
patrol(guard)              # returns only when patrol returns
walk_to(hero, vec2(500, 800))
```

Engine functions behave the same way. Some return at once (`set_position`), some suspend the
caller until the action they started is done (`walk_to`), and some start an operation in the
background and return a reference to it (`play_animation`). Which one a function is belongs to
its contract ([Runtime.md](Runtime.md#native-functions)); calls look the same.

### 9.2 spawn

`spawn f(arguments)` starts a call as a new, independent coroutine and continues at once:

```python
func start_scene(guard: actor, hero: actor):
	var task: coroutine = spawn patrol(guard)
	spawn walk_to(guard, vec2(900, 600))    # an engine function, spawned directly
	walk_to(hero, vec2(500, 800))
```

* The callee is a script function or an engine function. The built-ins `int`, `float`, `str`,
  `vec2` and `range`, handlers, and engine functions with an `any` parameter (`print`) cannot be
  spawned.
* The arguments are type-checked as for a call and evaluated **once**, in the spawning
  coroutine, before the new one is scheduled.
* The new coroutine has its own parameters, variables and call stack. It first runs in the next
  frame, after the coroutines that already exist, in spawn order. It never runs inside the
  spawning coroutine.
* `spawn` is an expression of type `coroutine`; it may also stand alone as a statement.
* A spawned coroutine is independent: it keeps running after the coroutine that spawned it
  returns or is cancelled. It belongs to the same owner as the coroutine that spawned it, e.g. the
  room whose handler spawned it: when the room is destroyed, it is cancelled too.
* A spawned function's result is discarded. There is no joining (`wait(coroutine)`) in version
  2.0.

### 9.3 Coroutine references and cancel

A `coroutine` value identifies one invocation: two spawns of the same function give different
references. It can be stored, passed and returned, but not compared or printed into strings.

`cancel(task)` stops the coroutine. It never runs another statement, and everything it held is
released: its frames and values, and its wait. Cancelling a coroutine that has ended, or
cancelling it twice, does nothing. A reference never points to another invocation, even after
the coroutine is gone.

A coroutine can be stopped while it runs, for example by an engine function it called that
unloads its room. It stops when that call returns.

### 9.4 Engine operations and wait(operation)

Some engine functions start an operation and return an `operation` reference to that one
operation (`var playback: operation = play_animation(hero, "wave")`).

`wait(op)` suspends the calling coroutine until the operation ends:
- **completed:** the coroutine continues. If the operation has already completed, `wait` returns
  at once;
- **cancelled or replaced** (for example by another animation): the waiting coroutine is
  cancelled.

An operation that never ends by itself (a looping animation) cannot be waited for: `wait(op)` is
a runtime error. Only the calling coroutine waits; the game, other scripts and the operation
itself go on.

The reference stays valid after the operation has ended, so a later `wait` sees the outcome of
*this* operation even if others have started since. Operation references do not keep entities
alive.

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
* Variables are declared with `var`, are block-scoped and must be declared before use; no
  shadowing.
* `for` only iterates over `range(...)`; no `break`, `continue`, `else` on loops.
* Comparisons do not chain; `and`/`or` return `bool`, not an operand.
* Enums are scoped and not integers; `vec2` is a built-in value type.
* No classes (`object` declarations are named groups of handlers, §8), closures, generators,
  decorators, exceptions, imports, lists, dicts, `**`, augmented assignment (`+=`), conditional
  expressions, f-strings or `global`.
* Function parameters require types; there are no default or keyword arguments.
* `wait`/`yield` are functions provided by the engine, not keywords.

## 12. Limitations (version 3.0)

* No `break`/`continue`, no `+=`, no compound data types (lists, dictionaries, structures)
  besides `vec2`.
* No spawned blocks, closures or function values; no joining of coroutines (`wait(coroutine)`),
  no coroutine groups or structured concurrency.
* No modules or imports: the files of a game form one program (§1). No global (script-level)
  variables or constants, including for room scripts.
* Objects (§8): no `self.property` access (object state goes through natives), no variables in
  object declarations, no calls between handlers, no default or fallback handlers.
* Up to 255 parameters per function, 65 535 registers per function and 65 536 distinct constants
  per file.
* `print` output is the only built-in output; everything else comes from engine natives.

## Appendix: grammar

```text
module      := (NEWLINE | function | extern | object | enum)* EOF
function    := "func" NAME "(" [params] ")" ["->" type] ":" block
object      := "object" NAME ":" NEWLINE INDENT function+ DEDENT
extern      := "extern" "func" NAME "(" [params] ")" ["->" type] NEWLINE
enum        := "enum" NAME ":" NEWLINE INDENT (NAME NEWLINE)+ DEDENT
params      := param ("," param)* [","]
param       := NAME ":" type
type        := "int" | "float" | "bool" | "string" | "vec2" | "actor" | "object" | "entity"
             | "coroutine" | "operation" | "None" | NAME            (NAME: an enum)
block       := NEWLINE INDENT statement+ DEDENT
statement   := "var" NAME [":" type] "=" expr NEWLINE
             | NAME ["." NAME] "=" expr NEWLINE                    (assignment)
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
unary       := "-" unary | postfix
postfix     := primary ("." NAME)*
primary     := INT | FLOAT | STRING | "True" | "False" | "None" | "self"
             | "spawn" NAME "(" [expr ("," expr)* [","]] ")"
             | NAME | NAME "(" [expr ("," expr)* [","]] ")" | "(" expr ")"
```
