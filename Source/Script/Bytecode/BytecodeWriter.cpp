#include "Script/Bytecode/BytecodeWriter.h"

#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/BytecodeFormat.h"
#include "Script/Bytecode/Crc32.h"

#include <cstring>
#include <limits>

namespace schuimm::script {

namespace {

class ByteWriter {
public:
    void u8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }
    void u16(std::uint16_t value)
    {
        u8(static_cast<std::uint8_t>(value & 0xFFu));
        u8(static_cast<std::uint8_t>(value >> 8));
    }
    void u32(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
        }
    }
    void u64(std::uint64_t value)
    {
        for (int shift = 0; shift < 64; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
        }
    }
    void bytes(const std::string& text)
    {
        for (const char ch : text) {
            u8(static_cast<std::uint8_t>(ch));
        }
    }
    void patchU32(std::size_t offset, std::uint32_t value)
    {
        for (int i = 0; i < 4; ++i) {
            bytes_[offset + static_cast<std::size_t>(i)] = static_cast<std::byte>((value >> (8 * i)) & 0xFFu);
        }
    }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] std::vector<std::byte>& data() noexcept { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

bool fitsU32(std::size_t value) noexcept { return value <= std::numeric_limits<std::uint32_t>::max(); }

Status tooLarge(const char* what) { return makeError(ErrorCode::InvalidArgument, concat("Bytecode: too many ", what)); }

void writeTypes(ByteWriter& out, const std::vector<ScriptType>& types)
{
    out.u8(static_cast<std::uint8_t>(types.size()));
    for (const ScriptType type : types) {
        out.u8(static_cast<std::uint8_t>(type));
    }
}

} // namespace

Result<std::vector<std::byte>> writeBytecode(const Module& module)
{
    if (!fitsU32(module.strings.size()) || !fitsU32(module.constants.size()) || !fitsU32(module.imports.size()) ||
        !fitsU32(module.functions.size())) {
        return std::unexpected(tooLarge("table entries").error());
    }
    std::size_t totalInstructions = 0;
    for (const Function& function : module.functions) {
        if (function.parameters.size() > format::maxParameters) {
            return std::unexpected(tooLarge("parameters").error());
        }
        if (function.positions.size() != function.code.size()) {
            return makeError(ErrorCode::InvalidArgument, "Bytecode: debug positions do not match the code");
        }
        totalInstructions += function.code.size();
    }
    for (const NativeImport& import : module.imports) {
        if (import.parameters.size() > format::maxParameters) {
            return std::unexpected(tooLarge("parameters").error());
        }
    }
    if (!fitsU32(totalInstructions)) {
        return std::unexpected(tooLarge("instructions").error());
    }

    ByteWriter out;
    // Header; size and CRC are patched at the end.
    for (const std::uint8_t byte : format::magic) {
        out.u8(byte);
    }
    out.u16(bytecodeFormatMajor);
    out.u16(bytecodeFormatMinor);
    out.u16(module.languageMajor);
    out.u16(module.languageMinor);
    out.u32(module.compilerVersion);
    out.u32(0); // flags
    out.u32(0); // file size
    out.u32(format::sectionCount);
    out.u32(0); // CRC-32

    const std::size_t tableOffset = out.size();
    for (std::uint32_t i = 0; i < format::sectionCount; ++i) {
        out.u32(i + 1);
        out.u32(0);
        out.u32(0);
    }
    std::uint32_t sectionIndex = 0;
    std::size_t sectionStart = 0;
    const auto beginSection = [&] { sectionStart = out.size(); };
    const auto endSection = [&] {
        const std::size_t entry = tableOffset + sectionIndex * format::sectionEntrySize;
        out.patchU32(entry + 4, static_cast<std::uint32_t>(sectionStart));
        out.patchU32(entry + 8, static_cast<std::uint32_t>(out.size() - sectionStart));
        ++sectionIndex;
    };

    beginSection(); // Strings
    out.u32(static_cast<std::uint32_t>(module.strings.size()));
    for (const std::string& text : module.strings) {
        if (!fitsU32(text.size())) {
            return std::unexpected(tooLarge("string bytes").error());
        }
        out.u32(static_cast<std::uint32_t>(text.size()));
        out.bytes(text);
    }
    endSection();

    beginSection(); // Constants
    out.u32(static_cast<std::uint32_t>(module.constants.size()));
    for (const Constant& constant : module.constants) {
        out.u8(static_cast<std::uint8_t>(constant.type));
        switch (constant.type) {
        case ScriptType::Int: out.u64(static_cast<std::uint64_t>(constant.intValue)); break;
        case ScriptType::Float: {
            std::uint64_t bits = 0;
            std::memcpy(&bits, &constant.floatValue, sizeof bits);
            out.u64(bits);
            break;
        }
        case ScriptType::String: out.u32(constant.stringIndex); break;
        default: return makeError(ErrorCode::InvalidArgument, "Bytecode: constants must be int, float or string");
        }
    }
    endSection();

    beginSection(); // Imports
    out.u32(static_cast<std::uint32_t>(module.imports.size()));
    for (const NativeImport& import : module.imports) {
        out.u32(import.nameIndex);
        out.u8(static_cast<std::uint8_t>(import.returnType));
        writeTypes(out, import.parameters);
    }
    endSection();

    beginSection(); // Functions
    out.u32(static_cast<std::uint32_t>(module.functions.size()));
    std::uint32_t codeOffset = 0;
    for (const Function& function : module.functions) {
        out.u32(function.nameIndex);
        out.u8(static_cast<std::uint8_t>(function.returnType));
        writeTypes(out, function.parameters);
        out.u16(function.registerCount);
        out.u32(codeOffset);
        out.u32(static_cast<std::uint32_t>(function.code.size()));
        out.u32(codeOffset); // Debug entries parallel the code.
        codeOffset += static_cast<std::uint32_t>(function.code.size());
    }
    endSection();

    beginSection(); // Code
    out.u32(static_cast<std::uint32_t>(totalInstructions));
    for (const Function& function : module.functions) {
        for (const Instruction& instruction : function.code) {
            out.u8(static_cast<std::uint8_t>(instruction.opcode));
            out.u8(0);
            out.u16(instruction.a);
            out.u16(instruction.b);
            out.u16(instruction.c);
        }
    }
    endSection();

    beginSection(); // Debug
    out.u32(module.sourceNameIndex);
    out.u32(static_cast<std::uint32_t>(totalInstructions));
    for (const Function& function : module.functions) {
        for (const SourcePosition& position : function.positions) {
            out.u32(position.line);
            out.u32(position.column);
        }
    }
    endSection();

    if (out.size() > format::maxFileSize) {
        return makeError(ErrorCode::InvalidArgument, "Bytecode: module exceeds the maximum file size");
    }
    out.patchU32(20, static_cast<std::uint32_t>(out.size()));
    const std::span<const std::byte> payload(out.data().data() + format::headerSize, out.size() - format::headerSize);
    out.patchU32(28, crc32(payload));
    return std::move(out.data());
}

} // namespace schuimm::script
