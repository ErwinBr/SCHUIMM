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
    CHECK(failsWith(bytes, "format 2.0 is not supported", ErrorCode::Unsupported));
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
