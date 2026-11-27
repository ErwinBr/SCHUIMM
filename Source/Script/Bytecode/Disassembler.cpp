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

std::string signature(const Module& module, std::uint32_t nameIndex, const std::vector<TypeRef>& parameters,
                      TypeRef returnType)
{
    std::string text = module.strings[nameIndex] + "(";
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        text += (i == 0 ? "" : ", ");
        text += formatType(module, parameters[i]);
    }
    text += ") -> ";
    text += formatType(module, returnType);
    return text;
}

} // namespace

std::string formatType(const Module& module, TypeRef type)
{
    if (type.kind == ScriptType::Enum && type.enumIndex < module.enums.size()) {
        return module.strings[module.enums[type.enumIndex].nameIndex];
    }
    return toString(type.kind);
}

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

std::string formatInitialValue(const Module& module, const VariableEntry& variable)
{
    const InitialValue& initial = variable.initial;
    switch (variable.type.kind) {
    case ScriptType::Bool: return formatBool(initial.boolValue);
    case ScriptType::Int: return formatInt(initial.intValue);
    case ScriptType::Float: return formatFloat(initial.x);
    case ScriptType::String: return initial.stringIndex < module.strings.size() ? quoteString(module.strings[initial.stringIndex]) : "?";
    case ScriptType::Vec2: return concat("vec2(", formatFloat(initial.x), ", ", formatFloat(initial.y), ")");
    case ScriptType::Enum:
        if (variable.type.enumIndex < module.enums.size() && initial.ordinal < module.enums[variable.type.enumIndex].members.size()) {
            const EnumEntry& entry = module.enums[variable.type.enumIndex];
            return concat(module.strings[entry.nameIndex], ".", module.strings[entry.members[initial.ordinal]]);
        }
        return concat("#", initial.ordinal);
    default: return "None";
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
        case OperandKind::Enum: operand(concat("e", value, " (", module.strings[module.enums[value].nameIndex], ")")); break;
        case OperandKind::Global:
            operand(value < module.globals.size() ? concat("g", value, " (", module.strings[module.globals[value].nameIndex], ")")
                                                  : concat("g", value));
            break;
        case OperandKind::Instance: operand(concat("i", value)); break;
        case OperandKind::Immediate:
            if (instruction.opcode == Opcode::LoadEnum && instruction.b < module.enums.size() &&
                value < module.enums[instruction.b].members.size()) {
                operand(concat("#", value, " (", module.strings[module.enums[instruction.b].members[value]], ")"));
            } else {
                operand(concat("#", value));
            }
            break;
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
    std::string out = concat("; SCHUIMM bytecode ", bytecodeFormatMajor, ".", module.formatMinor, ", language ",
                             module.languageMajor, ".", module.languageMinor);
    if (module.sources.size() == 1) {
        out += concat(", source ", quoteString(module.sourceName()), "\n");
    } else {
        out += "\nsources:\n";
        for (std::size_t i = 0; i < module.sources.size(); ++i) {
            out += concat("  s", i, " ", toString(module.sources[i].role), " ", quoteString(module.sourcePath(static_cast<std::uint32_t>(i))), "\n");
        }
    }
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
    if (!module.enums.empty()) {
        out += "enums:\n";
        for (std::size_t e = 0; e < module.enums.size(); ++e) {
            const EnumEntry& entry = module.enums[e];
            out += concat("  e", e, " ", entry.isEngine() ? module.strings[entry.qualifierIndex] + "." : std::string(),
                          module.strings[entry.nameIndex], ":");
            for (const std::uint32_t member : entry.members) {
                out += " " + module.strings[member];
            }
            out += "\n";
        }
    }
    // Handlers are listed as "object.handler" ("room.handler", "actor.handler"); the parameter 0
    // of object and actor handlers is self.
    std::vector<std::string> prefix(module.functions.size());
    if (!module.behaviours.empty()) {
        out += "behaviours:\n";
        for (const BehaviourEntry& behaviour : module.behaviours) {
            const std::string name = behaviour.kind == BehaviourKind::Object ? module.strings[behaviour.nameIndex]
                                                                              : std::string(toString(behaviour.kind));
            out += concat("  ", behaviour.kind == BehaviourKind::Object ? "object " : "", name);
            if (module.sources.size() > 1) out += concat(" (s", behaviour.source, ")");
            out += ":";
            for (const HandlerEntry& handler : behaviour.handlers) {
                out += concat(" ", module.strings[handler.nameIndex], " -> f", handler.function);
                prefix[handler.function] = name + ".";
            }
            out += "\n";
            for (std::size_t v = 0; v < behaviour.variables.size(); ++v) {
                const VariableEntry& variable = behaviour.variables[v];
                out += concat("    i", v, " ", module.strings[variable.nameIndex], ": ", formatType(module, variable.type),
                              " = ", formatInitialValue(module, variable), "\n");
            }
        }
    }
    if (!module.globals.empty()) {
        out += "globals:\n";
        for (std::size_t g = 0; g < module.globals.size(); ++g) {
            const VariableEntry& variable = module.globals[g];
            out += concat("  g", g, " ", module.strings[variable.nameIndex], ": ", formatType(module, variable.type), " = ",
                          formatInitialValue(module, variable), "\n");
        }
    }
    for (std::size_t f = 0; f < module.functions.size(); ++f) {
        const Function& function = module.functions[f];
        out += concat("\nfunction f", f, " ", prefix[f],
                      signature(module, function.nameIndex, function.parameters, function.returnType), "  ; ",
                      function.registerCount(), " registers");
        if (module.sources.size() > 1 && function.source != noSource) out += concat(", s", function.source);
        out += "\n";
        if (function.registerCount() > 0) {
            std::string types = "  ; register types:";
            for (std::size_t r = 0; r < function.registerTypes.size(); ++r) {
                types += concat(" r", r, ":", formatType(module, function.registerTypes[r]));
            }
            out += types + "\n";
        }
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
