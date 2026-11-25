#include "Script/Bytecode/BytecodeReader.h"

#include "Core/Common/CheckedMath.h"
#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/BytecodeFormat.h"
#include "Script/Bytecode/Crc32.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <set>
#include <string_view>

namespace schuimm::script {

namespace {

std::unexpected<Error> invalid(std::string message)
{
    return makeError(ErrorCode::InvalidData, concat("Invalid bytecode: ", message));
}

// Bounds-checked little-endian reader over one section. Every read fails cleanly at the end.
class ByteReader {
public:
    ByteReader(std::span<const std::byte> data, std::size_t baseOffset)
        : data_(data)
        , base_(baseOffset)
    {
    }

    [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - position_; }
    [[nodiscard]] std::size_t offset() const noexcept { return base_ + position_; }
    [[nodiscard]] bool atEnd() const noexcept { return position_ == data_.size(); }

    bool u8(std::uint8_t& value)
    {
        if (remaining() < 1) return false;
        value = static_cast<std::uint8_t>(data_[position_++]);
        return true;
    }
    bool u16(std::uint16_t& value)
    {
        if (remaining() < 2) return false;
        value = static_cast<std::uint16_t>(static_cast<unsigned>(data_[position_]) |
                                           (static_cast<unsigned>(data_[position_ + 1]) << 8));
        position_ += 2;
        return true;
    }
    bool u32(std::uint32_t& value)
    {
        if (remaining() < 4) return false;
        value = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            value |= static_cast<std::uint32_t>(data_[position_ + i]) << (8 * i);
        }
        position_ += 4;
        return true;
    }
    bool u64(std::uint64_t& value)
    {
        if (remaining() < 8) return false;
        value = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            value |= static_cast<std::uint64_t>(data_[position_ + i]) << (8 * i);
        }
        position_ += 8;
        return true;
    }
    bool text(std::size_t length, std::string& out)
    {
        if (remaining() < length) return false;
        out.assign(reinterpret_cast<const char*>(data_.data() + position_), length);
        position_ += length;
        return true;
    }

private:
    std::span<const std::byte> data_;
    std::size_t base_;
    std::size_t position_ = 0;
};

std::unexpected<Error> bad(std::string message)
{
    return makeError(ErrorCode::InvalidData, std::move(message));
}

std::uint16_t loadU16(std::span<const std::byte> data, std::size_t offset)
{
    return static_cast<std::uint16_t>(static_cast<unsigned>(data[offset]) | (static_cast<unsigned>(data[offset + 1]) << 8));
}

std::uint32_t loadU32(std::span<const std::byte> data, std::size_t offset)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(data[offset + i]) << (8 * i);
    }
    return value;
}

// Strict UTF-8 check (no overlong forms, surrogates or values above U+10FFFF).
bool isValidUtf8(std::string_view text) noexcept
{
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        char32_t minimum = 0;
        char32_t codepoint = 0;
        if (lead < 0x80) {
            ++i;
            continue;
        }
        if ((lead & 0xE0) == 0xC0) {
            length = 2;
            minimum = 0x80;
            codepoint = lead & 0x1F;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            minimum = 0x800;
            codepoint = lead & 0x0F;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            minimum = 0x10000;
            codepoint = lead & 0x07;
        } else {
            return false;
        }
        if (text.size() - i < length) {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const auto continuation = static_cast<unsigned char>(text[i + k]);
            if ((continuation & 0xC0) != 0x80) {
                return false;
            }
            codepoint = (codepoint << 6) | (continuation & 0x3F);
        }
        if (codepoint < minimum || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return false;
        }
        i += length;
    }
    return true;
}

// Reads `count` element-of-at-least-`minimumSize` entries: rejects counts the data cannot hold
// before anything is allocated.
bool plausibleCount(const ByteReader& reader, std::uint32_t count, std::size_t minimumSize)
{
    const std::optional<std::size_t> bytes = checkedMul(count, minimumSize);
    return bytes && *bytes <= reader.remaining();
}

Result<std::vector<ScriptType>> readTypes(ByteReader& reader, const char* what)
{
    std::uint8_t count = 0;
    if (!reader.u8(count)) {
        return invalid(concat("truncated ", what, " signature at offset ", reader.offset()));
    }
    std::vector<ScriptType> types;
    types.reserve(count);
    for (std::uint8_t i = 0; i < count; ++i) {
        std::uint8_t raw = 0;
        if (!reader.u8(raw)) {
            return invalid(concat("truncated ", what, " signature at offset ", reader.offset()));
        }
        const auto type = scriptTypeFromByte(raw);
        if (!type) {
            return invalid(concat("unknown type ", static_cast<unsigned>(raw), " at offset ", reader.offset() - 1));
        }
        types.push_back(*type);
    }
    return types;
}

struct Section {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    bool present = false;
};

struct RawFunction {
    std::uint32_t codeOffset = 0;
    std::uint32_t codeLength = 0;
    std::uint32_t debugOffset = 0;
};

Status checkEnd(const ByteReader& reader, const char* section)
{
    if (!reader.atEnd()) {
        return invalid(concat(reader.remaining(), " unexpected trailing bytes in the ", section, " section at offset ",
                              reader.offset()));
    }
    return {};
}

// --- Semantic validation ------------------------------------------------------------------------

struct Validator {
    const Module& module;
    // Per function: the object whose handler it is, or noOwner (room-level function).
    std::vector<std::uint32_t> owner = {};

    static constexpr std::uint32_t noOwner = 0xFFFFFFFFu;

    [[nodiscard]] std::string functionName(std::size_t index) const
    {
        const Function& function = module.functions[index];
        std::string name = function.nameIndex < module.strings.size() ? module.strings[function.nameIndex] : concat("#", index);
        if (index < owner.size() && owner[index] != noOwner) {
            return concat(module.strings[module.objects[owner[index]].nameIndex], ".", name);
        }
        return name;
    }

    [[nodiscard]] bool hasObjects() const noexcept { return module.formatMinor >= format::objectsMinorVersion; }

    static bool usesObjectType(const std::vector<ScriptType>& parameters, ScriptType returnType)
    {
        return returnType == ScriptType::Object ||
               std::find(parameters.begin(), parameters.end(), ScriptType::Object) != parameters.end();
    }

    // The Objects table: unique object names, unique handler names per object, existing
    // functions, each function a handler at most once, and "self" (object) as parameter 0.
    Status checkObjects()
    {
        owner.assign(module.functions.size(), noOwner);
        if (!hasObjects() && !module.objects.empty()) {
            return invalid(concat("object declarations require bytecode format 1.", format::objectsMinorVersion));
        }
        std::set<std::string_view> objectNames;
        for (std::size_t o = 0; o < module.objects.size(); ++o) {
            const ObjectEntry& object = module.objects[o];
            if (object.nameIndex >= module.strings.size()) {
                return invalid(concat("object ", o, " refers to a missing name"));
            }
            const std::string& name = module.strings[object.nameIndex];
            if (!objectNames.insert(name).second) {
                return invalid(concat("duplicate object '", name, "'"));
            }
            std::set<std::string_view> handlerNames;
            for (std::size_t h = 0; h < object.handlers.size(); ++h) {
                const HandlerEntry& handler = object.handlers[h];
                if (handler.nameIndex >= module.strings.size()) {
                    return invalid(concat("handler ", h, " of object '", name, "' refers to a missing name"));
                }
                const std::string& handlerName = module.strings[handler.nameIndex];
                if (!handlerNames.insert(handlerName).second) {
                    return invalid(concat("duplicate handler '", handlerName, "' in object '", name, "'"));
                }
                if (handler.function >= module.functions.size()) {
                    return invalid(concat("handler '", name, ".", handlerName, "' refers to missing function ", handler.function));
                }
                if (owner[handler.function] != noOwner) {
                    return invalid(concat("function ", handler.function, " is the handler of more than one event"));
                }
                const Function& function = module.functions[handler.function];
                if (function.parameters.empty() || function.parameters[0] != ScriptType::Object) {
                    return invalid(concat("handler '", name, ".", handlerName,
                                          "' must take the object (self) as its first parameter"));
                }
                owner[handler.function] = static_cast<std::uint32_t>(o);
            }
        }
        return {};
    }

    Status run()
    {
        for (std::size_t i = 0; i < module.strings.size(); ++i) {
            if (!isValidUtf8(module.strings[i])) {
                return invalid(concat("string ", i, " is not valid UTF-8"));
            }
        }
        if (module.sourceNameIndex >= module.strings.size()) {
            return invalid("source name refers to a missing string");
        }
        if (module.constants.size() > 0x10000) {
            return invalid("more than 65536 constants");
        }
        for (std::size_t i = 0; i < module.constants.size(); ++i) {
            const Constant& constant = module.constants[i];
            if (constant.type != ScriptType::Int && constant.type != ScriptType::Float &&
                constant.type != ScriptType::String) {
                return invalid(concat("constant ", i, " has unsupported type ", toString(constant.type)));
            }
            if (constant.type == ScriptType::String && constant.stringIndex >= module.strings.size()) {
                return invalid(concat("constant ", i, " refers to missing string ", constant.stringIndex));
            }
        }
        std::set<std::string_view> names;
        for (std::size_t i = 0; i < module.imports.size(); ++i) {
            const NativeImport& import = module.imports[i];
            if (import.nameIndex >= module.strings.size()) {
                return invalid(concat("native import ", i, " refers to a missing name"));
            }
            if (auto status = checkSignature(import.parameters, import.returnType, true); !status) {
                return invalid(concat("native import '", module.strings[import.nameIndex], "': ", status.error().message));
            }
            if (!names.insert(module.strings[import.nameIndex]).second) {
                return invalid(concat("duplicate native import '", module.strings[import.nameIndex], "'"));
            }
        }
        if (module.functions.size() > 0x10000 || module.imports.size() > 0x10000) {
            return invalid("more than 65536 functions or imports");
        }
        if (auto status = checkObjects(); !status) {
            return status;
        }
        if (!hasObjects()) {
            for (const NativeImport& import : module.imports) {
                if (usesObjectType(import.parameters, import.returnType)) {
                    return invalid(concat("native import '", module.strings[import.nameIndex],
                                          "' uses type object, which requires bytecode format 1.", format::objectsMinorVersion));
                }
            }
        }
        names.clear();
        for (std::size_t i = 0; i < module.functions.size(); ++i) {
            const Function& function = module.functions[i];
            if (function.nameIndex >= module.strings.size()) {
                return invalid(concat("function ", i, " refers to a missing name"));
            }
            // Room-level function names are unique; a handler's name is unique within its object.
            if (owner[i] == noOwner && !names.insert(module.strings[function.nameIndex]).second) {
                return invalid(concat("duplicate function '", module.strings[function.nameIndex], "'"));
            }
            if (!hasObjects() && usesObjectType(function.parameters, function.returnType)) {
                return invalid(concat("function '", functionName(i), "' uses type object, which requires bytecode format 1.",
                                      format::objectsMinorVersion));
            }
            if (auto status = checkSignature(function.parameters, function.returnType, false); !status) {
                return invalid(concat("function '", functionName(i), "': ", status.error().message));
            }
            if (function.parameters.size() > function.registerCount) {
                return invalid(concat("function '", functionName(i), "' has fewer registers than parameters"));
            }
            if (function.code.empty()) {
                return invalid(concat("function '", functionName(i), "' has no code"));
            }
            if (function.positions.size() != function.code.size()) {
                return invalid(concat("function '", functionName(i), "' debug information does not match its code"));
            }
            for (std::size_t ip = 0; ip < function.code.size(); ++ip) {
                if (auto status = checkInstruction(function, ip); !status) {
                    return invalid(concat("function '", functionName(i), "' instruction ", ip, " (",
                                          toString(function.code[ip].opcode), "): ", status.error().message));
                }
            }
            if (!opcodeInfo(function.code.back().opcode).terminator) {
                return invalid(concat("function '", functionName(i), "' does not end with a terminator"));
            }
        }
        return {};
    }

    static Status checkSignature(const std::vector<ScriptType>& parameters, ScriptType returnType, bool native)
    {
        if (parameters.size() > format::maxParameters) {
            return bad("too many parameters");
        }
        for (const ScriptType type : parameters) {
            if (type == ScriptType::None || (type == ScriptType::Any && !native)) {
                return bad(concat("invalid parameter type ", toString(type)));
            }
        }
        if (returnType == ScriptType::Any) {
            return bad("invalid return type any");
        }
        return {};
    }

    Status checkOperand(const Function& function, OperandKind kind, std::uint16_t value, const char* field) const
    {
        switch (kind) {
        case OperandKind::Unused:
            if (value != 0) return bad(concat("unused operand ", field, " is ", value, ", expected 0"));
            return {};
        case OperandKind::Register:
            if (value >= function.registerCount) {
                return bad(concat("register ", value, " in ", field, " out of range (", function.registerCount, " registers)"));
            }
            return {};
        case OperandKind::OptRegister:
            if (value != noRegister && value >= function.registerCount) {
                return bad(concat("register ", value, " in ", field, " out of range (", function.registerCount, " registers)"));
            }
            return {};
        case OperandKind::Constant:
            if (value >= module.constants.size()) return bad(concat("constant ", value, " does not exist"));
            return {};
        case OperandKind::Function:
            if (value >= module.functions.size()) return bad(concat("function ", value, " does not exist"));
            return {};
        case OperandKind::Native:
            if (value >= module.imports.size()) return bad(concat("native import ", value, " does not exist"));
            return {};
        case OperandKind::ArgWindow:
        case OperandKind::Target:
            return {}; // Checked with the whole instruction.
        }
        return bad("unknown operand kind");
    }

    Status checkResult(std::uint16_t destination, ScriptType returnType) const
    {
        if (returnType == ScriptType::None) {
            if (destination != noRegister) return bad("call of a function without result stores a result");
        } else if (destination == noRegister) {
            return bad("call result has no destination register");
        }
        return {};
    }

    Status checkInstruction(const Function& function, std::size_t ip) const
    {
        const Instruction& instruction = function.code[ip];
        const OpcodeInfo& info = opcodeInfo(instruction.opcode);
        if (!hasObjects() && (instruction.opcode == Opcode::EqObject || instruction.opcode == Opcode::NeObject)) {
            return bad(concat("opcode requires bytecode format 1.", format::objectsMinorVersion));
        }
        if (auto s = checkOperand(function, info.a, instruction.a, "a"); !s) return s;
        if (info.b == OperandKind::Target) {
            if (instruction.target() >= function.code.size()) {
                return bad(concat("branch target ", instruction.target(), " outside the function (", function.code.size(),
                                      " instructions)"));
            }
            return {};
        }
        if (auto s = checkOperand(function, info.b, instruction.b, "b"); !s) return s;
        if (auto s = checkOperand(function, info.c, instruction.c, "c"); !s) return s;

        switch (instruction.opcode) {
        case Opcode::Call:
        case Opcode::CallNative: {
            const bool native = instruction.opcode == Opcode::CallNative;
            const std::vector<ScriptType>& parameters =
                native ? module.imports[instruction.b].parameters : module.functions[instruction.b].parameters;
            const ScriptType returnType =
                native ? module.imports[instruction.b].returnType : module.functions[instruction.b].returnType;
            if (auto s = checkResult(instruction.a, returnType); !s) return s;
            const std::size_t end = static_cast<std::size_t>(instruction.c) + parameters.size();
            if (!parameters.empty() && end > function.registerCount) {
                return bad(concat("argument registers ", instruction.c, "..", end - 1, " out of range (",
                                      function.registerCount, " registers)"));
            }
            if (parameters.empty() && instruction.c != 0) {
                return bad("argument window of a call without arguments must be 0");
            }
            return {};
        }
        case Opcode::Return:
            if (function.returnType == ScriptType::None && instruction.a != noRegister) {
                return bad("function without result returns a value");
            }
            if (function.returnType != ScriptType::None && instruction.a == noRegister) {
                return bad("function with a result returns no value");
            }
            return {};
        default: return {};
        }
    }
};

} // namespace

Status validateModule(const Module& module)
{
    Validator validator{module};
    return validator.run();
}

Result<Module> readBytecode(std::span<const std::byte> data)
{
    if (data.size() < format::headerSize) {
        return invalid(concat("file is ", data.size(), " bytes, smaller than the ", format::headerSize, "-byte header"));
    }
    if (std::memcmp(data.data(), format::magic, sizeof format::magic) != 0) {
        return invalid("not a SCHUIMM bytecode file (bad magic)");
    }
    const std::uint16_t formatMajor = loadU16(data, 4);
    const std::uint16_t formatMinor = loadU16(data, 6);
    if (formatMajor != bytecodeFormatMajor || formatMinor > bytecodeFormatMinor) {
        return makeError(ErrorCode::Unsupported, concat("Bytecode format ", formatMajor, ".", formatMinor,
                                                        " is not supported (this runtime reads ", bytecodeFormatMajor,
                                                        ".0 to ", bytecodeFormatMajor, ".", bytecodeFormatMinor, ")"));
    }
    Module module;
    module.formatMinor = formatMinor;
    module.languageMajor = loadU16(data, 8);
    module.languageMinor = loadU16(data, 10);
    if (module.languageMajor != languageVersionMajor || module.languageMinor > languageVersionMinor) {
        return makeError(ErrorCode::Unsupported, concat("Script language version ", module.languageMajor, ".",
                                                        module.languageMinor, " is not supported"));
    }
    module.compilerVersion = loadU32(data, 12);
    if (loadU32(data, 16) != 0) {
        return invalid("unknown header flags");
    }
    const std::uint32_t fileSize = loadU32(data, 20);
    if (fileSize != data.size() || fileSize > format::maxFileSize) {
        return invalid(concat("header declares ", fileSize, " bytes but the file has ", data.size()));
    }
    const std::uint32_t sectionCount = loadU32(data, 24);
    const bool hasObjects = formatMinor >= format::objectsMinorVersion;
    const std::uint32_t expectedSections = hasObjects ? format::sectionCount : format::sectionCountVersion10;
    if (sectionCount != expectedSections) {
        return invalid(concat("expected ", expectedSections, " sections, header declares ", sectionCount));
    }
    const std::size_t tableEnd = format::headerSize + std::size_t{sectionCount} * format::sectionEntrySize;
    if (tableEnd > data.size()) {
        return invalid("section table extends past the end of the file");
    }
    if (crc32(data.subspan(format::headerSize)) != loadU32(data, 28)) {
        return invalid("checksum mismatch (file is corrupt)");
    }

    std::array<Section, format::sectionCount + 1> sections{};
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
    for (std::uint32_t i = 0; i < sectionCount; ++i) {
        const std::size_t entry = format::headerSize + std::size_t{i} * format::sectionEntrySize;
        const std::uint32_t id = loadU32(data, entry);
        const std::uint32_t offset = loadU32(data, entry + 4);
        const std::uint32_t size = loadU32(data, entry + 8);
        if (id == 0 || id > expectedSections) {
            return invalid(concat("unknown section id ", id, " at offset ", entry));
        }
        if (sections[id].present) {
            return invalid(concat("duplicate section id ", id));
        }
        const std::optional<std::size_t> end = checkedAdd(offset, size);
        if (offset < tableEnd || !end || *end > data.size()) {
            return invalid(concat("section ", id, " (offset ", offset, ", size ", size, ") is outside the file"));
        }
        sections[id] = {offset, size, true};
        ranges.emplace_back(offset, size);
    }
    std::sort(ranges.begin(), ranges.end());
    for (std::size_t i = 1; i < ranges.size(); ++i) {
        if (static_cast<std::size_t>(ranges[i - 1].first) + ranges[i - 1].second > ranges[i].first) {
            return invalid(concat("sections overlap at offset ", ranges[i].first));
        }
    }
    const auto reader = [&](format::SectionId id) {
        const Section& section = sections[static_cast<std::uint32_t>(id)];
        return ByteReader(data.subspan(section.offset, section.size), section.offset);
    };
    const auto truncated = [](const ByteReader& r, const char* what) {
        return invalid(concat("truncated ", what, " at offset ", r.offset()));
    };

    {   // Strings
        ByteReader r = reader(format::SectionId::Strings);
        std::uint32_t count = 0;
        if (!r.u32(count) || !plausibleCount(r, count, 4)) return truncated(r, "string table");
        module.strings.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t length = 0;
            if (!r.u32(length) || !r.text(length, module.strings[i])) return truncated(r, "string");
        }
        if (auto s = checkEnd(r, "string"); !s) return std::unexpected(s.error());
    }
    {   // Constants
        ByteReader r = reader(format::SectionId::Constants);
        std::uint32_t count = 0;
        if (!r.u32(count) || !plausibleCount(r, count, 5)) return truncated(r, "constant pool");
        module.constants.resize(count);
        for (Constant& constant : module.constants) {
            std::uint8_t tag = 0;
            if (!r.u8(tag)) return truncated(r, "constant");
            const auto type = scriptTypeFromByte(tag);
            if (type == ScriptType::Int) {
                std::uint64_t bits = 0;
                if (!r.u64(bits)) return truncated(r, "constant");
                constant.type = ScriptType::Int;
                constant.intValue = static_cast<std::int64_t>(bits);
            } else if (type == ScriptType::Float) {
                std::uint64_t bits = 0;
                if (!r.u64(bits)) return truncated(r, "constant");
                constant.type = ScriptType::Float;
                std::memcpy(&constant.floatValue, &bits, sizeof bits);
            } else if (type == ScriptType::String) {
                constant.type = ScriptType::String;
                if (!r.u32(constant.stringIndex)) return truncated(r, "constant");
            } else {
                return invalid(concat("constant with unsupported type tag ", static_cast<unsigned>(tag), " at offset ",
                                      r.offset() - 1));
            }
        }
        if (auto s = checkEnd(r, "constant"); !s) return std::unexpected(s.error());
    }
    {   // Imports
        ByteReader r = reader(format::SectionId::Imports);
        std::uint32_t count = 0;
        if (!r.u32(count) || !plausibleCount(r, count, 6)) return truncated(r, "import table");
        module.imports.resize(count);
        for (NativeImport& import : module.imports) {
            std::uint8_t returnType = 0;
            if (!r.u32(import.nameIndex) || !r.u8(returnType)) return truncated(r, "native import");
            const auto type = scriptTypeFromByte(returnType);
            if (!type) return invalid(concat("unknown return type at offset ", r.offset() - 1));
            import.returnType = *type;
            auto parameters = readTypes(r, "native import");
            if (!parameters) return std::unexpected(parameters.error());
            import.parameters = std::move(*parameters);
        }
        if (auto s = checkEnd(r, "import"); !s) return std::unexpected(s.error());
    }
    std::vector<RawFunction> rawFunctions;
    {   // Functions
        ByteReader r = reader(format::SectionId::Functions);
        std::uint32_t count = 0;
        if (!r.u32(count) || !plausibleCount(r, count, 20)) return truncated(r, "function table");
        module.functions.resize(count);
        rawFunctions.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Function& function = module.functions[i];
            std::uint8_t returnType = 0;
            if (!r.u32(function.nameIndex) || !r.u8(returnType)) return truncated(r, "function");
            const auto type = scriptTypeFromByte(returnType);
            if (!type) return invalid(concat("unknown return type at offset ", r.offset() - 1));
            function.returnType = *type;
            auto parameters = readTypes(r, "function");
            if (!parameters) return std::unexpected(parameters.error());
            function.parameters = std::move(*parameters);
            RawFunction& raw = rawFunctions[i];
            if (!r.u16(function.registerCount) || !r.u32(raw.codeOffset) || !r.u32(raw.codeLength) ||
                !r.u32(raw.debugOffset)) {
                return truncated(r, "function");
            }
        }
        if (auto s = checkEnd(r, "function"); !s) return std::unexpected(s.error());
    }
    if (hasObjects) {   // Objects
        ByteReader r = reader(format::SectionId::Objects);
        std::uint32_t count = 0;
        if (!r.u32(count) || !plausibleCount(r, count, 8)) return truncated(r, "object table");
        module.objects.resize(count);
        for (ObjectEntry& object : module.objects) {
            std::uint32_t handlers = 0;
            if (!r.u32(object.nameIndex) || !r.u32(handlers) || !plausibleCount(r, handlers, 8)) {
                return truncated(r, "object");
            }
            object.handlers.resize(handlers);
            for (HandlerEntry& handler : object.handlers) {
                if (!r.u32(handler.nameIndex) || !r.u32(handler.function)) return truncated(r, "handler");
            }
        }
        if (auto s = checkEnd(r, "object"); !s) return std::unexpected(s.error());
    }
    std::vector<Instruction> code;
    {   // Code
        ByteReader r = reader(format::SectionId::Code);
        std::uint32_t count = 0;
        if (!r.u32(count) || !plausibleCount(r, count, format::instructionSize)) return truncated(r, "code");
        code.resize(count);
        for (Instruction& instruction : code) {
            const std::size_t offset = r.offset();
            std::uint8_t opcode = 0;
            std::uint8_t reserved = 0;
            if (!r.u8(opcode) || !r.u8(reserved) || !r.u16(instruction.a) || !r.u16(instruction.b) || !r.u16(instruction.c)) {
                return truncated(r, "instruction");
            }
            const auto decoded = opcodeFromByte(opcode);
            if (!decoded) return invalid(concat("unknown opcode ", static_cast<unsigned>(opcode), " at offset ", offset));
            if (reserved != 0) return invalid(concat("reserved instruction byte is not 0 at offset ", offset + 1));
            instruction.opcode = *decoded;
        }
        if (auto s = checkEnd(r, "code"); !s) return std::unexpected(s.error());
    }
    std::vector<SourcePosition> positions;
    {   // Debug
        ByteReader r = reader(format::SectionId::Debug);
        std::uint32_t count = 0;
        if (!r.u32(module.sourceNameIndex) || !r.u32(count) || !plausibleCount(r, count, format::debugEntrySize)) {
            return truncated(r, "debug information");
        }
        positions.resize(count);
        for (SourcePosition& position : positions) {
            if (!r.u32(position.line) || !r.u32(position.column)) return truncated(r, "debug entry");
        }
        if (auto s = checkEnd(r, "debug"); !s) return std::unexpected(s.error());
    }
    for (std::size_t i = 0; i < rawFunctions.size(); ++i) {
        const RawFunction& raw = rawFunctions[i];
        const std::optional<std::size_t> codeEnd = checkedAdd(raw.codeOffset, raw.codeLength);
        const std::optional<std::size_t> debugEnd = checkedAdd(raw.debugOffset, raw.codeLength);
        if (!codeEnd || *codeEnd > code.size()) {
            return invalid(concat("function ", i, " code range ", raw.codeOffset, "+", raw.codeLength, " is outside the code section"));
        }
        if (!debugEnd || *debugEnd > positions.size()) {
            return invalid(concat("function ", i, " debug range is outside the debug section"));
        }
        Function& function = module.functions[i];
        function.code.assign(code.begin() + raw.codeOffset, code.begin() + static_cast<std::ptrdiff_t>(*codeEnd));
        function.positions.assign(positions.begin() + raw.debugOffset, positions.begin() + static_cast<std::ptrdiff_t>(*debugEnd));
    }
    if (auto status = validateModule(module); !status) {
        return std::unexpected(status.error());
    }
    return module;
}

} // namespace schuimm::script
