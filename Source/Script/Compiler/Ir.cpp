#include "Script/Compiler/Ir.h"

#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/Disassembler.h"
#include "Script/Bytecode/Semantics.h"

#include <array>

namespace schuimm::script {

namespace {
constexpr std::array irOpTable{
#define SCHUIMM_X(name, operands, removable, traps) IrOpInfo{#name, operands, removable, traps},
    SCHUIMM_IR_OPS(SCHUIMM_X)
#undef SCHUIMM_X
};

std::string reg(VReg value) { return value == noVReg ? std::string("-") : concat("r", value); }
} // namespace

const IrOpInfo& irOpInfo(IrOp op) noexcept { return irOpTable[static_cast<std::size_t>(op)]; }

std::vector<BlockId> successors(const Terminator& terminator)
{
    switch (terminator.kind) {
    case TerminatorKind::Jump: return {terminator.target};
    case TerminatorKind::Branch: return {terminator.target, terminator.falseTarget};
    default: return {};
    }
}

std::string typeName(Type type, const IrModule& module)
{
    if (isEnum(type) && enumIndexOf(type) < module.enums.size()) {
        return module.enums[enumIndexOf(type)].name;
    }
    return toString(type);
}

std::string displayName(const IrFunction& function, const IrModule& module)
{
    if (function.object < module.objects.size()) {
        return concat(module.objects[function.object].name, ".", function.name);
    }
    return function.name;
}

std::string formatIrInstruction(const IrInstruction& instruction, const IrModule& module)
{
    std::string text;
    if (instruction.dest != noVReg) {
        text = reg(instruction.dest) + " = ";
    }
    text += irOpInfo(instruction.op).name;
    switch (instruction.op) {
    case IrOp::LoadInt: text += " " + formatInt(instruction.intValue); break;
    case IrOp::LoadFloat: text += " " + formatFloat(instruction.floatValue); break;
    case IrOp::LoadBool: text += concat(" ", formatBool(instruction.boolValue)); break;
    case IrOp::LoadString: text += " " + quoteString(instruction.stringValue); break;
    case IrOp::LoadEnum: {
        const IrEnum* declaration = instruction.callee < module.enums.size() ? &module.enums[instruction.callee] : nullptr;
        if (declaration != nullptr && static_cast<std::size_t>(instruction.intValue) < declaration->members.size()) {
            text += concat(" ", declaration->name, ".", declaration->members[static_cast<std::size_t>(instruction.intValue)]);
        } else {
            text += concat(" enum", instruction.callee, ".", instruction.intValue);
        }
        break;
    }
    case IrOp::Call:
        text += " " + (instruction.callee < module.functions.size() ? module.functions[instruction.callee].name
                                                                     : concat("f", instruction.callee));
        break;
    case IrOp::CallNative:
        text += " " + (instruction.callee < module.natives.size() ? module.natives[instruction.callee].name
                                                                   : concat("n", instruction.callee));
        break;
    default: break;
    }
    for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
        text += (i == 0 ? " " : ", ") + reg(instruction.operands[i]);
    }
    return text;
}

std::string dumpIr(const IrFunction& function, const IrModule& module)
{
    std::string out = concat("func ", displayName(function, module), "(");
    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
        out += concat(i == 0 ? "" : ", ", "r", i, ": ", typeName(function.parameters[i], module));
    }
    out += concat(") -> ", typeName(function.returnType, module), "\n");
    for (std::size_t b = 0; b < function.blocks.size(); ++b) {
        const IrBlock& block = function.blocks[b];
        out += concat("bb", b, ":\n");
        for (const IrInstruction& instruction : block.instructions) {
            out += "  " + formatIrInstruction(instruction, module) + "\n";
        }
        const Terminator& t = block.terminator;
        switch (t.kind) {
        case TerminatorKind::None: out += "  <no terminator>\n"; break;
        case TerminatorKind::Jump: out += concat("  Jump bb", t.target, "\n"); break;
        case TerminatorKind::Branch: out += concat("  Branch ", reg(t.value), ", bb", t.target, ", bb", t.falseTarget, "\n"); break;
        case TerminatorKind::Return: out += t.value == noVReg ? "  Return\n" : concat("  Return ", reg(t.value), "\n"); break;
        case TerminatorKind::Unreachable: out += "  Unreachable\n"; break;
        }
    }
    return out;
}

std::string dumpIr(const IrModule& module)
{
    std::string out;
    for (const IrNative& native : module.natives) {
        out += concat("native ", native.name, "(");
        for (std::size_t i = 0; i < native.parameters.size(); ++i) {
            out += concat(i == 0 ? "" : ", ", typeName(native.parameters[i], module));
        }
        out += concat(") -> ", typeName(native.returnType, module), "\n");
    }
    for (const IrEnum& declaration : module.enums) {
        out += concat("enum ", declaration.qualifier.empty() ? "" : declaration.qualifier + ".", declaration.name, ":");
        for (const std::string& member : declaration.members) {
            out += " " + member;
        }
        out += "\n";
    }
    for (const IrObject& object : module.objects) {
        out += concat("object ", object.name, ":");
        for (const IrHandler& handler : object.handlers) {
            out += concat(" ", handler.name, "=f", handler.function);
        }
        out += "\n";
    }
    for (const IrFunction& function : module.functions) {
        out += "\n" + dumpIr(function, module);
    }
    return out;
}

std::string verifyIr(const IrFunction& function)
{
    if (function.blocks.empty()) {
        return "function has no blocks";
    }
    const auto validRegister = [&](VReg value) { return value < function.registerCount(); };
    for (std::size_t b = 0; b < function.blocks.size(); ++b) {
        const IrBlock& block = function.blocks[b];
        for (const IrInstruction& instruction : block.instructions) {
            const IrOpInfo& info = irOpInfo(instruction.op);
            if (info.operandCount >= 0 && instruction.operands.size() != static_cast<std::size_t>(info.operandCount)) {
                return concat("bb", b, ": ", info.name, " has ", instruction.operands.size(), " operands");
            }
            for (const VReg operand : instruction.operands) {
                if (!validRegister(operand)) return concat("bb", b, ": operand ", operand, " out of range");
            }
            if (instruction.dest != noVReg && !validRegister(instruction.dest)) {
                return concat("bb", b, ": destination out of range");
            }
        }
        const Terminator& t = block.terminator;
        if (t.kind == TerminatorKind::None) return concat("bb", b, " has no terminator");
        for (const BlockId target : successors(t)) {
            if (target >= function.blocks.size()) return concat("bb", b, " jumps to missing bb", target);
        }
        if (t.kind == TerminatorKind::Branch && !validRegister(t.value)) return concat("bb", b, ": bad branch condition");
        if (t.kind == TerminatorKind::Return && t.value != noVReg && !validRegister(t.value)) {
            return concat("bb", b, ": bad return value");
        }
    }
    return {};
}

} // namespace schuimm::script
