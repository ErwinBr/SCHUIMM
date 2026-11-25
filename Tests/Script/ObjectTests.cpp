// Object declarations: parsing, scopes and diagnostics, handler registration, dispatch through
// the scheduler and the host, the self context (also across suspension) and runtime errors.

#include "ScriptTestSupport.h"

#include "Core/Logging/Logger.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Script/Bytecode/Disassembler.h"
#include "Script/Compiler/AstPrinter.h"
#include "Script/Runtime/ScriptHost.h"

#include <map>
#include <set>

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

std::string readText(const std::string& path)
{
    auto bytes = fs::readFile(path);
    REQUIRE(bytes.has_value());
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

std::string kitchenSource() { return readText(std::string(SCHUIMM_SCRIPT_EXAMPLES_DIR) + "/kitchen.sch"); }

constexpr ObjectRef banana{100};
constexpr ObjectRef spoon{101};

// A stand-in for the adventure runtime: object state keyed by ObjectRef (the engine owns it,
// not the script), say() output, and an inventory lookup returning object handles.
struct Room {
    NativeRegistry natives;
    std::vector<std::string> output;
    std::vector<std::string> sounds;
    std::map<std::uint64_t, std::map<std::string, bool>> flags;
    std::set<std::uint64_t> destroyed; // Ids are never reused: a destroyed id stays stale.
    ScriptScheduler scheduler;
    std::vector<ScriptError> errors;

    explicit Room(SchedulerSettings settings = {})
        : scheduler(settings)
    {
        REQUIRE(registerStandardNatives(natives, [this](std::string_view, std::string_view text) {
                    output.emplace_back(text);
                }).has_value());
        add({"say", {ScriptType::String}, ScriptType::None, [this](NativeContext&, std::span<const Value> arguments) {
                 output.emplace_back(arguments[0].asString());
                 return NativeResult::returnValue();
             },
             {}});
        add({"get_flag", {ScriptType::Object, ScriptType::String}, ScriptType::Bool,
             [this](NativeContext&, std::span<const Value> arguments) {
                 if (destroyed.contains(arguments[0].asObject().id)) return NativeResult::error("stale object handle");
                 const auto& object = flags[arguments[0].asObject().id];
                 const auto found = object.find(std::string(arguments[1].asString()));
                 return NativeResult::returnValue(Value::fromBool(found != object.end() && found->second));
             },
             {}});
        add({"set_flag", {ScriptType::Object, ScriptType::String, ScriptType::Bool}, ScriptType::None,
             [this](NativeContext&, std::span<const Value> arguments) {
                 if (destroyed.contains(arguments[0].asObject().id)) return NativeResult::error("stale object handle");
                 flags[arguments[0].asObject().id][std::string(arguments[1].asString())] = arguments[2].asBool();
                 return NativeResult::returnValue();
             },
             {}});
        add({"inventory_item", {ScriptType::String}, ScriptType::Object, [](NativeContext&, std::span<const Value> arguments) {
                 return NativeResult::returnValue(Value::fromObject(arguments[0].asString() == "banana" ? banana : spoon));
             },
             {}});
        add({"play_sound", {ScriptType::String, ScriptType::Float}, ScriptType::None,
             [this](NativeContext&, std::span<const Value> arguments) {
                 sounds.emplace_back(arguments[0].asString());
                 return NativeResult::returnValue();
             },
             {}});
        scheduler.setEventCallback([this](const CoroutineEvent& event) {
            if (event.kind == CoroutineEvent::Kind::Failed && event.error != nullptr) errors.push_back(*event.error);
        });
    }

    void add(NativeFunction function) { REQUIRE(natives.add(std::move(function)).has_value()); }

    std::shared_ptr<const Program> load(const std::string& source, bool optimize = true)
    {
        const Compiled c = compileSource(source, optimize, "kitchen.sch");
        if (!c.result.success) std::fprintf(stderr, "%s", c.diagnostics().c_str());
        REQUIRE(c.result.success);
        auto bytes = writeBytecode(c.result.module);
        REQUIRE(bytes.has_value());
        auto program = Program::load(*bytes, natives);
        if (!program) std::fprintf(stderr, "  load error: %s\n", program.error().message.c_str());
        REQUIRE(program.has_value());
        return *program;
    }

    DispatchResult dispatch(const std::shared_ptr<const Program>& program, std::string_view object, std::string_view event,
                            ObjectRef self, std::vector<Value> arguments = {})
    {
        auto result = scheduler.dispatch(program, object, event, self, std::move(arguments));
        if (!result) std::fprintf(stderr, "  dispatch error: %s\n", result.error().message.c_str());
        REQUIRE(result.has_value());
        return *result;
    }

    void runUntilIdle(double dt = 0.5)
    {
        for (int i = 0; i < 100 && scheduler.activeCount() > 0; ++i) scheduler.update(dt);
    }

    std::string joined() const
    {
        std::string text;
        for (const std::string& line : output) text += line + "\n";
        return text;
    }
};

const char* const twoObjects =
    "func room_name() -> string:\n"
    "\treturn \"kitchen\"\n"
    "object blender:\n"
    "\tfunc look_at():\n"
    "\t\tprint(\"blender \" + room_name())\n"
    "\t\tprint(self)\n"
    "\tfunc use():\n"
    "\t\tprint(\"use blender\")\n"
    "object cupboard:\n"
    "\tfunc look_at():\n"
    "\t\tprint(\"cupboard\")\n"
    "\t\tprint(self)\n";

} // namespace

// --- Parsing and compilation ----------------------------------------------------------------------

SCHUIMM_TEST(Objects_OneHandler)
{
    const Compiled c = compileSource("object blender:\n\tfunc look_at():\n\t\tprint(\"A blender.\")\n");
    REQUIRE(c.result.success);
    REQUIRE(c.result.ast->objects.size() == 1);
    const ObjectDecl& object = *c.result.ast->objects[0];
    CHECK_EQ(object.name, std::string("blender"));
    CHECK_EQ(object.nameRange.begin.line, 1u);
    REQUIRE(object.handlers.size() == 1);
    CHECK(object.handlers[0]->isHandler);
    CHECK(c.result.ast->functions.empty()); // Handlers are not room-level functions.
    CHECK_EQ(dumpAst(*c.result.ast), std::string("Object blender\n  Func look_at() -> None\n    ExprStmt\n"
                                                 "      Call print\n        String \"A blender.\"\n"));
    // The handler is an ordinary function whose hidden parameter 0 is self.
    REQUIRE(c.result.module.functions.size() == 1);
    CHECK(c.result.module.functions[0].parameters == std::vector<ScriptType>{ScriptType::Object});
    REQUIRE(c.result.module.objects.size() == 1);
    CHECK_EQ(c.result.module.string(c.result.module.objects[0].nameIndex), std::string("blender"));
}

SCHUIMM_TEST(Objects_SeveralObjectsHandlersAndRoomFunctions)
{
    const Compiled c = compileSource(kitchenSource());
    if (!c.result.success) std::fprintf(stderr, "%s", c.diagnostics().c_str());
    REQUIRE(c.result.success);
    const SemanticModel& semantics = c.result.semantics;
    REQUIRE(semantics.objects().size() == 2);
    const ObjectSemantics& blender = semantics.objects()[0];
    REQUIRE(blender.handlers.size() == 3);
    CHECK_EQ(blender.handlers[0].name, std::string("look_at"));
    CHECK_EQ(blender.handlers[1].name, std::string("use"));
    CHECK_EQ(blender.handlers[2].name, std::string("use_with"));
    CHECK_EQ(semantics.objects()[1].handlers.size(), std::size_t{1});
    // Room-level functions first, then the handlers, each linked to its object.
    REQUIRE(semantics.functions().size() == 6);
    CHECK(!semantics.functions()[0].isHandler());
    CHECK(!semantics.functions()[1].isHandler());
    CHECK_EQ(semantics.functions()[blender.handlers[2].function].object, 0u);
    CHECK_EQ(semantics.functions()[semantics.objects()[1].handlers[0].function].object, 1u);
    // use_with(item: object) has self plus its declared parameter.
    const FunctionSemantics& useWith = semantics.functions()[blender.handlers[2].function];
    REQUIRE(useWith.variables.size() >= 2);
    CHECK_EQ(useWith.variables[0].name, std::string("self"));
    CHECK(useWith.variables[0].type == Type::Object);
    CHECK_EQ(useWith.variables[1].name, std::string("item"));
    // IR dump and disassembly show handlers as object.handler.
    CHECK(dumpIr(c.result.ir).find("func blender.use_with(r0: object, r1: object) -> None") != std::string::npos);
    const std::string listing = disassemble(c.result.module);
    CHECK(listing.find("objects:\n  blender: look_at -> f2 use -> f3 use_with -> f4\n  cupboard: look_at -> f5\n") !=
          std::string::npos);
    CHECK(listing.find("function f4 blender.use_with(object, object) -> None") != std::string::npos);
}

SCHUIMM_TEST(Objects_HandlerNamesAreScopedToTheirObject)
{
    // The same handler name in two objects and as a room-level function is fine.
    const Compiled c = compileSource("func use():\n\tprint(\"room\")\n"
                                     "object a:\n\tfunc use():\n\t\tuse()\n"
                                     "object b:\n\tfunc use():\n\t\tpass\n");
    REQUIRE(c.result.success);
    // A handler cannot call a sibling handler directly.
    const Compiled sibling = compileSource("object a:\n\tfunc look_at():\n\t\tpass\n\tfunc use():\n\t\tlook_at()\n");
    CHECK(sibling.has("Undefined function 'look_at'", 5));
    CHECK(sibling.hasNote("is a handler of object 'a'"));
    // Room functions cannot call handlers either.
    CHECK(compileSource("object a:\n\tfunc use():\n\t\tpass\nfunc main():\n\tuse()\n").has("Undefined function 'use'", 5));
}

SCHUIMM_TEST(Objects_HandlersWithParametersAndControlFlow)
{
    const Compiled c = compileSource("object a:\n"
                                     "\tfunc count(n: int, label: string) -> int:\n"
                                     "\t\ttotal = 0\n"
                                     "\t\tfor i in range(n):\n"
                                     "\t\t\tif i % 2 == 0:\n"
                                     "\t\t\t\ttotal = total + i\n"
                                     "\t\t\telif i == 3:\n"
                                     "\t\t\t\twhile total < 100:\n"
                                     "\t\t\t\t\ttotal = total * 2 + 1\n"
                                     "\t\t\telse:\n"
                                     "\t\t\t\tpass\n"
                                     "\t\tprint(label)\n"
                                     "\t\treturn total\n");
    if (!c.result.success) std::fprintf(stderr, "%s", c.diagnostics().c_str());
    CHECK(c.result.success);
    // Every path of a handler with a result must return.
    CHECK(compileSource("object a:\n\tfunc f() -> int:\n\t\tpass\n").has("Function 'a.f' must return a value", 2));
}

SCHUIMM_TEST(Objects_IndentationUsesTabs)
{
    CHECK(compileSource("object a:\n    func f():\n        pass\n").has("Indentation must use tabs"));
    CHECK(compileSource("object a:\n\t\tfunc f():\n\t\t\tpass\n").has("Unexpected indentation", 2));
    CHECK(compileSource("object a:\n\tfunc f():\n\tpass\n").has("Expected an indented block for the function body", 3));
    CHECK(compileSource("object a: func f():\n").has("Expected a new line after ':' to start the body of object 'a'", 1));
}

SCHUIMM_TEST(Objects_EmptyObjectIsAnError)
{
    CHECK(compileSource("object a:\n").has("Expected an indented block of handler functions for object 'a'"));
    CHECK(compileSource("object a:\nfunc main():\n\tpass\n").has("Expected an indented block of handler functions", 2));
    const Compiled pass = compileSource("object a:\n\tpass\n");
    CHECK(pass.has("Only function declarations are allowed in object 'a'", 2));
}

SCHUIMM_TEST(Objects_DuplicatesAndNameClashes)
{
    const Compiled object = compileSource("object a:\n\tfunc f():\n\t\tpass\nobject a:\n\tfunc g():\n\t\tpass\n");
    CHECK(object.has("Duplicate declaration of object 'a'", 4));
    CHECK(object.hasNote("first declared on line 1"));
    const Compiled handler = compileSource("object a:\n\tfunc f():\n\t\tpass\n\tfunc f(x: int):\n\t\tpass\n");
    CHECK(handler.has("Duplicate handler 'f' in object 'a'", 4));
    CHECK(handler.hasNote("first declared on line 2"));
    CHECK(compileSource("func a():\n\tpass\nobject a:\n\tfunc f():\n\t\tpass\n").has("Object 'a' has the same name as a function", 3));
    CHECK(compileSource("object print:\n\tfunc f():\n\t\tpass\n").has("same name as a function"));
    CHECK(compileSource("object int:\n\tfunc f():\n\t\tpass\n").has("built-in name"));
    CHECK(compileSource("object a:\n\tfunc f(x: int, x: int):\n\t\tpass\n").has("Duplicate parameter 'x'", 2));
    CHECK(compileSource("object 7:\n\tfunc f():\n\t\tpass\n").has("Expected identifier for the object name", 1));
    CHECK(compileSource("object a\n\tfunc f():\n\t\tpass\n").has("Expected ':' after the object name", 1));
}

SCHUIMM_TEST(Objects_SelfOnlyInsideHandlers)
{
    const Compiled outside = compileSource("func main():\n\tprint(self)\n");
    CHECK(outside.has("'self' is only available inside object handlers", 2));
    CHECK(compileSource("object a:\n\tfunc f():\n\t\tself = 1\n").has("'self' cannot be assigned", 3));
    CHECK(compileSource("object a:\n\tfunc f(self: object):\n\t\tpass\n").has("for a parameter name", 2));
    CHECK(compileSource("object a:\n\tfunc f():\n\t\tself: int = 1\n").has("Expected the end of the line", 3));
    // self is an object: comparable with == and !=, nothing else.
    CHECK(compileSource("object a:\n\tfunc f(o: object) -> bool:\n\t\tother = self\n\t\treturn self == o or other != o\n")
              .result.success);
    CHECK(compileSource("object a:\n\tfunc f():\n\t\tx = self + 1\n").has("cannot be applied to 'object' and 'int'"));
    CHECK(compileSource("object a:\n\tfunc f():\n\t\tx = self < self\n").has("Operator '<' cannot be applied"));
    CHECK(compileSource("object a:\n\tfunc f():\n\t\tx = str(self)\n").has("cannot convert a value of type 'object'"));
    CHECK(compileSource("object a:\n\tfunc f():\n\t\tif self:\n\t\t\tpass\n").has("must be of type 'bool'"));
}

SCHUIMM_TEST(Objects_UnsupportedLocations)
{
    CHECK(compileSource("func main():\n\tobject a:\n\t\tfunc f():\n\t\t\tpass\n").has("only allowed at the top level", 2));
    CHECK(compileSource("object a:\n\tobject b:\n\t\tfunc f():\n\t\t\tpass\n").has("Object declarations cannot be nested", 2));
    CHECK(compileSource("object a:\n\textern func say(text: string)\n").has("Extern functions cannot be declared inside object 'a'", 2));
    CHECK(compileSource("object a:\n\tx = 1\n").has("Only function declarations are allowed in object 'a'", 2));
    CHECK(compileSource("object a:\n\tfunc f():\n\t\tfunc g():\n\t\t\tpass\n").has("cannot be declared inside other functions", 3));
    // Errors in one object do not hide errors in the next.
    const Compiled two = compileSource("object a:\n\tx = 1\n\tfunc f():\n\t\ty = undefined\nobject b:\n\tfunc g():\n\t\tprint(self)\n");
    CHECK(two.has("Only function declarations", 2));
    CHECK(!two.result.success);
}

SCHUIMM_TEST(Objects_ObjectTypeInSignatures)
{
    // "object" names the type of handles in annotations and extern declarations.
    const Compiled c = compileSource("extern func find_object(name: string) -> object\n"
                                     "func same(a: object, b: object) -> bool:\n"
                                     "\treturn a == b\n"
                                     "object a:\n"
                                     "\tfunc f():\n"
                                     "\t\tother: object = find_object(\"x\")\n"
                                     "\t\tprint(same(self, other))\n");
    REQUIRE(c.result.success);
    CHECK(c.result.module.imports[0].returnType == ScriptType::Object);
    CHECK(compileSource("func f(o: obj):\n\tpass\n").hasNote("int, float, bool, string and object"));
}

// --- Runtime: dispatch ------------------------------------------------------------------------------

SCHUIMM_TEST(Objects_DispatchInvokesTheHandler)
{
    Room room;
    const auto program = room.load(kitchenSource());
    CHECK(program->hasObject("blender"));
    CHECK(program->hasHandler("blender", "look_at"));
    CHECK(program->hasHandler("cupboard", "look_at"));
    CHECK(!program->hasHandler("cupboard", "use"));
    const DispatchResult result = room.dispatch(program, "blender", "look_at", ObjectRef{1});
    CHECK(result.handled);
    CHECK(room.scheduler.state(result.handle) == CoroutineState::Ready);
    room.runUntilIdle();
    CHECK(room.scheduler.state(result.handle) == CoroutineState::Finished);
    CHECK_EQ(room.joined(), std::string("An old blender.\n"));
    CHECK(room.errors.empty());
    // Room-level functions remain startable by name.
    REQUIRE(room.scheduler.start(program, "enter").has_value());
    room.runUntilIdle();
    CHECK_EQ(room.output.back(), std::string("Welcome to the kitchen."));
}

SCHUIMM_TEST(Objects_MissingHandlerIsNotAnError)
{
    Room room;
    const auto program = room.load(kitchenSource());
    const std::size_t before = room.scheduler.size();
    for (const auto& [object, event] : std::vector<std::pair<std::string, std::string>>{
             {"cupboard", "use"}, {"cupboard", "inspect"}, {"window", "look_at"}, {"", ""}, {"blender", "use_with "}}) {
        auto result = room.scheduler.dispatch(program, object, event, ObjectRef{1});
        REQUIRE(result.has_value());
        CHECK(!result->handled);
        CHECK(!result->handle.isValid());
    }
    CHECK_EQ(room.scheduler.size(), before); // No coroutine was created.
    room.runUntilIdle();
    CHECK(room.errors.empty());
    CHECK(room.output.empty());
    // A handler cannot be started without its object: it is not a room-level function.
    auto started = room.scheduler.start(program, "look_at");
    REQUIRE(!started.has_value());
    CHECK(started.error().code == ErrorCode::NotFound);
    CHECK(!room.scheduler.dispatch(nullptr, "blender", "use", ObjectRef{1}).has_value());
}

SCHUIMM_TEST(Objects_HandlersReceiveArguments)
{
    Room room;
    const auto program = room.load(kitchenSource());
    CHECK(room.dispatch(program, "blender", "use_with", ObjectRef{1}, {Value::fromObject(banana)}).handled);
    CHECK(room.dispatch(program, "blender", "use_with", ObjectRef{1}, {Value::fromObject(spoon)}).handled);
    room.runUntilIdle();
    CHECK_EQ(room.joined(), std::string("A banana smoothie sounds good.\nThat won't work.\n"));
    // Arguments are checked against the declared parameters (self is not counted).
    auto missing = room.scheduler.dispatch(program, "blender", "use_with", ObjectRef{1});
    REQUIRE(!missing.has_value());
    CHECK(missing.error().code == ErrorCode::InvalidArgument);
    CHECK(missing.error().message.find("'blender.use_with' takes 1 arguments, got 0") != std::string::npos);
    auto wrongType = room.scheduler.dispatch(program, "blender", "use_with", ObjectRef{1}, {Value::fromInt(3)});
    REQUIRE(!wrongType.has_value());
    CHECK(wrongType.error().message.find("Argument 1 of 'blender.use_with' must be object, got int") != std::string::npos);
    auto extra = room.scheduler.dispatch(program, "blender", "look_at", ObjectRef{1}, {Value::fromInt(3)});
    REQUIRE(!extra.has_value());
}

SCHUIMM_TEST(Objects_EachInvocationGetsItsOwnSelf)
{
    Room room;
    const auto program = room.load(twoObjects);
    room.dispatch(program, "blender", "look_at", ObjectRef{7});
    room.dispatch(program, "cupboard", "look_at", ObjectRef{8});
    room.dispatch(program, "blender", "look_at", ObjectRef{9}); // Another room instance's blender.
    room.runUntilIdle();
    CHECK(room.errors.empty());
    CHECK_EQ(room.joined(), std::string("blender kitchen\nobject#7\ncupboard\nobject#8\nblender kitchen\nobject#9\n"));
}

SCHUIMM_TEST(Objects_StateBelongsToTheEngineObject)
{
    // Two instances of the same room script: their blenders are different engine objects, so
    // the state the natives keep for them is separate.
    Room room;
    const auto program = room.load(kitchenSource());
    room.dispatch(program, "blender", "use", ObjectRef{10});
    room.scheduler.update(0.0);
    CHECK(room.flags[10]["is_running"]);
    CHECK(!room.flags[20]["is_running"]);
    room.dispatch(program, "blender", "use", ObjectRef{20});
    room.scheduler.update(0.5);
    CHECK(room.flags[20]["is_running"]);
    CHECK_EQ(room.sounds.size(), std::size_t{2});
}

SCHUIMM_TEST(Objects_SelfSurvivesSuspension)
{
    Room room;
    const auto program = room.load(kitchenSource());
    const ObjectRef blender{42};
    const DispatchResult first = room.dispatch(program, "blender", "use", blender);
    room.scheduler.update(0.0); // Switches on, plays the sound and waits 2 s.
    CHECK(room.scheduler.state(first.handle) == CoroutineState::Waiting);
    CHECK(room.flags[42]["is_running"]);
    // While the first invocation is suspended, the same handler runs again for the same object.
    room.dispatch(program, "blender", "use", blender);
    room.scheduler.update(0.5);
    CHECK_EQ(room.joined(), std::string("It's already running.\n"));
    for (int i = 0; i < 5; ++i) room.scheduler.update(0.5);
    // After resuming, self still refers to object 42.
    CHECK(room.scheduler.state(first.handle) == CoroutineState::Finished);
    CHECK(!room.flags[42]["is_running"]);
    CHECK_EQ(room.flags.count(0), std::size_t{0});
    CHECK_EQ(room.joined(), std::string("It's already running.\nThe blender stops.\n"));
    CHECK(room.errors.empty());
}

SCHUIMM_TEST(Objects_StaleHandleAfterSuspensionIsARuntimeError)
{
    // The engine destroys the blender while its handler waits. Ids are never reused, so on
    // resumption self is a stale handle (not another object), and the native reports it.
    Room room;
    const auto program = room.load(kitchenSource());
    const DispatchResult result = room.dispatch(program, "blender", "use", ObjectRef{42});
    room.scheduler.update(0.0);
    REQUIRE(room.scheduler.state(result.handle) == CoroutineState::Waiting);
    room.destroyed.insert(42);
    room.runUntilIdle();
    CHECK(room.scheduler.state(result.handle) == CoroutineState::Failed);
    REQUIRE(room.errors.size() == 1);
    CHECK(room.errors[0].message.find("stale object handle") != std::string::npos);
    CHECK(describe(room.errors[0]).find("blender.use") != std::string::npos);
    CHECK_EQ(room.joined(), std::string()); // "The blender stops." never ran.
}

SCHUIMM_TEST(Objects_DispatchRejectsTheNullHandle)
{
    Room room;
    const auto program = room.load(twoObjects);
    auto result = room.scheduler.dispatch(program, "blender", "look_at", ObjectRef{});
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ErrorCode::InvalidArgument);
    // Checked before the lookup: a null handle is a host error even for a missing handler.
    CHECK(!room.scheduler.dispatch(program, "blender", "dance", ObjectRef{0}).has_value());
    CHECK_EQ(room.scheduler.activeCount(), std::size_t{0});
}

SCHUIMM_TEST(Objects_SelfSurvivesSuspensionInNestedCalls)
{
    Room room;
    const auto program = room.load("func pause():\n\twait(1.0)\n\tyield()\n"
                                   "object door:\n"
                                   "\tfunc open(times: int):\n"
                                   "\t\tfor i in range(times):\n"
                                   "\t\t\tpause()\n"
                                   "\t\t\tprint(self)\n");
    room.dispatch(program, "door", "open", ObjectRef{3}, {Value::fromInt(2)});
    room.dispatch(program, "door", "open", ObjectRef{4}, {Value::fromInt(1)});
    room.runUntilIdle(0.25);
    CHECK(room.errors.empty());
    CHECK_EQ(room.joined(), std::string("object#3\nobject#4\nobject#3\n"));
}

SCHUIMM_TEST(Objects_RuntimeErrorsInHandlersAreReported)
{
    Room room;
    const auto program = room.load("func divide(n: int) -> int:\n"
                                   "\treturn 10 // n\n"
                                   "object scale:\n"
                                   "\tfunc weigh(n: int):\n"
                                   "\t\tprint(divide(n))\n");
    const DispatchResult ok = room.dispatch(program, "scale", "weigh", ObjectRef{1}, {Value::fromInt(5)});
    const DispatchResult bad = room.dispatch(program, "scale", "weigh", ObjectRef{1}, {Value::fromInt(0)});
    CHECK(ok.handled);
    CHECK(bad.handled); // Found; the failure happens while it runs.
    room.runUntilIdle();
    CHECK(room.scheduler.state(ok.handle) == CoroutineState::Finished);
    CHECK(room.scheduler.state(bad.handle) == CoroutineState::Failed);
    REQUIRE(room.errors.size() == 1);
    const ScriptError& error = room.errors[0];
    CHECK_EQ(error.message, std::string("Division by zero"));
    CHECK_EQ(error.function, std::string("divide"));
    CHECK_EQ(error.position.line, 2u);
    REQUIRE(error.stack.size() == 2);
    CHECK_EQ(error.stack[1].function, std::string("scale.weigh"));
    CHECK_EQ(error.stack[1].position.line, 5u);
    CHECK(describe(error).find("called from 'scale.weigh' (line 5)") != std::string::npos);
    CHECK_EQ(room.joined(), std::string("2\n"));
}

SCHUIMM_TEST(Objects_HandlersObeyTheInstructionBudget)
{
    SchedulerSettings settings;
    settings.instructionBudget = 1000;
    Room room(settings);
    const auto program = room.load("object clock:\n\tfunc tick():\n\t\twhile True:\n\t\t\tpass\n");
    const DispatchResult result = room.dispatch(program, "clock", "tick", ObjectRef{1});
    room.scheduler.update(0.1);
    CHECK(room.scheduler.state(result.handle) == CoroutineState::Failed);
    REQUIRE(room.errors.size() == 1);
    CHECK(room.errors[0].kind == ScriptErrorKind::ExecutionLimit);
    CHECK_EQ(room.errors[0].function, std::string("clock.tick"));
}

SCHUIMM_TEST(Objects_SameResultsOptimizedAndUnoptimized)
{
    std::string results[2];
    for (int optimize = 0; optimize < 2; ++optimize) {
        Room room;
        const auto program = room.load(kitchenSource(), optimize == 1);
        room.dispatch(program, "blender", "look_at", ObjectRef{1});
        room.dispatch(program, "blender", "use", ObjectRef{1});
        room.dispatch(program, "blender", "use_with", ObjectRef{1}, {Value::fromObject(banana)});
        room.dispatch(program, "cupboard", "look_at", ObjectRef{2});
        room.runUntilIdle();
        CHECK(room.errors.empty());
        results[optimize] = room.joined();
    }
    CHECK_EQ(results[0], results[1]);
    CHECK_EQ(results[1], std::string("An old blender.\nA banana smoothie sounds good.\nA wooden cupboard.\nThe blender stops.\n"));
}

SCHUIMM_TEST(Objects_ScriptHostDispatch)
{
    Logger logger(LogLevel::Off);
    ScriptHost host(logger);
    std::vector<std::string> output;
    host.setOutputCallback([&](std::string_view, std::string_view text) { output.emplace_back(text); });
    const Compiled c = compileSource(twoObjects);
    REQUIRE(c.result.success);
    auto bytes = writeBytecode(c.result.module);
    REQUIRE(bytes.has_value());
    auto program = host.load(*bytes, "room.sch");
    REQUIRE(program.has_value());
    auto handled = host.dispatch(*program, "cupboard", "look_at", ObjectRef{5});
    REQUIRE(handled.has_value());
    CHECK(handled->handled);
    auto missing = host.dispatch(*program, "cupboard", "use", ObjectRef{5});
    REQUIRE(missing.has_value());
    CHECK(!missing->handled);
    CHECK(!host.dispatch(*program, "cupboard", "look_at", ObjectRef{5}, {Value::fromInt(1)}).has_value());
    host.update(0.0);
    CHECK_EQ(output.size(), std::size_t{2});
    CHECK_EQ(output[1], std::string("object#5"));
    CHECK_EQ(host.statistics().started, std::uint64_t{1});
    CHECK_EQ(host.statistics().finished, std::uint64_t{1});
    CHECK_EQ(host.statistics().failed, std::uint64_t{0});
}
