// Language 2.0 types: var declarations, entity references, scoped enums, vec2, typed registers
// in the bytecode, and native API interfaces shared by the compiler and the runtime.

#include "ScriptTestSupport.h"

#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/Disassembler.h"

#include <functional>
#include <unordered_map>

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Runs main() optimized and unoptimized (through a .schb file); both must agree.
// `setup` registers the natives the script needs besides the core interface (each run gets a
// fresh registry: print writes to that run's output).
using Setup = std::function<void(NativeRegistry&)>;

std::string output(const std::string& source, const Setup& setup = {}, std::vector<const NativeInterface*> interfaces = {})
{
    RunConfig optimized;
    optimized.interfaces = interfaces;
    RunConfig unoptimized = optimized;
    unoptimized.optimize = false;
    const auto run = [&](const RunConfig& config) {
        NativeRegistry natives;
        if (setup) setup(natives);
        return describeRun(runScript(source, config, &natives));
    };
    const std::string a = run(optimized);
    const std::string b = run(unoptimized);
    if (a != b) {
        std::fprintf(stderr, "  optimized:\n%s  unoptimized:\n%s", a.c_str(), b.c_str());
        CHECK(false);
    }
    return a;
}

std::string body(const std::string& text) { return "func main():\n" + text; }

// Natives handing out entity references: actor(id) and object(id).
void addEntityNatives(NativeRegistry& natives)
{
    REQUIRE(natives
                .add({"find_actor", {ScriptType::Int}, ScriptType::Actor,
                      [](NativeContext&, std::span<const Value> arguments) {
                          return NativeResult::returnValue(Value::fromActor({static_cast<std::uint64_t>(arguments[0].asInt())}));
                      }})
                .has_value());
    REQUIRE(natives
                .add({"find_object", {ScriptType::Int}, ScriptType::Object,
                      [](NativeContext&, std::span<const Value> arguments) {
                          return NativeResult::returnValue(Value::fromObject({static_cast<std::uint64_t>(arguments[0].asInt())}));
                      }})
                .has_value());
}

const std::string entityExterns = "extern func find_actor(id: int) -> actor\nextern func find_object(id: int) -> object\n";

// A small interface standing in for an engine API (the adventure interface is declared the same way).
const NativeInterface& testInterface()
{
    static const NativeInterface interface = [] {
        NativeInterface result;
        result.name = "test";
        result.enums.push_back({"Mood", {"CALM", "HAPPY", "ANGRY"}});
        result.functions.push_back({"mood_of", {ScriptType::Int}, NativeType(ScriptType::Enum, "Mood"), NativeCategory::Immediate,
                                    false, "The mood of a character.", false});
        result.functions.push_back({"describe_mood", {NativeType(ScriptType::Enum, "Mood")}, ScriptType::String,
                                    NativeCategory::Immediate, false, "A word for a mood.", false});
        return result;
    }();
    return interface;
}

std::unordered_map<std::string, NativeImplementation> testImplementations()
{
    std::unordered_map<std::string, NativeImplementation> implementations;
    implementations["mood_of"] = [](NativeContext&, std::span<const Value> arguments) {
        return NativeResult::returnValue(Value::fromEnum(0, static_cast<std::uint32_t>(arguments[0].asInt() % 3)));
    };
    implementations["describe_mood"] = [](NativeContext&, std::span<const Value> arguments) {
        static const char* const words[] = {"calm", "happy", "angry"};
        return NativeResult::returnValue(Value::fromString(words[arguments[0].enumOrdinal()]));
    };
    return implementations;
}

Compiled withTestInterface(const std::string& source)
{
    return compileSource(source, CompileOptions{.optimize = true, .interfaces = {&testInterface()}});
}

} // namespace

// --- var ------------------------------------------------------------------------------------------

SCHUIMM_TEST(Language_VarDeclaresAndAssignmentAssigns)
{
    CHECK_EQ(output(body("\tvar a = 1\n\tvar b: float = 2\n\ta = a + 1\n\tb = b / 4\n\tprint(a)\n\tprint(b)\n")),
             std::string("2\n0.5\n"));
    const Compiled undeclared = compileSource(body("\tcount = 1\n"));
    CHECK(undeclared.has("Undefined variable 'count'", 2));
    CHECK(undeclared.hasNote("var count = ..."));
    CHECK(compileSource(body("\tvar a = 1\n\tvar a = 2\n")).has("already declared"));
    CHECK(compileSource(body("\tvar a = 1\n\tif True:\n\t\tvar a = 2\n")).has("already declared"));
    CHECK(compileSource(body("\tvar a: int\n")).has("must be initialised"));
    CHECK(compileSource(body("\ta: int = 1\n")).has("declared with 'var'"));
    CHECK(compileSource(body("\tvar a = print(1)\n")).has("has no value"));
    // Block scoping: a variable of a branch is gone after it.
    CHECK(compileSource(body("\tif True:\n\t\tvar inner = 1\n\tprint(inner)\n")).has("Undefined variable 'inner'"));
}

// --- Entity references ------------------------------------------------------------------------------

SCHUIMM_TEST(Language_EntityWideningAndReferenceEquality)
{
    const std::string source = entityExterns +
                               "func same(a: entity, b: entity) -> bool:\n\treturn a == b\n"
                               "func main():\n"
                               "\tvar hero = find_actor(3)\n"
                               "\tvar door: object = find_object(3)\n"
                               "\tvar anything: entity = hero\n"
                               "\tprint(same(hero, find_actor(3)))\n"
                               "\tprint(same(hero, door))\n" // Same id, different kinds: different entities.
                               "\tprint(anything == hero)\n"
                               "\tprint(hero != find_actor(4))\n"
                               "\tanything = door\n"
                               "\tprint(anything == door)\n"
                               "\tprint(hero)\n\tprint(door)\n";
    CHECK_EQ(output(source, addEntityNatives), std::string("True\nFalse\nTrue\nTrue\nTrue\nactor#3\nobject#3\n"));
}

SCHUIMM_TEST(Language_EntityTypeErrors)
{
    const auto errors = [](const std::string& text) { return compileSource(entityExterns + text); };
    // No narrowing: an entity may be either kind.
    CHECK(errors("func f(e: entity):\n\tvar a: actor = e\n").has("Cannot assign a value of type 'entity' to variable 'a' of type 'actor'"));
    CHECK(errors("func f(o: object):\n\tvar a: actor = o\n").has("of type 'object' to variable 'a' of type 'actor'"));
    CHECK(errors("func g(a: actor):\n\tpass\nfunc f(e: entity):\n\tg(e)\n").has("must be of type 'actor', got 'entity'"));
    CHECK(errors("func f(a: actor, o: object) -> bool:\n\treturn a == o\n").has("Operator '==' cannot be applied to 'actor' and 'object'"));
    CHECK(errors("func f(a: actor, o: object) -> bool:\n\treturn a == o\n").hasNote("never the same entity"));
    CHECK(errors("func f(a: actor) -> bool:\n\treturn a == 1\n").has("cannot be applied to 'actor' and 'int'"));
    CHECK(errors("func f(a: actor) -> bool:\n\treturn a < a\n").has("Operator '<'"));
    CHECK(errors("func f(e: entity) -> actor:\n\treturn e\n").has("Cannot return a value of type 'entity'"));
    // self is a room object.
    CHECK(errors("object door:\n\tfunc use():\n\t\tvar a: actor = self\n").has("of type 'object' to variable 'a' of type 'actor'"));
    CHECK(errors("object door:\n\tfunc use():\n\t\tvar e: entity = self\n\t\tvar o: object = self\n").result.success);
}

// --- Enums -----------------------------------------------------------------------------------------

SCHUIMM_TEST(Language_ScriptEnums)
{
    const std::string source = "enum Light:\n\tRED\n\tYELLOW\n\tGREEN\n"
                               "func next(l: Light) -> Light:\n"
                               "\tif l == Light.RED:\n\t\treturn Light.GREEN\n"
                               "\tif l == Light.GREEN:\n\t\treturn Light.YELLOW\n"
                               "\treturn Light.RED\n"
                               "func main():\n"
                               "\tvar light = Light.RED\n"
                               "\tfor i in range(4):\n\t\tprint(light)\n\t\tlight = next(light)\n"
                               "\tprint(light != Light.RED)\n";
    CHECK_EQ(output(source), std::string("Light.RED\nLight.GREEN\nLight.YELLOW\nLight.RED\nTrue\n"));
    // Enums may be declared after their use.
    CHECK(compileSource("func f(d: Side) -> bool:\n\treturn d == Side.LEFT\nenum Side:\n\tLEFT\n\tRIGHT\n").result.success);
}

SCHUIMM_TEST(Language_EnumErrors)
{
    const std::string light = "enum Light:\n\tRED\n\tGREEN\nenum Side:\n\tLEFT\n\tRIGHT\n";
    const auto errors = [&](const std::string& text) { return compileSource(light + text); };
    CHECK(errors("func f():\n\tvar l = Light.BLUE\n").has("Enum 'Light' has no member 'BLUE'"));
    CHECK(errors("func f():\n\tvar l = Light.BLUE\n").hasNote("The members are RED, GREEN."));
    CHECK(errors("func f():\n\tvar l: Light = 0\n").has("Cannot assign a value of type 'int' to variable 'l' of type 'Light'"));
    CHECK(errors("func f() -> int:\n\treturn Light.RED\n").has("Cannot return a value of type 'Light'"));
    CHECK(errors("func f() -> bool:\n\treturn Light.RED == Side.LEFT\n").has("cannot be applied to 'Light' and 'Side'"));
    CHECK(errors("func f() -> bool:\n\treturn Light.RED == 0\n").has("cannot be applied to 'Light' and 'int'"));
    CHECK(errors("func f() -> bool:\n\treturn Light.RED < Light.GREEN\n").has("Operator '<'"));
    CHECK(errors("func f():\n\tvar x = Light.RED + 1\n").has("Operator '+'"));
    CHECK(errors("func f():\n\tvar x = int(Light.RED)\n").has("cannot convert"));
    CHECK(errors("func f():\n\tprint(Light)\n").has("'Light' is an enum"));
    CHECK(errors("func f(x: Colour):\n\tpass\n").has("Unknown type 'Colour'"));
    CHECK(errors("func f():\n\tvar Light = 1\n").has("name of an enum"));
    CHECK(errors("func Light():\n\tpass\n").has("same name as an enum"));
    CHECK(compileSource("enum Side:\n\tLEFT\nobject Side:\n\tfunc use():\n\t\tpass\n").result.success); // Object names are the room's own.
    CHECK(compileSource("enum A:\n\tX\nenum A:\n\tY\n").has("Duplicate declaration of 'A'"));
    CHECK(compileSource("enum A:\n\tX\n\tX\n").has("Duplicate member 'X' in enum 'A'"));
    CHECK(compileSource("enum int:\n\tX\n").has("built-in name"));
    CHECK(compileSource("enum print:\n\tX\n").has("Duplicate declaration of 'print'"));
    CHECK(compileSource("enum A:\n\tX = 1\n").has("end of the line"));
    CHECK(compileSource("func f():\n\tenum A:\n\t\tX\n").has("only allowed at the top level"));
    CHECK(compileSource("enum A:\npass\n").has("indented list of members"));
}

// --- vec2 ------------------------------------------------------------------------------------------

SCHUIMM_TEST(Language_Vec2ArithmeticAndComponents)
{
    const std::string source = body("\tvar a = vec2(1, 2)\n"
                                    "\tvar b: vec2 = vec2(0.5, -1.5)\n"
                                    "\tprint(a + b)\n\tprint(a - b)\n\tprint(a * 2)\n\tprint(3 * a)\n\tprint(a / 4)\n\tprint(-a)\n"
                                    "\tprint(a.x + a.y)\n"
                                    "\tprint(a == vec2(1.0, 2.0))\n\tprint(a != b)\n"
                                    "\tvar c = a\n\tc.x = 10\n\tc.y = c.y * 3\n" // Value semantics: a is unchanged.
                                    "\tprint(a)\n\tprint(c)\n");
    CHECK_EQ(output(source), std::string("vec2(1.5, 0.5)\nvec2(0.5, 3.5)\nvec2(2.0, 4.0)\nvec2(3.0, 6.0)\nvec2(0.25, 0.5)\n"
                                         "vec2(-1.0, -2.0)\n3.0\nTrue\nTrue\nvec2(1.0, 2.0)\nvec2(10.0, 6.0)\n"));
    // Vectors passed to functions are copies.
    CHECK_EQ(output("func shift(v: vec2) -> vec2:\n\tv.x = v.x + 1\n\treturn v\n" +
                    body("\tvar p = vec2(0, 0)\n\tvar q = shift(p)\n\tprint(p)\n\tprint(q)\n")),
             std::string("vec2(0.0, 0.0)\nvec2(1.0, 0.0)\n"));
    // Division by zero is the float error; other non-finite results follow float arithmetic.
    CHECK(contains(output(body("\tvar zero = 0.0\n\tprint(vec2(1, 1) / zero)\n")), "Division by zero"));
    CHECK_EQ(output(body("\tprint(vec2(1e300, 0) * 1e300)\n")), std::string("vec2(inf, 0.0)\n"));
}

SCHUIMM_TEST(Language_Vec2CoreFunctions)
{
    CHECK_EQ(output(body("\tprint(length(vec2(3, 4)))\n"
                         "\tprint(distance(vec2(1, 1), vec2(4, 5)))\n"
                         "\tprint(normalize(vec2(0, -2)))\n"
                         "\tprint(normalize(vec2(0, 0)))\n"
                         "\tprint(lerp(vec2(0, 0), vec2(10, -10), 0.25))\n"
                         "\tprint(lerp(vec2(0, 0), vec2(10, 0), 2))\n")),
             std::string("5.0\n5.0\nvec2(0.0, -1.0)\nvec2(0.0, 0.0)\nvec2(2.5, -2.5)\nvec2(20.0, 0.0)\n"));
    // Pure natives: an unused call is removed by the optimizer.
    const Compiled c = compileSource(body("\tvar unused = length(vec2(1, 1))\n"));
    REQUIRE(c.result.success);
    CHECK(c.result.module.imports.empty());
}

SCHUIMM_TEST(Language_Vec2Errors)
{
    CHECK(compileSource(body("\tvar v = vec2(1)\n")).has("vec2() takes exactly 2 arguments"));
    CHECK(compileSource(body("\tvar v = vec2(\"a\", 1)\n")).has("Argument 1 of vec2() must be a number"));
    CHECK(compileSource(body("\tvar v = vec2(1, 2)\n\tprint(v.z)\n")).has("components 'x' and 'y', not 'z'"));
    CHECK(compileSource(body("\tvar v = vec2(1, 2)\n\tv.z = 1\n")).has("components 'x' and 'y', not 'z'"));
    CHECK(compileSource(body("\tvar n = 1\n\tprint(n.x)\n")).has("A value of type 'int' has no member 'x'"));
    CHECK(compileSource(body("\tvar n = 1\n\tn.x = 2\n")).has("has no component 'x'"));
    CHECK(compileSource(body("\tvar v = vec2(1, 2)\n\tv.x = \"s\"\n")).has("to component 'x' of type 'float'"));
    CHECK(compileSource(body("\tvar v = vec2(1, 2)\n\tprint(v * v)\n")).has("Operator '*'"));
    CHECK(compileSource(body("\tvar v = vec2(1, 2)\n\tprint(2 / v)\n")).has("Operator '/'"));
    CHECK(compileSource(body("\tvar v = vec2(1, 2)\n\tprint(v + 1)\n")).has("Operator '+'"));
    CHECK(compileSource(body("\tvar v = vec2(1, 2)\n\tprint(v < v)\n")).has("Operator '<'"));
    CHECK(compileSource(body("\tvar v = vec2(1, 2)\n\tprint(v // 2)\n")).has("Operator '//'"));
    CHECK(compileSource(body("\tvar v: vec2 = 1\n")).has("Cannot assign"));
    CHECK(compileSource(body("\tvec2(1, 2).x = 3\n")).has("component of a vec2 variable"));
    CHECK(compileSource(body("\tvar v = vec2(1, 2)\n\tprint(v.length())\n")).has("no methods"));
}

// --- Bytecode 2.0 ------------------------------------------------------------------------------------

namespace {

Module typedModule()
{
    const Compiled c = compileSource(entityExterns +
                                     "enum Light:\n\tRED\n\tGREEN\n"
                                     "func f(a: actor, e: entity, l: Light, v: vec2) -> bool:\n"
                                     "\tvar w = v * 2\n\tw.y = 1\n"
                                     "\treturn a == e and l == Light.GREEN and w != v\n"
                                     "func main():\n\tprint(f(find_actor(1), find_object(2), Light.RED, vec2(1, 2)))\n",
                                     false);
    if (!c.result.success) std::fprintf(stderr, "%s", c.diagnostics().c_str());
    REQUIRE(c.result.success);
    return c.result.module;
}

bool rejected(const Module& module, std::string_view text)
{
    const Status status = validateModule(module);
    if (status.has_value()) {
        std::fprintf(stderr, "  module accepted, expected \"%.*s\"\n", static_cast<int>(text.size()), text.data());
        return false;
    }
    if (status.error().message.find(text) == std::string::npos) {
        std::fprintf(stderr, "  got: %s\n", status.error().message.c_str());
        return false;
    }
    return true;
}

} // namespace

SCHUIMM_TEST(Bytecode2_TypedRegistersAndEnumsRoundTrip)
{
    const Module module = typedModule();
    REQUIRE(module.enums.size() == 1);
    CHECK(!module.enums[0].isEngine());
    CHECK_EQ(module.string(module.enums[0].nameIndex), std::string("Light"));
    const Function& f = module.functions[0];
    REQUIRE(f.parameters.size() == 4);
    CHECK(f.parameters[0] == TypeRef(ScriptType::Actor));
    CHECK(f.parameters[1] == TypeRef(ScriptType::Entity));
    CHECK(f.parameters[2] == TypeRef::enumType(0));
    CHECK(f.parameters[3] == TypeRef(ScriptType::Vec2));
    CHECK(f.registerCount() >= f.parameters.size());
    auto bytes = writeBytecode(module);
    REQUIRE(bytes.has_value());
    auto read = readBytecode(*bytes);
    REQUIRE(read.has_value());
    REQUIRE(read->enums.size() == 1);
    CHECK(read->enums[0].members == module.enums[0].members);
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        CHECK(read->functions[i].registerTypes == module.functions[i].registerTypes);
        CHECK(read->functions[i].code == module.functions[i].code);
    }
    const std::string listing = disassemble(*read);
    CHECK(contains(listing, "e0 Light: RED GREEN"));
    CHECK(contains(listing, "LoadEnum"));
    CHECK(contains(listing, "EqRef"));
    CHECK(contains(listing, "SetVec2Y"));
    // It runs, through the bytecode file.
    CHECK_EQ(output(entityExterns + "func main():\n\tvar a = find_actor(1)\n\tvar e: entity = a\n\tprint(a == e)\n", addEntityNatives),
             std::string("True\n"));
}

SCHUIMM_TEST(Bytecode2_ValidatorChecksRegisterTypes)
{
    const Module good = typedModule();
    REQUIRE(validateModule(good).has_value());
    const auto findInstruction = [](const Function& function, Opcode opcode) {
        for (std::size_t i = 0; i < function.code.size(); ++i) {
            if (function.code[i].opcode == opcode) return i;
        }
        return function.code.size();
    };
    Module m = good;
    Function& f = m.functions[0];
    // An enum ordinal outside the enum.
    const std::size_t load = findInstruction(f, Opcode::LoadEnum);
    REQUIRE(load < f.code.size());
    f.code[load].c = 7;
    CHECK(rejected(m, "has no member 7"));
    // An enum index outside the table.
    m = good;
    m.functions[0].code[load].b = 3;
    CHECK(rejected(m, "enum 3 does not exist"));
    // A register declared 'any' (only native parameters may be).
    m = good;
    m.functions[0].registerTypes.back() = ScriptType::Any;
    CHECK(rejected(m, "any"));
    // A parameter register whose type differs from the parameter.
    m = good;
    m.functions[0].registerTypes[0] = ScriptType::Object;
    CHECK(rejected(m, "parameter"));
    // Comparing an actor with an object.
    m = good;
    const std::size_t compare = findInstruction(m.functions[0], Opcode::EqRef);
    REQUIRE(compare < m.functions[0].code.size());
    m.functions[0].registerTypes[1] = ScriptType::Object;
    m.functions[0].parameters[1] = ScriptType::Object;
    CHECK(rejected(m, "actor reference is compared with an object reference"));
    // A vector instruction on a float register.
    m = good;
    const std::size_t multiply = findInstruction(m.functions[0], Opcode::MulVec2);
    REQUIRE(multiply < m.functions[0].code.size());
    std::swap(m.functions[0].code[multiply].b, m.functions[0].code[multiply].c);
    CHECK(rejected(m, "expected vec2"));
    // An enum member with an unknown name string.
    m = good;
    m.enums[0].members.push_back(9999);
    CHECK(rejected(m, "missing"));
    // An engine enum without a valid qualifier string.
    m = good;
    m.enums[0].qualifierIndex = 9999;
    CHECK(rejected(m, "missing"));
}

// --- Native interfaces --------------------------------------------------------------------------------

SCHUIMM_TEST(Interface_CoreInterfaceIsWellFormed)
{
    CHECK(checkInterface(coreInterface()).empty());
    CHECK(checkInterface(testInterface()).empty());
    const NativeDeclaration* wait = coreInterface().findFunction("wait");
    REQUIRE(wait != nullptr);
    CHECK(wait->category == NativeCategory::Suspending);
    CHECK_EQ(formatSignature(*wait), std::string("wait(float) -> None"));
    CHECK_EQ(formatSignature(*testInterface().findFunction("describe_mood")), std::string("describe_mood(Mood) -> string"));
    CHECK(coreInterface().findFunction("length")->pure);
    // Malformed interfaces.
    NativeInterface bad = testInterface();
    bad.functions.push_back(bad.functions[0]);
    CHECK(contains(checkInterface(bad), "duplicate function 'mood_of'"));
    bad = testInterface();
    bad.functions[1].parameters[0].enumName = "Weather";
    CHECK(contains(checkInterface(bad), "enum 'Weather', which the interface does not declare"));
    bad = testInterface();
    bad.enums[0].members.push_back("CALM");
    CHECK(contains(checkInterface(bad), "duplicate member 'CALM'"));
    bad = testInterface();
    bad.functions[0].returnType = ScriptType::Entity;
    CHECK(contains(checkInterface(bad), "cannot return entity"));
    bad = testInterface();
    bad.name = "not valid";
    CHECK(!checkInterface(bad).empty());
}

SCHUIMM_TEST(Interface_ScriptsUseInterfaceFunctionsAndEnums)
{
    const std::string source = "func main():\n"
                               "\tvar mood = mood_of(4)\n"
                               "\tprint(mood)\n"
                               "\tprint(mood == Mood.HAPPY)\n"
                               "\tprint(describe_mood(Mood.ANGRY))\n";
    // Without the interface the names do not exist.
    CHECK(compileSource(source).has("Undefined function 'mood_of'"));
    const Compiled c = withTestInterface(source);
    REQUIRE(c.result.success);
    REQUIRE(c.result.module.enums.size() == 1);
    CHECK(c.result.module.enums[0].isEngine());
    CHECK_EQ(c.result.module.string(c.result.module.enums[0].qualifierIndex), std::string("test"));
    const Setup bind = [](NativeRegistry& natives) { REQUIRE(natives.bind(testInterface(), testImplementations()).has_value()); };
    CHECK_EQ(output(source, bind, {&testInterface()}), std::string("Mood.HAPPY\nTrue\nangry\n"));
    // The core interface may be listed explicitly; it is not added twice.
    CHECK(compileSource(source, CompileOptions{.optimize = true, .interfaces = {&coreInterface(), &testInterface()}}).result.success);
}

SCHUIMM_TEST(Interface_ConflictingDeclarationsAreRejected)
{
    CHECK(withTestInterface("extern func mood_of(id: int) -> int\n").has("'mood_of' is a built-in function"));
    CHECK(withTestInterface("extern func mood_of(id: int) -> int\n").hasNote("native interface 'test'"));
    CHECK(withTestInterface("func describe_mood(m: Mood) -> string:\n\treturn \"\"\n").has("built-in function"));
    CHECK(compileSource("extern func print(value: string)\n").has("'print' is a built-in function"));
    CHECK(compileSource("func length(v: vec2) -> float:\n\treturn 0.0\n").has("'length' is a built-in function"));
    CHECK(withTestInterface("enum Mood:\n\tX\n").has("Duplicate declaration of 'Mood'"));
    CHECK(withTestInterface("enum Mood:\n\tX\n").hasNote("built-in enum"));
    CHECK(withTestInterface("object Mood:\n\tfunc use():\n\t\tpass\n").result.success); // Object names are the room's own.
    CHECK(withTestInterface("func main():\n\tvar mood_of = 1\n").has("name of a function"));
    // Two interfaces declaring the same names.
    NativeInterface other = testInterface();
    other.name = "other";
    const Compiled twice =
        compileSource("func main():\n\tpass\n", CompileOptions{.optimize = true, .interfaces = {&testInterface(), &other}});
    CHECK(twice.has("Enum 'Mood' is declared by both native interface 'test' and 'other'"));
    CHECK(twice.has("Function 'mood_of' is declared by both native interface 'test' and 'other'"));
    CHECK(compileSource("func main():\n\tpass\n", CompileOptions{.optimize = true, .interfaces = {&testInterface(), &testInterface()}})
              .has("supplied twice"));
    // extern func keeps working for names no interface declares.
    CHECK(withTestInterface("extern func ring(times: int)\nfunc main():\n\tring(2)\n").result.success);
}

SCHUIMM_TEST(Interface_RuntimeBindingIsValidated)
{
    NativeRegistry natives;
    auto implementations = testImplementations();
    implementations.erase("describe_mood");
    Status status = natives.bind(testInterface(), implementations);
    REQUIRE(!status.has_value());
    CHECK(contains(status.error().message, "no implementation for describe_mood(Mood) -> string"));
    CHECK(natives.find("mood_of") == nullptr); // A failed bind registers nothing.
    CHECK(natives.findEnum("test.Mood") == nullptr);
    implementations = testImplementations();
    implementations["unknown"] = implementations["mood_of"];
    status = natives.bind(testInterface(), implementations);
    REQUIRE(!status.has_value());
    CHECK(contains(status.error().message, "'unknown' is not declared by the interface"));
    REQUIRE(natives.bind(testInterface(), testImplementations()).has_value());
    const auto bound = natives.find("describe_mood");
    REQUIRE(bound != nullptr);
    CHECK_EQ(formatSignature(*bound), std::string("describe_mood(test.Mood) -> string"));
    CHECK_EQ(bound->interfaceName, std::string("test"));
    // Binding again: the names are taken.
    status = natives.bind(testInterface(), testImplementations());
    CHECK(!status.has_value());
    // A different interface reusing the qualified enum name with other members.
    NativeInterface changed;
    changed.name = "test";
    changed.enums.push_back({"Mood", {"CALM", "ANGRY"}});
    status = natives.bind(changed, {});
    REQUIRE(!status.has_value());
    CHECK(contains(status.error().message, "already registered with different members"));
}

SCHUIMM_TEST(Interface_LinkingChecksEngineEnumIdentity)
{
    const std::string source = "func main():\n\tprint(describe_mood(Mood.CALM))\n";
    const Compiled c = withTestInterface(source);
    REQUIRE(c.result.success);
    // An engine without the interface.
    NativeRegistry missing;
    REQUIRE(registerStandardNatives(missing, {}).has_value());
    auto linked = Program::link(c.result.module, missing);
    REQUIRE(!linked.has_value());
    CHECK(linked.error().code == ErrorCode::NotFound);
    CHECK(contains(linked.error().message, "uses engine enum 'test.Mood', which the engine does not provide"));
    // An engine whose enum has different members: the ordinals would mean something else.
    NativeInterface changed = testInterface();
    changed.enums[0].members = {"CALM", "ANGRY", "HAPPY"};
    NativeRegistry other;
    REQUIRE(registerStandardNatives(other, {}).has_value());
    REQUIRE(other.bind(changed, testImplementations()).has_value());
    linked = Program::link(c.result.module, other);
    REQUIRE(!linked.has_value());
    CHECK(contains(linked.error().message, "different version of enum 'test.Mood'"));
    // A native whose signature differs from the script's view of it.
    NativeInterface retyped = testInterface();
    retyped.functions[1].returnType = ScriptType::Int;
    NativeRegistry third;
    REQUIRE(registerStandardNatives(third, {}).has_value());
    REQUIRE(third.bind(retyped, testImplementations()).has_value());
    linked = Program::link(c.result.module, third);
    REQUIRE(!linked.has_value());
    CHECK(contains(linked.error().message, "declares native function describe_mood(Mood) -> string"));
    CHECK(contains(linked.error().message, "engine provides describe_mood(test.Mood) -> int"));
}

SCHUIMM_TEST(Interface_NativeEnumResultsAreChecked)
{
    // A native returning an ordinal outside the enum is a controlled native error.
    const Setup bind = [](NativeRegistry& natives) {
        auto implementations = testImplementations();
        implementations["mood_of"] = [](NativeContext&, std::span<const Value>) {
            return NativeResult::returnValue(Value::fromEnum(0, 9));
        };
        REQUIRE(natives.bind(testInterface(), implementations).has_value());
    };
    const std::string text = output("func main():\n\tprint(mood_of(1))\n", bind, {&testInterface()});
    CHECK(contains(text, "invalid enum ordinal 9"));
}
