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

// A type: its kind byte, followed by a u16 enum index for enum types.
bool readType(ByteReader& reader, TypeRef& out, std::string& problem)
{
    std::uint8_t raw = 0;
    if (!reader.u8(raw)) {
        problem = concat("truncated type at offset ", reader.offset());
        return false;
    }
    const auto type = scriptTypeFromByte(raw);
    if (!type) {
        problem = concat("unknown type ", static_cast<unsigned>(raw), " at offset ", reader.offset() - 1);
        return false;
    }
    std::uint16_t index = 0;
    if (*type == ScriptType::Enum && !reader.u16(index)) {
        problem = concat("truncated enum type at offset ", reader.offset());
        return false;
    }
    out = TypeRef(*type, index);
    return true;
}

Result<std::vector<TypeRef>> readTypes(ByteReader& reader, const char* what)
{
    std::uint8_t count = 0;
    if (!reader.u8(count)) {
        return invalid(concat("truncated ", what, " signature at offset ", reader.offset()));
    }
    std::vector<TypeRef> types(count);
    for (TypeRef& type : types) {
        std::string problem;
        if (!readType(reader, type, problem)) return invalid(problem);
    }
    return types;
}

// A variable entry: name, type and the initial value in the encoding of the type.
bool readVariable(ByteReader& reader, VariableEntry& variable, std::string& problem)
{
    if (!reader.u32(variable.nameIndex)) {
        problem = concat("truncated variable at offset ", reader.offset());
        return false;
    }
    if (!readType(reader, variable.type, problem)) return false;
    InitialValue& initial = variable.initial;
    std::uint64_t bits = 0;
    bool ok = true;
    switch (variable.type.kind) {
    case ScriptType::Bool: {
        std::uint8_t value = 0;
        ok = reader.u8(value);
        if (ok && value > 1) {
            problem = concat("invalid bool value ", static_cast<unsigned>(value), " at offset ", reader.offset() - 1);
            return false;
        }
        initial.boolValue = value != 0;
        break;
    }
    case ScriptType::Int:
        ok = reader.u64(bits);
        initial.intValue = static_cast<std::int64_t>(bits);
        break;
    case ScriptType::Float:
        ok = reader.u64(bits);
        std::memcpy(&initial.x, &bits, sizeof bits);
        break;
    case ScriptType::String: ok = reader.u32(initial.stringIndex); break;
    case ScriptType::Vec2:
        ok = reader.u64(bits);
        std::memcpy(&initial.x, &bits, sizeof bits);
        ok = ok && reader.u64(bits);
        std::memcpy(&initial.y, &bits, sizeof bits);
        break;
    case ScriptType::Enum: ok = reader.u32(initial.ordinal); break;
    default: break;
    }
    if (!ok) {
        problem = concat("truncated variable value at offset ", reader.offset());
        return false;
    }
    return true;
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
    // Per function: the behaviour whose handler it is, or noOwner (global function).
    std::vector<std::uint32_t> owner = {};
    std::size_t checking = 0; // The function whose instructions are being checked.

    static constexpr std::uint32_t noOwner = 0xFFFFFFFFu;

    [[nodiscard]] std::string functionName(std::size_t index) const
    {
        const Function& function = module.functions[index];
        std::string name = function.nameIndex < module.strings.size() ? module.strings[function.nameIndex] : concat("#", index);
        if (index < owner.size() && owner[index] != noOwner) {
            const BehaviourEntry& behaviour = module.behaviours[owner[index]];
            const std::string prefix =
                behaviour.kind == BehaviourKind::Object ? module.strings[behaviour.nameIndex] : toString(behaviour.kind);
            return concat(prefix, ".", name);
        }
        return name;
    }

    [[nodiscard]] std::string typeName(TypeRef type) const
    {
        if (type.kind == ScriptType::Enum && type.enumIndex < module.enums.size()) {
            return module.strings[module.enums[type.enumIndex].nameIndex];
        }
        return toString(type.kind);
    }

    Status checkType(TypeRef type) const
    {
        if (type.kind == ScriptType::Enum && type.enumIndex >= module.enums.size()) {
            return bad(concat("enum type ", type.enumIndex, " does not exist"));
        }
        return {};
    }

    // Script-declared enums have unique names; engine enums are unique per qualifier and name.
    Status checkEnums()
    {
        std::set<std::pair<std::uint32_t, std::string_view>> names;
        for (std::size_t e = 0; e < module.enums.size(); ++e) {
            const EnumEntry& entry = module.enums[e];
            if (entry.nameIndex >= module.strings.size()) return invalid(concat("enum ", e, " refers to a missing name"));
            if (entry.isEngine() && entry.qualifierIndex >= module.strings.size()) {
                return invalid(concat("enum ", e, " refers to a missing interface name"));
            }
            const std::string& name = module.strings[entry.nameIndex];
            if (!names.insert({entry.qualifierIndex, name}).second) return invalid(concat("duplicate enum '", name, "'"));
            if (entry.members.empty() || entry.members.size() > 0x10000) {
                return invalid(concat("enum '", name, "' must have 1 to 65536 members"));
            }
            std::set<std::string_view> members;
            for (const std::uint32_t member : entry.members) {
                if (member >= module.strings.size()) return invalid(concat("enum '", name, "' refers to a missing member name"));
                if (!members.insert(module.strings[member]).second) {
                    return invalid(concat("duplicate member '", module.strings[member], "' in enum '", name, "'"));
                }
            }
        }
        return {};
    }

    // Global or instance variables: existing, unique names; a value type (not None or any);
    // an initial value that fits it (an existing string, an existing enum member).
    Status checkVariables(const std::vector<VariableEntry>& variables, const std::string& kind) const
    {
        std::set<std::string_view> names;
        for (std::size_t v = 0; v < variables.size(); ++v) {
            const VariableEntry& variable = variables[v];
            if (variable.nameIndex >= module.strings.size()) {
                return invalid(concat(kind, " variable ", v, " refers to a missing name"));
            }
            const std::string& name = module.strings[variable.nameIndex];
            if (!names.insert(name).second) return invalid(concat("duplicate ", kind, " variable '", name, "'"));
            const TypeRef type = variable.type;
            if (type.kind == ScriptType::None || type.kind == ScriptType::Any) {
                return invalid(concat(kind, " variable '", name, "' has type ", toString(type.kind)));
            }
            if (auto status = checkType(type); !status) {
                return invalid(concat(kind, " variable '", name, "': ", status.error().message));
            }
            if (type.kind == ScriptType::String && variable.initial.stringIndex >= module.strings.size()) {
                return invalid(concat(kind, " variable '", name, "' starts with missing string ", variable.initial.stringIndex));
            }
            if (type.kind == ScriptType::Enum && variable.initial.ordinal >= module.enums[type.enumIndex].members.size()) {
                return invalid(concat(kind, " variable '", name, "' starts with member ", variable.initial.ordinal,
                                      ", which its enum does not have"));
            }
        }
        return {};
    }

    // The Sources table: at least one source, with existing, unique paths.
    Status checkSources()
    {
        if (module.sources.empty()) {
            return invalid("the module has no sources");
        }
        std::set<std::string_view> paths;
        for (std::size_t i = 0; i < module.sources.size(); ++i) {
            const SourceEntry& source = module.sources[i];
            if (source.pathIndex >= module.strings.size()) {
                return invalid(concat("source ", i, " refers to a missing path"));
            }
            if (!paths.insert(module.strings[source.pathIndex]).second) {
                return invalid(concat("duplicate source '", module.strings[source.pathIndex], "'"));
            }
        }
        return {};
    }

    // The Behaviours table:
    //   * every behaviour belongs to an existing source of the matching role: room and object
    //     behaviours to room scripts, actor behaviours to actor scripts;
    //   * a source has at most one room and one actor behaviour; object behaviours are named,
    //     uniquely per source, the others unnamed;
    //   * handler names are unique per behaviour; each handler is an existing function of the same
    //     name and source, and no function is the handler of two events;
    //   * an object handler takes the object (self) as parameter 0, an actor handler the actor.
    Status checkBehaviours()
    {
        owner.assign(module.functions.size(), noOwner);
        std::set<std::pair<std::uint32_t, std::string>> objectNames;
        std::set<std::pair<std::uint32_t, BehaviourKind>> unnamed;
        for (std::size_t b = 0; b < module.behaviours.size(); ++b) {
            const BehaviourEntry& behaviour = module.behaviours[b];
            if (behaviour.source >= module.sources.size()) {
                return invalid(concat("behaviour ", b, " refers to missing source ", behaviour.source));
            }
            const ScriptRole role = module.sources[behaviour.source].role;
            const ScriptRole expected = behaviour.kind == BehaviourKind::Actor ? ScriptRole::Actor : ScriptRole::Room;
            if (role != expected) {
                return invalid(concat("behaviour ", b, " (", toString(behaviour.kind), ") belongs to a ", toString(role),
                                      " script"));
            }
            std::string name;
            if (behaviour.kind == BehaviourKind::Object) {
                if (behaviour.nameIndex >= module.strings.size()) {
                    return invalid(concat("object behaviour ", b, " refers to a missing name"));
                }
                name = module.strings[behaviour.nameIndex];
                if (!objectNames.insert({behaviour.source, name}).second) {
                    return invalid(concat("duplicate object '", name, "' in source '", module.sourcePath(behaviour.source), "'"));
                }
            } else {
                if (behaviour.nameIndex != noString) {
                    return invalid(concat("behaviour ", b, " (", toString(behaviour.kind), ") must not have a name"));
                }
                name = toString(behaviour.kind);
                if (!unnamed.insert({behaviour.source, behaviour.kind}).second) {
                    return invalid(concat("source '", module.sourcePath(behaviour.source), "' has more than one ", name,
                                          " behaviour"));
                }
            }
            std::set<std::string_view> handlerNames;
            for (std::size_t h = 0; h < behaviour.handlers.size(); ++h) {
                const HandlerEntry& handler = behaviour.handlers[h];
                if (handler.nameIndex >= module.strings.size()) {
                    return invalid(concat("handler ", h, " of '", name, "' refers to a missing name"));
                }
                const std::string& handlerName = module.strings[handler.nameIndex];
                if (!handlerNames.insert(handlerName).second) {
                    return invalid(concat("duplicate handler '", handlerName, "' in '", name, "'"));
                }
                if (handler.function >= module.functions.size()) {
                    return invalid(concat("handler '", name, ".", handlerName, "' refers to missing function ", handler.function));
                }
                if (owner[handler.function] != noOwner) {
                    return invalid(concat("function ", handler.function, " is the handler of more than one event"));
                }
                const Function& function = module.functions[handler.function];
                if (function.nameIndex >= module.strings.size() || module.strings[function.nameIndex] != handlerName) {
                    return invalid(concat("handler '", name, ".", handlerName, "' refers to function ", handler.function,
                                          ", which has a different name"));
                }
                if (function.source != behaviour.source) {
                    return invalid(concat("handler '", name, ".", handlerName, "' is a function of another source"));
                }
                if (behaviour.kind != BehaviourKind::Room) {
                    const TypeRef self(behaviour.kind == BehaviourKind::Actor ? ScriptType::Actor : ScriptType::Object);
                    if (function.parameters.empty() || function.parameters[0] != self) {
                        return invalid(concat("handler '", name, ".", handlerName, "' must take the ", toString(self.kind),
                                              " (self) as its first parameter"));
                    }
                }
                owner[handler.function] = static_cast<std::uint32_t>(b);
            }
            if (auto status = checkVariables(behaviour.variables, concat("instance (", name, ")")); !status) return status;
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
        if (auto status = checkSources(); !status) {
            return status;
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
        if (module.enums.size() > 0x10000) {
            return invalid("more than 65536 enums");
        }
        if (auto status = checkEnums(); !status) {
            return status;
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
        if (auto status = checkVariables(module.globals, "global"); !status) {
            return status;
        }
        if (auto status = checkBehaviours(); !status) {
            return status;
        }
        names.clear();
        for (std::size_t i = 0; i < module.functions.size(); ++i) {
            const Function& function = module.functions[i];
            if (function.nameIndex >= module.strings.size()) {
                return invalid(concat("function ", i, " refers to a missing name"));
            }
            if (function.source != noSource && function.source >= module.sources.size()) {
                return invalid(concat("function ", i, " refers to missing source ", function.source));
            }
            // Global function names are unique; a handler's name is unique within its behaviour.
            if (owner[i] == noOwner && !names.insert(module.strings[function.nameIndex]).second) {
                return invalid(concat("duplicate function '", module.strings[function.nameIndex], "'"));
            }
            if (auto status = checkSignature(function.parameters, function.returnType, false); !status) {
                return invalid(concat("function '", functionName(i), "': ", status.error().message));
            }
            if (function.registerTypes.size() > 0xFFFF) {
                return invalid(concat("function '", functionName(i), "' has more than 65535 registers"));
            }
            if (function.parameters.size() > function.registerTypes.size()) {
                return invalid(concat("function '", functionName(i), "' has fewer registers than parameters"));
            }
            for (std::size_t p = 0; p < function.parameters.size(); ++p) {
                if (function.registerTypes[p] != function.parameters[p]) {
                    return invalid(concat("function '", functionName(i), "': register ", p,
                                          " does not have the type of parameter ", p + 1));
                }
            }
            for (std::size_t reg = 0; reg < function.registerTypes.size(); ++reg) {
                const TypeRef type = function.registerTypes[reg];
                if (type.kind == ScriptType::Any) {
                    return invalid(concat("function '", functionName(i), "': register ", reg, " has type any"));
                }
                if (auto status = checkType(type); !status) {
                    return invalid(concat("function '", functionName(i), "': register ", reg, ": ", status.error().message));
                }
            }
            if (function.code.empty()) {
                return invalid(concat("function '", functionName(i), "' has no code"));
            }
            if (function.positions.size() != function.code.size()) {
                return invalid(concat("function '", functionName(i), "' debug information does not match its code"));
            }
            checking = i;
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

    Status checkSignature(const std::vector<TypeRef>& parameters, TypeRef returnType, bool native) const
    {
        if (parameters.size() > format::maxParameters) {
            return bad("too many parameters");
        }
        for (const TypeRef type : parameters) {
            if (type.kind == ScriptType::None || (type.kind == ScriptType::Any && !native)) {
                return bad(concat("invalid parameter type ", toString(type.kind)));
            }
            if (auto status = checkType(type); !status) return status;
        }
        if (returnType.kind == ScriptType::Any) {
            return bad("invalid return type any");
        }
        return checkType(returnType);
    }

    // An instance variable of the behaviour of the function being checked (checked to exist).
    [[nodiscard]] const VariableEntry& instanceVariable(std::uint16_t index) const
    {
        return module.behaviours[owner[checking]].variables[index];
    }

    Status checkOperand(const Function& function, OperandKind kind, std::uint16_t value, const char* field) const
    {
        switch (kind) {
        case OperandKind::Unused:
            if (value != 0) return bad(concat("unused operand ", field, " is ", value, ", expected 0"));
            return {};
        case OperandKind::Register:
            if (value >= function.registerCount()) {
                return bad(concat("register ", value, " in ", field, " out of range (", function.registerCount(), " registers)"));
            }
            return {};
        case OperandKind::OptRegister:
            if (value != noRegister && value >= function.registerCount()) {
                return bad(concat("register ", value, " in ", field, " out of range (", function.registerCount(), " registers)"));
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
        case OperandKind::Enum:
            if (value >= module.enums.size()) return bad(concat("enum ", value, " does not exist"));
            return {};
        case OperandKind::Global:
            if (value >= module.globals.size()) return bad(concat("global variable ", value, " does not exist"));
            return {};
        case OperandKind::Instance: {
            const std::uint32_t behaviour = checking < owner.size() ? owner[checking] : noOwner;
            if (behaviour == noOwner) {
                return bad("an instance variable is used outside a behaviour (by a global function)");
            }
            if (value >= module.behaviours[behaviour].variables.size()) {
                return bad(concat("instance variable ", value, " does not exist"));
            }
            return {};
        }
        case OperandKind::ArgWindow:
        case OperandKind::Target:
        case OperandKind::Immediate:
            return {}; // Checked with the whole instruction.
        }
        return bad("unknown operand kind");
    }

    Status checkResult(const Function& function, std::uint16_t destination, TypeRef returnType) const
    {
        if (returnType.kind == ScriptType::None) {
            if (destination != noRegister) return bad("call of a function without result stores a result");
            return {};
        }
        if (destination == noRegister) {
            return bad("call result has no destination register");
        }
        if (!isAssignableType(returnType, function.registerTypes[destination])) {
            return bad(concat("result of type ", typeName(returnType), " stored in register ", destination, " of type ",
                              typeName(function.registerTypes[destination])));
        }
        return {};
    }

    // Register type rules of each instruction (Bytecode.md, "Instructions").
    Status checkTypes(const Function& function, const Instruction& in) const
    {
        const auto& types = function.registerTypes;
        const auto expect = [&](std::uint16_t reg, TypeRef type, const char* field) -> Status {
            if (types[reg] != type) {
                return bad(concat("register ", reg, " in ", field, " has type ", typeName(types[reg]), ", expected ",
                                  typeName(type)));
            }
            return {};
        };
        const auto all = [&](TypeRef a, TypeRef b, TypeRef c) -> Status {
            if (auto s = expect(in.a, a, "a"); !s) return s;
            if (auto s = expect(in.b, b, "b"); !s) return s;
            return expect(in.c, c, "c");
        };
        const auto unary = [&](TypeRef a, TypeRef b) -> Status {
            if (auto s = expect(in.a, a, "a"); !s) return s;
            return expect(in.b, b, "b");
        };
        using enum ScriptType;
        switch (in.opcode) {
        case Opcode::Nop:
        case Opcode::Jump:
        case Opcode::Unreachable: return {};
        case Opcode::LoadConst: return expect(in.a, module.constants[in.b].type, "a");
        case Opcode::LoadTrue:
        case Opcode::LoadFalse: return expect(in.a, Bool, "a");
        case Opcode::LoadNone: return expect(in.a, None, "a");
        case Opcode::Move:
            if (!isAssignableType(types[in.b], types[in.a])) {
                return bad(concat("cannot move a ", typeName(types[in.b]), " into a register of type ", typeName(types[in.a])));
            }
            return {};
        case Opcode::NegInt: return unary(Int, Int);
        case Opcode::NegFloat: return unary(Float, Float);
        case Opcode::Not: return unary(Bool, Bool);
        case Opcode::AddInt:
        case Opcode::SubInt:
        case Opcode::MulInt:
        case Opcode::DivInt:
        case Opcode::ModInt:
        case Opcode::AddIntSat: return all(Int, Int, Int);
        case Opcode::AddFloat:
        case Opcode::SubFloat:
        case Opcode::MulFloat:
        case Opcode::DivFloat:
        case Opcode::FloorDivFloat:
        case Opcode::ModFloat: return all(Float, Float, Float);
        case Opcode::Concat: return all(String, String, String);
        case Opcode::EqInt:
        case Opcode::NeInt:
        case Opcode::LtInt:
        case Opcode::LeInt:
        case Opcode::GtInt:
        case Opcode::GeInt: return all(Bool, Int, Int);
        case Opcode::EqFloat:
        case Opcode::NeFloat:
        case Opcode::LtFloat:
        case Opcode::LeFloat:
        case Opcode::GtFloat:
        case Opcode::GeFloat: return all(Bool, Float, Float);
        case Opcode::EqBool:
        case Opcode::NeBool: return all(Bool, Bool, Bool);
        case Opcode::EqString:
        case Opcode::NeString:
        case Opcode::LtString:
        case Opcode::LeString:
        case Opcode::GtString:
        case Opcode::GeString: return all(Bool, String, String);
        case Opcode::IntToFloat: return unary(Float, Int);
        case Opcode::FloatToInt: return unary(Int, Float);
        case Opcode::BoolToInt: return unary(Int, Bool);
        case Opcode::IntToString: return unary(String, Int);
        case Opcode::FloatToString: return unary(String, Float);
        case Opcode::BoolToString: return unary(String, Bool);
        case Opcode::CheckRangeStep: return expect(in.a, Int, "a");
        case Opcode::JumpIfTrue:
        case Opcode::JumpIfFalse: return expect(in.a, Bool, "a");
        case Opcode::EqRef:
        case Opcode::NeRef: {
            if (auto s = expect(in.a, Bool, "a"); !s) return s;
            const ScriptType left = types[in.b].kind;
            const ScriptType right = types[in.c].kind;
            if (!isEntityReference(left) || !isEntityReference(right)) return bad("operands must be entity references");
            if ((left == Actor && right == Object) || (left == Object && right == Actor)) {
                return bad("an actor reference is compared with an object reference");
            }
            return {};
        }
        case Opcode::LoadEnum:
            if (auto s = expect(in.a, TypeRef::enumType(in.b), "a"); !s) return s;
            if (in.c >= module.enums[in.b].members.size()) {
                return bad(concat("enum '", typeName(TypeRef::enumType(in.b)), "' has no member ", in.c));
            }
            return {};
        case Opcode::EqEnum:
        case Opcode::NeEnum:
            if (auto s = expect(in.a, Bool, "a"); !s) return s;
            if (types[in.b].kind != Enum || types[in.b] != types[in.c]) return bad("operands must be values of the same enum");
            return {};
        case Opcode::MakeVec2: return all(Vec2, Float, Float);
        case Opcode::Vec2X:
        case Opcode::Vec2Y: return unary(Float, Vec2);
        case Opcode::SetVec2X:
        case Opcode::SetVec2Y: return unary(Vec2, Float);
        case Opcode::AddVec2:
        case Opcode::SubVec2: return all(Vec2, Vec2, Vec2);
        case Opcode::MulVec2:
        case Opcode::DivVec2: return all(Vec2, Vec2, Float);
        case Opcode::NegVec2: return unary(Vec2, Vec2);
        case Opcode::EqVec2:
        case Opcode::NeVec2: return all(Bool, Vec2, Vec2);
        case Opcode::LoadNull:
            if (!isNullableType(types[in.a].kind)) {
                return bad(concat("register ", in.a, " of type ", typeName(types[in.a]), " has no null value"));
            }
            return {};
        case Opcode::LoadGlobal: return expect(in.a, module.globals[in.b].type, "a");
        case Opcode::StoreGlobal:
            if (!isAssignableType(types[in.b], module.globals[in.a].type)) {
                return bad(concat("cannot store a ", typeName(types[in.b]), " in global variable ", in.a, " of type ",
                                  typeName(module.globals[in.a].type)));
            }
            return {};
        case Opcode::LoadInstance: return expect(in.a, instanceVariable(in.b).type, "a");
        case Opcode::StoreInstance:
            if (!isAssignableType(types[in.b], instanceVariable(in.a).type)) {
                return bad(concat("cannot store a ", typeName(types[in.b]), " in instance variable ", in.a, " of type ",
                                  typeName(instanceVariable(in.a).type)));
            }
            return {};
        case Opcode::Call:
        case Opcode::CallNative:
        case Opcode::Spawn:
        case Opcode::Return: return {}; // Checked with the signature (checkInstruction).
        }
        return bad("unknown opcode");
    }

    Status checkInstruction(const Function& function, std::size_t ip) const
    {
        const Instruction& instruction = function.code[ip];
        const OpcodeInfo& info = opcodeInfo(instruction.opcode);
        if (auto s = checkOperand(function, info.a, instruction.a, "a"); !s) return s;
        if (info.b == OperandKind::Target) {
            if (instruction.target() >= function.code.size()) {
                return bad(concat("branch target ", instruction.target(), " outside the function (", function.code.size(),
                                      " instructions)"));
            }
            return checkTypes(function, instruction);
        }
        if (auto s = checkOperand(function, info.b, instruction.b, "b"); !s) return s;
        if (auto s = checkOperand(function, info.c, instruction.c, "c"); !s) return s;

        switch (instruction.opcode) {
        case Opcode::Call:
        case Opcode::CallNative:
        case Opcode::Spawn: {
            const bool native = instruction.opcode == Opcode::CallNative;
            const bool spawn = instruction.opcode == Opcode::Spawn;
            if (!native && owner[instruction.b] != noOwner) {
                return bad(concat(spawn ? "spawns handler '" : "calls handler '", functionName(instruction.b),
                                  "'; handlers are only started by event dispatch"));
            }
            const std::vector<TypeRef>& parameters =
                native ? module.imports[instruction.b].parameters : module.functions[instruction.b].parameters;
            const TypeRef returnType =
                native ? module.imports[instruction.b].returnType : module.functions[instruction.b].returnType;
            const std::size_t end = static_cast<std::size_t>(instruction.c) + parameters.size();
            if (!parameters.empty() && end > function.registerCount()) {
                return bad(concat("argument registers ", instruction.c, "..", end - 1, " out of range (",
                                      function.registerCount(), " registers)"));
            }
            if (parameters.empty() && instruction.c != 0) {
                return bad("argument window of a call without arguments must be 0");
            }
            if (spawn) {
                // The result of a spawn is the coroutine handle, not the function's result.
                if (instruction.a != noRegister && function.registerTypes[instruction.a].kind != ScriptType::Coroutine) {
                    return bad(concat("spawn stores its coroutine in register ", instruction.a, " of type ",
                                      typeName(function.registerTypes[instruction.a])));
                }
            } else if (auto s = checkResult(function, instruction.a, returnType); !s) {
                return s;
            }
            for (std::size_t i = 0; i < parameters.size(); ++i) {
                const TypeRef argument = function.registerTypes[instruction.c + i];
                if (!isAssignableType(argument, parameters[i])) {
                    return bad(concat("argument ", i + 1, " has type ", typeName(argument), ", expected ",
                                      typeName(parameters[i])));
                }
            }
            return {};
        }
        case Opcode::Return:
            if (function.returnType.kind == ScriptType::None && instruction.a != noRegister) {
                return bad("function without result returns a value");
            }
            if (function.returnType.kind != ScriptType::None) {
                if (instruction.a == noRegister) return bad("function with a result returns no value");
                if (!isAssignableType(function.registerTypes[instruction.a], function.returnType)) {
                    return bad(concat("returns a ", typeName(function.registerTypes[instruction.a]), " from a function returning ",
                                      typeName(function.returnType)));
                }
            }
            return {};
        default: return checkTypes(function, instruction);
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
                                                        ".0 to ", bytecodeFormatMajor, ".", bytecodeFormatMinor,
                                                        "); recompile the script from its source"));
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
    const std::uint32_t expectedSections = format::sectionCount;
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
            if (!r.u32(import.nameIndex)) return truncated(r, "native import");
            std::string problem;
            if (!readType(r, import.returnType, problem)) return invalid(problem);
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
        if (!r.u32(count) || !plausibleCount(r, count, 24)) return truncated(r, "function table");
        module.functions.resize(count);
        rawFunctions.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Function& function = module.functions[i];
            if (!r.u32(function.nameIndex) || !r.u32(function.source)) return truncated(r, "function");
            std::string problem;
            if (!readType(r, function.returnType, problem)) return invalid(problem);
            auto parameters = readTypes(r, "function");
            if (!parameters) return std::unexpected(parameters.error());
            function.parameters = std::move(*parameters);
            std::uint16_t registers = 0;
            if (!r.u16(registers) || !plausibleCount(r, registers, 1)) return truncated(r, "function");
            function.registerTypes.resize(registers);
            for (TypeRef& type : function.registerTypes) {
                if (!readType(r, type, problem)) return invalid(problem);
            }
            RawFunction& raw = rawFunctions[i];
            if (!r.u32(raw.codeOffset) || !r.u32(raw.codeLength) || !r.u32(raw.debugOffset)) {
                return truncated(r, "function");
            }
        }
        if (auto s = checkEnd(r, "function"); !s) return std::unexpected(s.error());
    }
    {   // Behaviours
        ByteReader r = reader(format::SectionId::Behaviours);
        std::uint32_t count = 0;
        if (!r.u32(count) || !plausibleCount(r, count, 17)) return truncated(r, "behaviour table");
        module.behaviours.resize(count);
        for (BehaviourEntry& behaviour : module.behaviours) {
            std::uint8_t kind = 0;
            std::uint32_t handlers = 0;
            if (!r.u8(kind) || !r.u32(behaviour.source) || !r.u32(behaviour.nameIndex) || !r.u32(handlers) ||
                !plausibleCount(r, handlers, 8)) {
                return truncated(r, "behaviour");
            }
            if (kind >= behaviourKindCount) return invalid(concat("unknown behaviour kind ", static_cast<unsigned>(kind)));
            behaviour.kind = static_cast<BehaviourKind>(kind);
            behaviour.handlers.resize(handlers);
            for (HandlerEntry& handler : behaviour.handlers) {
                if (!r.u32(handler.nameIndex) || !r.u32(handler.function)) return truncated(r, "handler");
            }
            std::uint32_t variables = 0;
            if (!r.u32(variables) || !plausibleCount(r, variables, 5)) return truncated(r, "instance variables");
            behaviour.variables.resize(variables);
            for (VariableEntry& variable : behaviour.variables) {
                std::string problem;
                if (!readVariable(r, variable, problem)) return invalid(problem);
            }
        }
        if (auto s = checkEnd(r, "behaviour"); !s) return std::unexpected(s.error());
    }
    {   // Sources
        ByteReader r = reader(format::SectionId::Sources);
        std::uint32_t count = 0;
        if (!r.u32(count) || !plausibleCount(r, count, 5)) return truncated(r, "source table");
        module.sources.resize(count);
        for (SourceEntry& source : module.sources) {
            std::uint8_t role = 0;
            if (!r.u32(source.pathIndex) || !r.u8(role)) return truncated(r, "source");
            if (role >= scriptRoleCount) return invalid(concat("unknown script role ", static_cast<unsigned>(role)));
            source.role = static_cast<ScriptRole>(role);
        }
        if (auto s = checkEnd(r, "source"); !s) return std::unexpected(s.error());
    }
    {   // Globals
        ByteReader r = reader(format::SectionId::Globals);
        std::uint32_t count = 0;
        if (!r.u32(count) || !plausibleCount(r, count, 5)) return truncated(r, "global table");
        module.globals.resize(count);
        for (VariableEntry& variable : module.globals) {
            std::string problem;
            if (!readVariable(r, variable, problem)) return invalid(problem);
        }
        if (auto s = checkEnd(r, "global"); !s) return std::unexpected(s.error());
    }
    {   // Enums
        ByteReader r = reader(format::SectionId::Enums);
        std::uint32_t count = 0;
        if (!r.u32(count) || !plausibleCount(r, count, 12)) return truncated(r, "enum table");
        module.enums.resize(count);
        for (EnumEntry& entry : module.enums) {
            std::uint32_t members = 0;
            if (!r.u32(entry.nameIndex) || !r.u32(entry.qualifierIndex) || !r.u32(members) ||
                !plausibleCount(r, members, 4)) {
                return truncated(r, "enum");
            }
            entry.members.resize(members);
            for (std::uint32_t& member : entry.members) {
                if (!r.u32(member)) return truncated(r, "enum member");
            }
        }
        if (auto s = checkEnd(r, "enum"); !s) return std::unexpected(s.error());
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
        if (!r.u32(count) || !plausibleCount(r, count, format::debugEntrySize)) {
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
