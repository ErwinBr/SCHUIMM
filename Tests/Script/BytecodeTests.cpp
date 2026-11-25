#include "ScriptTestSupport.h"

#include "Script/Bytecode/BytecodeFormat.h"
#include "Script/Bytecode/Crc32.h"
#include "Script/Bytecode/Disassembler.h"
#include "Script/Runtime/ScriptScheduler.h"

#include <cstring>

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

const char* sampleSource =
    "extern func give(item: string, count: int) -> bool\n"
    "func main():\n"
    "\tname = \"key\"\n"
    "\tfor i in range(3):\n"
    "\t\tif give(name, i):\n"
    "\t\t\tprint(helper(i, 2.5))\n"
    "\twait(0.5)\n"
    "func helper(n: int, f: float) -> float:\n"
    "\treturn n * f\n";

Module sampleModule()
{
    const Compiled c = compileSource(sampleSource);
    REQUIRE(c.result.success);
    return c.result.module;
}

std::vector<std::byte> sampleBytes()
{
    auto bytes = writeBytecode(sampleModule());
    REQUIRE(bytes.has_value());
    return *bytes;
}

void putU16(std::vector<std::byte>& data, std::size_t offset, std::uint16_t value)
{
    data[offset] = static_cast<std::byte>(value & 0xFF);
    data[offset + 1] = static_cast<std::byte>(value >> 8);
}

void putU32(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i) data[offset + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFF);
}

std::uint32_t getU32(const std::vector<std::byte>& data, std::size_t offset)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(data[offset + i]) << (8 * i);
    return value;
}

// Recomputes the header checksum so a test reaches the checks behind it.
void fixCrc(std::vector<std::byte>& data)
{
    putU32(data, 28, crc32(std::span<const std::byte>(data).subspan(format::headerSize)));
}

// Offset of section `id` in a well-formed file.
std::size_t sectionOffset(const std::vector<std::byte>& data, format::SectionId id)
{
    for (std::size_t i = 0; i < format::sectionCount; ++i) {
        const std::size_t entry = format::headerSize + i * format::sectionEntrySize;
        if (getU32(data, entry) == static_cast<std::uint32_t>(id)) return getU32(data, entry + 4);
    }
    return 0;
}

std::size_t sectionEntry(format::SectionId id) { return format::headerSize + (static_cast<std::size_t>(id) - 1) * format::sectionEntrySize; }

bool failsWith(const std::vector<std::byte>& data, std::string_view text, ErrorCode code = ErrorCode::InvalidData)
{
    const auto module = readBytecode(data);
    if (module.has_value()) {
        std::fprintf(stderr, "  unexpectedly valid\n");
        return false;
    }
    if (module.error().code != code || module.error().message.find(text) == std::string::npos) {
        std::fprintf(stderr, "  error was: %s\n", describe(module.error()).c_str());
        return false;
    }
    return true;
}

// Writes a modified module without validation (the writer only encodes).
std::vector<std::byte> encode(const Module& module)
{
    auto bytes = writeBytecode(module);
    REQUIRE(bytes.has_value());
    return *bytes;
}

} // namespace

SCHUIMM_TEST(Bytecode_RoundTrip)
{
    const Module original = sampleModule();
    const std::vector<std::byte> bytes = encode(original);
    CHECK(std::memcmp(bytes.data(), "SCHB", 4) == 0);
    auto read = readBytecode(bytes);
    REQUIRE(read.has_value());
    CHECK(read->strings == original.strings);
    REQUIRE(read->functions.size() == original.functions.size());
    for (std::size_t i = 0; i < original.functions.size(); ++i) {
        CHECK(read->functions[i].code == original.functions[i].code);
        CHECK(read->functions[i].positions == original.functions[i].positions);
        CHECK(read->functions[i].registerCount == original.functions[i].registerCount);
        CHECK(read->functions[i].parameters == original.functions[i].parameters);
    }
    REQUIRE(read->imports.size() == 3); // give, print and wait: only natives that are called
    CHECK_EQ(read->constants.size(), original.constants.size());
    CHECK_EQ(read->sourceName(), std::string("test.sch"));
    // Writing the re-read module gives identical bytes.
    CHECK(encode(*read) == bytes);
    // The disassembly names functions, natives and source lines.
    const std::string listing = disassemble(*read);
    CHECK(listing.find("function f1 helper(int, float) -> float") != std::string::npos);
    CHECK(listing.find("CallNative") != std::string::npos);
    CHECK(listing.find("; line 9") != std::string::npos);
}

SCHUIMM_TEST(Bytecode_IsIndependentOfTheHost)
{
    // Little-endian fields at fixed offsets: the header can be checked byte by byte.
    const std::vector<std::byte> bytes = sampleBytes();
    CHECK(static_cast<int>(bytes[4]) == bytecodeFormatMajor);
    CHECK(static_cast<int>(bytes[5]) == 0);
    CHECK(static_cast<int>(bytes[8]) == languageVersionMajor);
    CHECK_EQ(getU32(bytes, 20), static_cast<std::uint32_t>(bytes.size()));
    CHECK_EQ(getU32(bytes, 24), format::sectionCount);
    // Compiling the same source twice produces identical files.
    CHECK(sampleBytes() == bytes);
}

SCHUIMM_TEST(Bytecode_VersionChecks)
{
    std::vector<std::byte> bytes = sampleBytes();
    putU16(bytes, 4, bytecodeFormatMajor + 1);
    CHECK(failsWith(bytes, "format 2." + std::to_string(bytecodeFormatMinor) + " is not supported", ErrorCode::Unsupported));
    bytes = sampleBytes();
    putU16(bytes, 6, bytecodeFormatMinor + 1);
    CHECK(failsWith(bytes, "not supported", ErrorCode::Unsupported));
    bytes = sampleBytes();
    putU16(bytes, 8, languageVersionMajor + 1);
    CHECK(failsWith(bytes, "language version", ErrorCode::Unsupported));
    bytes = sampleBytes();
    bytes[0] = static_cast<std::byte>('X');
    CHECK(failsWith(bytes, "bad magic"));
    bytes = sampleBytes();
    putU32(bytes, 16, 1);
    CHECK(failsWith(bytes, "header flags"));
}

SCHUIMM_TEST(Bytecode_TruncatedFilesAreRejected)
{
    const std::vector<std::byte> bytes = sampleBytes();
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length));
        if (readBytecode(truncated).has_value()) {
            std::fprintf(stderr, "  truncated file of %zu bytes accepted\n", length);
            CHECK(false);
        }
        if (truncated.size() >= format::headerSize) {
            // Even with a consistent size field and checksum the sections no longer fit.
            putU32(truncated, 20, static_cast<std::uint32_t>(truncated.size()));
            fixCrc(truncated);
            CHECK(!readBytecode(truncated).has_value());
        }
    }
    std::vector<std::byte> longer = bytes;
    longer.push_back(std::byte{0});
    CHECK(failsWith(longer, "header declares"));
}

SCHUIMM_TEST(Bytecode_ChecksumDetectsCorruption)
{
    std::vector<std::byte> bytes = sampleBytes();
    bytes[bytes.size() - 3] ^= std::byte{0x10};
    CHECK(failsWith(bytes, "checksum mismatch"));
}

SCHUIMM_TEST(Bytecode_InvalidOpcodesAndReservedBytes)
{
    std::vector<std::byte> bytes = sampleBytes();
    const std::size_t code = sectionOffset(bytes, format::SectionId::Code) + 4;
    bytes[code] = static_cast<std::byte>(0xEE);
    fixCrc(bytes);
    CHECK(failsWith(bytes, "unknown opcode 238"));
    bytes = sampleBytes();
    bytes[code + 1] = std::byte{1};
    fixCrc(bytes);
    CHECK(failsWith(bytes, "reserved instruction byte"));
}

SCHUIMM_TEST(Bytecode_InvalidRegistersTargetsAndIndices)
{
    Module module = sampleModule();
    Function& main = module.functions[0];
    const std::uint16_t count = main.registerCount;

    Module m = module;
    m.functions[0].code[0] = {Opcode::Move, count, 0, 0};
    CHECK(failsWith(encode(m), "register"));

    m = module;
    m.functions[0].code[0] = {Opcode::LoadConst, 0, static_cast<std::uint16_t>(m.constants.size()), 0};
    CHECK(failsWith(encode(m), "constant"));

    m = module;
    Instruction jump{Opcode::Jump, 0, 0, 0};
    jump.setTarget(static_cast<std::uint32_t>(m.functions[0].code.size()));
    m.functions[0].code[0] = jump;
    CHECK(failsWith(encode(m), "branch target"));
    jump.setTarget(0x10000u + 3); // high half used
    m.functions[0].code[0] = jump;
    CHECK(failsWith(encode(m), "branch target"));

    m = module;
    m.functions[0].code[0] = {Opcode::Call, noRegister, 7, 0};
    CHECK(failsWith(encode(m), "function 7 does not exist"));

    m = module;
    m.functions[0].code[0] = {Opcode::CallNative, noRegister, 9, 0};
    CHECK(failsWith(encode(m), "native import 9 does not exist"));

    m = module; // helper(int, float) -> float called with an argument window running off the end
    m.functions[0].code[0] = {Opcode::Call, 0, 1, static_cast<std::uint16_t>(count - 1)};
    CHECK(failsWith(encode(m), "argument registers"));

    m = module; // result of a function with a result discarded
    m.functions[0].code[0] = {Opcode::Call, noRegister, 1, 0};
    CHECK(failsWith(encode(m), "no destination"));

    m = module;
    m.functions[0].code.back() = {Opcode::Nop, 0, 0, 0};
    CHECK(failsWith(encode(m), "terminator"));

    m = module;
    m.functions[0].code[0] = {Opcode::Nop, 1, 0, 0};
    CHECK(failsWith(encode(m), "unused operand"));

    m = module;
    m.functions[1].registerCount = 1; // fewer registers than parameters
    CHECK(failsWith(encode(m), "fewer registers than parameters"));

    m = module;
    m.functions[0].nameIndex = static_cast<std::uint32_t>(m.strings.size());
    CHECK(failsWith(encode(m), "missing name"));

    m = module;
    m.strings[0] = std::string("\xC0\x80", 2); // overlong encoding
    CHECK(failsWith(encode(m), "not valid UTF-8"));

    m = module;
    m.functions.push_back(m.functions[1]);
    CHECK(failsWith(encode(m), "duplicate function"));

    m = module;
    m.functions[1].parameters[0] = ScriptType::Any;
    CHECK(failsWith(encode(m), "invalid parameter type any"));
}

SCHUIMM_TEST(Bytecode_MalformedSectionsAndOverflowingOffsets)
{
    const std::vector<std::byte> good = sampleBytes();
    std::vector<std::byte> bytes = good;

    // A section that extends past the end of the file.
    putU32(bytes, sectionEntry(format::SectionId::Code) + 8, 0xFFFFFFF0u);
    fixCrc(bytes);
    CHECK(failsWith(bytes, "outside the file"));
    // An offset + size that overflows 32 bits.
    bytes = good;
    putU32(bytes, sectionEntry(format::SectionId::Debug) + 4, 0xFFFFFFF0u);
    putU32(bytes, sectionEntry(format::SectionId::Debug) + 8, 0x20u);
    fixCrc(bytes);
    CHECK(failsWith(bytes, "outside the file"));
    // Overlapping sections.
    bytes = good;
    putU32(bytes, sectionEntry(format::SectionId::Constants) + 4, getU32(good, sectionEntry(format::SectionId::Strings) + 4));
    fixCrc(bytes);
    CHECK(failsWith(bytes, "overlap"));
    // A section pointing into the header.
    bytes = good;
    putU32(bytes, sectionEntry(format::SectionId::Strings) + 4, 4);
    fixCrc(bytes);
    CHECK(failsWith(bytes, "outside the file"));
    // Duplicate and unknown section ids.
    bytes = good;
    putU32(bytes, sectionEntry(format::SectionId::Debug), 1);
    fixCrc(bytes);
    CHECK(failsWith(bytes, "duplicate section"));
    bytes = good;
    putU32(bytes, sectionEntry(format::SectionId::Debug), 77);
    fixCrc(bytes);
    CHECK(failsWith(bytes, "unknown section id 77"));
    // Element counts larger than the section can hold are rejected before allocating.
    bytes = good;
    putU32(bytes, sectionOffset(good, format::SectionId::Strings), 0xFFFFFFFFu);
    fixCrc(bytes);
    CHECK(failsWith(bytes, "truncated string table"));
    bytes = good;
    putU32(bytes, sectionOffset(good, format::SectionId::Code), 0x7FFFFFFFu);
    fixCrc(bytes);
    CHECK(failsWith(bytes, "truncated code"));
    // A string length running past its section.
    bytes = good;
    putU32(bytes, sectionOffset(good, format::SectionId::Strings) + 4, 0xFFFFFFF0u);
    fixCrc(bytes);
    CHECK(failsWith(bytes, "truncated string"));
    // A function whose code range overflows.
    bytes = good;
    const std::size_t functions = sectionOffset(good, format::SectionId::Functions) + 4;
    // main(): name u32, return u8, param count u8 (0), registers u16, code offset u32, length u32.
    putU32(bytes, functions + 8, 0xFFFFFFFFu);
    putU32(bytes, functions + 12, 2);
    fixCrc(bytes);
    CHECK(failsWith(bytes, "code range"));
    // Trailing bytes inside a section.
    Module m = sampleModule();
    std::vector<std::byte> trailing = encode(m);
    const std::size_t sizeField = sectionEntry(format::SectionId::Imports) + 8;
    putU32(trailing, sizeField, getU32(trailing, sizeField) - 1);
    fixCrc(trailing);
    CHECK(!readBytecode(trailing).has_value());
}

SCHUIMM_TEST(Bytecode_RandomCorruptionNeverCrashes)
{
    // Deterministic mutation sweep: flip bits everywhere (with a corrected checksum, so the
    // structural and semantic checks are exercised). Every result must be a clean error or a
    // module that validates, and a module that validates must run without crashing.
    const std::vector<std::byte> good = sampleBytes();
    NativeRegistry natives;
    REQUIRE(registerStandardNatives(natives, {}).has_value());
    REQUIRE(natives.add({"give", {ScriptType::String, ScriptType::Int}, ScriptType::Bool,
                         [](NativeContext&, std::span<const Value>) { return NativeResult::returnValue(Value::fromBool(true)); },
                         {}})
                .has_value());
    std::uint32_t seed = 12345;
    int accepted = 0;
    int rejected = 0;
    for (std::size_t position = format::headerSize; position < good.size(); ++position) {
        for (int round = 0; round < 3; ++round) {
            seed = seed * 1664525u + 1013904223u;
            std::vector<std::byte> bytes = good;
            bytes[position] ^= static_cast<std::byte>(((seed >> 24) & 0xFF) | 1);
            fixCrc(bytes);
            auto program = Program::load(bytes, natives);
            if (!program) {
                ++rejected;
                continue;
            }
            ++accepted;
            SchedulerSettings settings;
            settings.instructionBudget = 2000;
            ScriptScheduler scheduler(settings);
            for (std::size_t f = 0; f < (*program)->module().functions.size(); ++f) {
                const Function& function = (*program)->module().functions[f];
                std::vector<Value> arguments;
                for (const ScriptType type : function.parameters) {
                    switch (type) {
                    case ScriptType::Int: arguments.push_back(Value::fromInt(2)); break;
                    case ScriptType::Float: arguments.push_back(Value::fromFloat(1.5)); break;
                    case ScriptType::Bool: arguments.push_back(Value::fromBool(true)); break;
                    default: arguments.push_back(Value::fromString("s")); break;
                    }
                }
                (void)scheduler.start(*program, (*program)->functionName(f), std::move(arguments));
            }
            for (int update = 0; update < 4; ++update) {
                scheduler.update(0.25);
            }
        }
    }
    CHECK(rejected > 0);
    CHECK(accepted > 0); // e.g. changed constants or line numbers
}

SCHUIMM_TEST(Bytecode_ValidateModuleForInMemoryModules)
{
    CHECK(validateModule(sampleModule()).has_value());
    Module empty;
    empty.strings = {"x"};
    CHECK(validateModule(empty).has_value()); // A module without functions is valid (but has no entry).
    Module noCode = empty;
    noCode.functions.push_back({});
    CHECK(!validateModule(noCode).has_value());
}

// --- Format 1.1: objects -------------------------------------------------------------------------

namespace {

const char* objectSource =
    "func helper(n: int) -> int:\n"
    "\treturn n * 2\n"
    "func use():\n"
    "\tprint(\"room use\")\n"
    "object blender:\n"
    "\tfunc look_at():\n"
    "\t\tprint(helper(21))\n"
    "\tfunc use(other: object) -> bool:\n"
    "\t\treturn self == other\n"
    "object cupboard:\n"
    "\tfunc look_at():\n"
    "\t\tprint(self)\n";

Module objectModule()
{
    const Compiled c = compileSource(objectSource);
    if (!c.result.success) std::fprintf(stderr, "%s", c.diagnostics().c_str());
    REQUIRE(c.result.success);
    return c.result.module;
}

// Rewrites a 1.1 file without objects as a format 1.0 file (6 sections, no Objects section).
std::vector<std::byte> downgradeToVersion10(const std::vector<std::byte>& bytes)
{
    std::vector<std::byte> out(bytes.begin(), bytes.begin() + format::headerSize);
    putU16(out, 6, 0);
    putU16(out, 10, 0);
    putU32(out, 24, format::sectionCountVersion10);
    out.resize(format::headerSize + format::sectionCountVersion10 * format::sectionEntrySize);
    for (std::uint32_t id = 1; id <= format::sectionCountVersion10; ++id) {
        const std::size_t entry = sectionEntry(static_cast<format::SectionId>(id));
        const std::uint32_t offset = getU32(bytes, entry + 4);
        const std::uint32_t size = getU32(bytes, entry + 8);
        putU32(out, entry, id);
        putU32(out, entry + 4, static_cast<std::uint32_t>(out.size()));
        putU32(out, entry + 8, size);
        out.insert(out.end(), bytes.begin() + offset, bytes.begin() + offset + size);
    }
    putU32(out, 20, static_cast<std::uint32_t>(out.size()));
    fixCrc(out);
    return out;
}

std::vector<std::byte> encodeModule(const Module& module)
{
    auto bytes = writeBytecode(module);
    REQUIRE(bytes.has_value());
    return *bytes;
}

bool invalidModule(const Module& module, std::string_view text)
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

SCHUIMM_TEST(Bytecode_ObjectsRoundTrip)
{
    const Module original = objectModule();
    REQUIRE(original.objects.size() == 2);
    const std::vector<std::byte> bytes = encodeModule(original);
    CHECK_EQ(getU32(bytes, 24), format::sectionCount);
    CHECK(static_cast<int>(bytes[6]) == format::objectsMinorVersion);
    auto read = readBytecode(bytes);
    REQUIRE(read.has_value());
    CHECK_EQ(read->formatMinor, bytecodeFormatMinor);
    REQUIRE(read->objects.size() == 2);
    for (std::size_t o = 0; o < 2; ++o) {
        CHECK_EQ(read->objects[o].nameIndex, original.objects[o].nameIndex);
        REQUIRE(read->objects[o].handlers.size() == original.objects[o].handlers.size());
        for (std::size_t h = 0; h < original.objects[o].handlers.size(); ++h) {
            CHECK_EQ(read->objects[o].handlers[h].nameIndex, original.objects[o].handlers[h].nameIndex);
            CHECK_EQ(read->objects[o].handlers[h].function, original.objects[o].handlers[h].function);
        }
    }
    CHECK(encodeModule(*read) == bytes);
    const std::string listing = disassemble(*read);
    CHECK(listing.find("; SCHUIMM bytecode 1.1, language 1.1") != std::string::npos);
    CHECK(listing.find("  blender: look_at -> f2 use -> f3\n  cupboard: look_at -> f4\n") != std::string::npos);
    CHECK(listing.find("function f3 blender.use(object, object) -> bool") != std::string::npos);
    CHECK(listing.find("EqObject") != std::string::npos);
    // Room-level "use" and the handler "blender.use" coexist; lookup keeps them apart.
    NativeRegistry natives;
    REQUIRE(registerStandardNatives(natives, {}).has_value());
    auto program = Program::load(bytes, natives);
    REQUIRE(program.has_value());
    CHECK_EQ((*program)->findFunction("use"), std::optional<std::uint32_t>(1));
    CHECK_EQ((*program)->findHandler("blender", "use"), std::optional<std::uint32_t>(3));
    CHECK_EQ((*program)->findHandler("cupboard", "look_at"), std::optional<std::uint32_t>(4));
    CHECK(!(*program)->findFunction("look_at").has_value());
    CHECK_EQ((*program)->functionDisplayName(3), std::string("blender.use"));
    CHECK_EQ((*program)->functionDisplayName(1), std::string("use"));
}

SCHUIMM_TEST(Bytecode_Version10FilesStillLoad)
{
    const std::vector<std::byte> old = downgradeToVersion10(sampleBytes());
    auto module = readBytecode(old);
    if (!module) std::fprintf(stderr, "  %s\n", module.error().message.c_str());
    REQUIRE(module.has_value());
    CHECK_EQ(module->formatMinor, std::uint16_t{0});
    CHECK(module->objects.empty());
    // It runs as before.
    NativeRegistry natives;
    std::vector<std::string> output;
    REQUIRE(registerStandardNatives(natives, [&](std::string_view, std::string_view text) { output.emplace_back(text); })
                .has_value());
    REQUIRE(natives.add({"give", {ScriptType::String, ScriptType::Int}, ScriptType::Bool,
                         [](NativeContext&, std::span<const Value>) { return NativeResult::returnValue(Value::fromBool(true)); },
                         {}})
                .has_value());
    auto program = Program::load(old, natives);
    REQUIRE(program.has_value());
    ScriptScheduler scheduler;
    REQUIRE(scheduler.start(*program, "main").has_value());
    for (int i = 0; i < 4; ++i) scheduler.update(0.5);
    CHECK_EQ(output.size(), std::size_t{3});
    // A 1.0 header with 7 sections, or a 1.1 header with 6, is malformed.
    std::vector<std::byte> mixed = old;
    putU16(mixed, 6, 1);
    fixCrc(mixed);
    CHECK(failsWith(mixed, "expected 7 sections"));
    std::vector<std::byte> current = sampleBytes();
    putU16(current, 6, 0);
    fixCrc(current);
    CHECK(failsWith(current, "expected 6 sections"));
}

SCHUIMM_TEST(Bytecode_Version10CannotUseObjects)
{
    // The object type in a signature.
    const Compiled typed = compileSource("extern func touch(o: object)\nfunc f(o: object):\n\ttouch(o)\n");
    REQUIRE(typed.result.success);
    CHECK(failsWith(downgradeToVersion10(encodeModule(typed.result.module)), "requires bytecode format 1.1"));
    Module module = typed.result.module;
    module.formatMinor = 0;
    CHECK(invalidModule(module, "native import 'touch' uses type object"));
    module.imports.clear();
    module.functions[0].code = {{Opcode::Return, noRegister, 0, 0}};
    module.functions[0].positions = {{}};
    CHECK(invalidModule(module, "function 'f' uses type object"));
    // The object opcodes.
    const Compiled compare = compileSource("func f(a: int, b: int) -> bool:\n\treturn a == b\n");
    REQUIRE(compare.result.success);
    Module opcodes = compare.result.module;
    bool replaced = false;
    for (Instruction& instruction : opcodes.functions[0].code) {
        if (instruction.opcode == Opcode::EqInt) {
            instruction.opcode = Opcode::EqObject;
            replaced = true;
        }
    }
    REQUIRE(replaced);
    CHECK(validateModule(opcodes).has_value()); // 1.1: valid (register types are checked when run).
    opcodes.formatMinor = 0;
    CHECK(invalidModule(opcodes, "opcode requires bytecode format 1.1"));
    // Objects.
    Module objects = objectModule();
    objects.formatMinor = 0;
    CHECK(invalidModule(objects, "object declarations require bytecode format 1.1"));
}

SCHUIMM_TEST(Bytecode_ObjectTableValidation)
{
    const Module good = objectModule();
    REQUIRE(validateModule(good).has_value());
    Module m = good;
    m.objects.push_back(m.objects[0]);
    CHECK(invalidModule(m, "duplicate object 'blender'"));
    m = good;
    m.objects[0].handlers.push_back(m.objects[0].handlers[0]);
    CHECK(invalidModule(m, "duplicate handler 'look_at' in object 'blender'"));
    m = good;
    m.objects[0].handlers[0].function = 99;
    CHECK(invalidModule(m, "refers to missing function 99"));
    m = good;
    m.objects[1].handlers[0].function = m.objects[0].handlers[0].function;
    CHECK(invalidModule(m, "is the handler of more than one event"));
    m = good;
    m.objects[0].handlers[0].function = 0; // helper(int): no self parameter.
    CHECK(invalidModule(m, "must take the object (self) as its first parameter"));
    m = good;
    m.objects[0].nameIndex = 9999;
    CHECK(invalidModule(m, "object 0 refers to a missing name"));
    m = good;
    m.objects[0].handlers[1].nameIndex = 9999;
    CHECK(invalidModule(m, "handler 1 of object 'blender' refers to a missing name"));
    // Handler names need only be unique within their object, room functions among themselves.
    m = good;
    m.functions[1].nameIndex = m.functions[0].nameIndex;
    CHECK(invalidModule(m, "duplicate function 'helper'"));
    // A truncated objects section.
    std::vector<std::byte> bytes = encodeModule(good);
    const std::size_t objects = sectionOffset(bytes, format::SectionId::Objects);
    putU32(bytes, objects, 1000);
    fixCrc(bytes);
    CHECK(failsWith(bytes, "truncated object table"));
}

SCHUIMM_TEST(Bytecode_RandomCorruptionOfObjectsNeverCrashes)
{
    // As Bytecode_RandomCorruptionNeverCrashes, for a file with objects: whatever validates is
    // started through every room function and dispatched through every handler.
    const std::vector<std::byte> good = encodeModule(objectModule());
    NativeRegistry natives;
    REQUIRE(registerStandardNatives(natives, {}).has_value());
    std::uint32_t seed = 54321;
    int accepted = 0;
    int rejected = 0;
    const auto argumentFor = [](ScriptType type) {
        switch (type) {
        case ScriptType::Int: return Value::fromInt(2);
        case ScriptType::Float: return Value::fromFloat(1.5);
        case ScriptType::Bool: return Value::fromBool(true);
        case ScriptType::Object: return Value::fromObject(ObjectRef{3});
        default: return Value::fromString("s");
        }
    };
    for (std::size_t position = format::headerSize; position < good.size(); ++position) {
        for (int round = 0; round < 3; ++round) {
            seed = seed * 1664525u + 1013904223u;
            std::vector<std::byte> bytes = good;
            bytes[position] ^= static_cast<std::byte>(((seed >> 24) & 0xFF) | 1);
            fixCrc(bytes);
            auto program = Program::load(bytes, natives);
            if (!program) {
                ++rejected;
                continue;
            }
            ++accepted;
            SchedulerSettings settings;
            settings.instructionBudget = 2000;
            ScriptScheduler scheduler(settings);
            const Module& module = (*program)->module();
            for (std::size_t f = 0; f < module.functions.size(); ++f) {
                std::vector<Value> arguments;
                for (const ScriptType type : module.functions[f].parameters) arguments.push_back(argumentFor(type));
                (void)scheduler.start(*program, (*program)->functionName(f), arguments);
            }
            for (const ObjectEntry& object : module.objects) {
                for (const HandlerEntry& handler : object.handlers) {
                    const Function& function = module.functions[handler.function];
                    std::vector<Value> arguments;
                    for (std::size_t p = 1; p < function.parameters.size(); ++p) {
                        arguments.push_back(argumentFor(function.parameters[p]));
                    }
                    (void)scheduler.dispatch(*program, module.string(object.nameIndex), module.string(handler.nameIndex),
                                             ObjectRef{9}, std::move(arguments));
                }
            }
            for (int update = 0; update < 4; ++update) scheduler.update(0.25);
        }
    }
    CHECK(rejected > 0);
    CHECK(accepted > 0);
}
