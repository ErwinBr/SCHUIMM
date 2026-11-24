#include "Script/Bytecode/Disassembler.h"

#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/Semantics.h"

#include <cstdio>

namespace schuimm::script {

namespace {

std::string reg(std::uint16_t value)
{
    return value == noRegister ? std::string("-") : concat("r", value);
}

std::string signature(const Module& module, std::uint32_t nameIndex, const std::vector<ScriptType>& parameters,
                      ScriptType returnType)
{
    std::string text = module.strings[nameIndex] + "(";
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        text += (i == 0 ? "" : ", ");
        text += toString(parameters[i]);
    }
    text += ") -> ";
    text += toString(returnType);
    return text;
}

} // namespace

std::string quoteString(const std::string& text)
{
    std::string out = "\"";
    for (const char ch : text) {
        switch (ch) {
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\0': out += "\\0"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20 || ch == 0x7F) {
                char buffer[8];
                std::snprintf(buffer, sizeof buffer, "\\x%02X", static_cast<unsigned>(static_cast<unsigned char>(ch)));
                out += buffer;
            } else {
                out += ch;
            }
        }
    }
    out += '"';
    return out;
}

std::string formatConstant(const Module& module, const Constant& constant)
{
    switch (constant.type) {
    case ScriptType::Int: return formatInt(constant.intValue);
    case ScriptType::Float: return formatFloat(constant.floatValue);
    case ScriptType::String: return quoteString(module.strings[constant.stringIndex]);
    default: return "?";
    }
}

std::string formatInstruction(const Module& module, const Instruction& instruction)
{
    const OpcodeInfo& info = opcodeInfo(instruction.opcode);
    std::string text = info.name;
    bool first = true;
    const auto operand = [&](const std::string& value) {
        text += first ? " " : ", ";
        text += value;
        first = false;
    };
    const auto describe = [&](OperandKind kind, std::uint16_t value) {
        switch (kind) {
        case OperandKind::Unused: break;
        case OperandKind::Register:
        case OperandKind::OptRegister: operand(reg(value)); break;
        case OperandKind::Constant: operand(concat("k", value, " (", formatConstant(module, module.constants[value]), ")")); break;
        case OperandKind::Function: operand(concat("f", value, " (", module.strings[module.functions[value].nameIndex], ")")); break;
        case OperandKind::Native: operand(concat("n", value, " (", module.strings[module.imports[value].nameIndex], ")")); break;
        case OperandKind::ArgWindow: operand(concat("args r", value)); break;
        case OperandKind::Target: operand(concat("@", instruction.target())); break;
        }
    };
    describe(info.a, instruction.a);
    describe(info.b, instruction.b);
    if (info.b != OperandKind::Target) {
        describe(info.c, instruction.c);
    }
    return text;
}

std::string disassemble(const Module& module)
{
    std::string out = concat("; SCHUIMM bytecode ", bytecodeFormatMajor, ".", bytecodeFormatMinor, ", language ",
                             module.languageMajor, ".", module.languageMinor, ", source ",
                             quoteString(module.sourceName()), "\n");
    if (!module.constants.empty()) {
        out += "constants:\n";
        for (std::size_t i = 0; i < module.constants.size(); ++i) {
            out += concat("  k", i, " ", toString(module.constants[i].type), " ",
                          formatConstant(module, module.constants[i]), "\n");
        }
    }
    if (!module.imports.empty()) {
        out += "natives:\n";
        for (std::size_t i = 0; i < module.imports.size(); ++i) {
            const NativeImport& import = module.imports[i];
            out += concat("  n", i, " ", signature(module, import.nameIndex, import.parameters, import.returnType), "\n");
        }
    }
    for (std::size_t f = 0; f < module.functions.size(); ++f) {
        const Function& function = module.functions[f];
        out += concat("\nfunction f", f, " ", signature(module, function.nameIndex, function.parameters, function.returnType),
                      "  ; ", function.registerCount, " registers\n");
        std::uint32_t lastLine = 0;
        for (std::size_t ip = 0; ip < function.code.size(); ++ip) {
            std::string line = concat("  ", ip < 10 ? "   " : (ip < 100 ? "  " : (ip < 1000 ? " " : "")), ip, "  ",
                                      formatInstruction(module, function.code[ip]));
            const SourcePosition& position = function.positions[ip];
            if (position.line != 0 && position.line != lastLine) {
                while (line.size() < 48) {
                    line += ' ';
                }
                line += concat(" ; line ", position.line);
                lastLine = position.line;
            }
            out += line + "\n";
        }
    }
    return out;
}

} // namespace schuimm::script
