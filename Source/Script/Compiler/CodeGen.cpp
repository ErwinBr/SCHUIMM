#include "Script/Compiler/CodeGen.h"

#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/Semantics.h"

#include <cstring>
#include <map>
#include <unordered_map>

namespace schuimm::script {

namespace {

constexpr std::uint32_t maxRegisters = 0xFFFF; // noRegister (0xFFFF) is reserved.
constexpr std::uint32_t maxConstants = 0x10000;

Opcode opcodeFor(IrOp op)
{
    switch (op) {
    case IrOp::Move: return Opcode::Move;
    case IrOp::NegInt: return Opcode::NegInt;
    case IrOp::NegFloat: return Opcode::NegFloat;
    case IrOp::Not: return Opcode::Not;
    case IrOp::AddInt: return Opcode::AddInt;
    case IrOp::SubInt: return Opcode::SubInt;
    case IrOp::MulInt: return Opcode::MulInt;
    case IrOp::DivInt: return Opcode::DivInt;
    case IrOp::ModInt: return Opcode::ModInt;
    case IrOp::AddIntSat: return Opcode::AddIntSat;
    case IrOp::AddFloat: return Opcode::AddFloat;
    case IrOp::SubFloat: return Opcode::SubFloat;
    case IrOp::MulFloat: return Opcode::MulFloat;
    case IrOp::DivFloat: return Opcode::DivFloat;
    case IrOp::FloorDivFloat: return Opcode::FloorDivFloat;
    case IrOp::ModFloat: return Opcode::ModFloat;
    case IrOp::Concat: return Opcode::Concat;
    case IrOp::EqInt: return Opcode::EqInt;
    case IrOp::NeInt: return Opcode::NeInt;
    case IrOp::LtInt: return Opcode::LtInt;
    case IrOp::LeInt: return Opcode::LeInt;
    case IrOp::GtInt: return Opcode::GtInt;
    case IrOp::GeInt: return Opcode::GeInt;
    case IrOp::EqFloat: return Opcode::EqFloat;
    case IrOp::NeFloat: return Opcode::NeFloat;
    case IrOp::LtFloat: return Opcode::LtFloat;
    case IrOp::LeFloat: return Opcode::LeFloat;
    case IrOp::GtFloat: return Opcode::GtFloat;
    case IrOp::GeFloat: return Opcode::GeFloat;
    case IrOp::EqBool: return Opcode::EqBool;
    case IrOp::NeBool: return Opcode::NeBool;
    case IrOp::EqString: return Opcode::EqString;
    case IrOp::NeString: return Opcode::NeString;
    case IrOp::LtString: return Opcode::LtString;
    case IrOp::LeString: return Opcode::LeString;
    case IrOp::GtString: return Opcode::GtString;
    case IrOp::GeString: return Opcode::GeString;
    case IrOp::IntToFloat: return Opcode::IntToFloat;
    case IrOp::FloatToInt: return Opcode::FloatToInt;
    case IrOp::BoolToInt: return Opcode::BoolToInt;
    case IrOp::IntToString: return Opcode::IntToString;
    case IrOp::FloatToString: return Opcode::FloatToString;
    case IrOp::BoolToString: return Opcode::BoolToString;
    case IrOp::CheckRangeStep: return Opcode::CheckRangeStep;
    case IrOp::EqObject: return Opcode::EqObject;
    case IrOp::NeObject: return Opcode::NeObject;
    default: return Opcode::Nop;
    }
}

class Generator {
public:
    Generator(const IrModule& ir, Diagnostics& diagnostics)
        : ir_(ir)
        , diagnostics_(diagnostics)
    {
    }

    Module run()
    {
        module_.sourceNameIndex = intern(ir_.sourceName);
        for (const IrFunction& function : ir_.functions) {
            Function out;
            out.nameIndex = intern(function.name);
            for (const Type type : function.parameters) {
                out.parameters.push_back(toScriptType(type));
            }
            out.returnType = toScriptType(function.returnType);
            module_.functions.push_back(std::move(out));
        }
        for (std::size_t i = 0; i < ir_.functions.size(); ++i) {
            generate(ir_.functions[i], module_.functions[i]);
        }
        for (const IrObject& object : ir_.objects) {
            ObjectEntry out;
            out.nameIndex = intern(object.name);
            for (const IrHandler& handler : object.handlers) {
                out.handlers.push_back({intern(handler.name), handler.function});
            }
            module_.objects.push_back(std::move(out));
        }
        if (module_.constants.size() > maxConstants) {
            fail(concat("The script uses ", module_.constants.size(), " distinct constants; the limit is ", maxConstants, "."));
        }
        return std::move(module_);
    }

private:
    void fail(std::string message)
    {
        SourceRange range;
        range.begin = {0, 1, 1};
        diagnostics_.error(DiagnosticPhase::CodeGen, range, std::move(message));
    }

    std::uint32_t intern(const std::string& text)
    {
        if (const auto found = strings_.find(text); found != strings_.end()) {
            return found->second;
        }
        const auto index = static_cast<std::uint32_t>(module_.strings.size());
        module_.strings.push_back(text);
        strings_.emplace(text, index);
        return index;
    }

    std::uint16_t constant(Constant value)
    {
        // Key: type tag plus the exact bits / string index.
        std::uint64_t bits = 0;
        if (value.type == ScriptType::Int) bits = static_cast<std::uint64_t>(value.intValue);
        if (value.type == ScriptType::Float) std::memcpy(&bits, &value.floatValue, sizeof bits);
        if (value.type == ScriptType::String) bits = value.stringIndex;
        const auto key = std::make_pair(static_cast<int>(value.type), bits);
        if (const auto found = constants_.find(key); found != constants_.end()) {
            return found->second;
        }
        const auto index = static_cast<std::uint16_t>(module_.constants.size() & 0xFFFF);
        module_.constants.push_back(value);
        constants_.emplace(key, index);
        return index;
    }

    std::uint16_t nativeImport(std::uint32_t nativeIndex)
    {
        if (const auto found = imports_.find(nativeIndex); found != imports_.end()) {
            return found->second;
        }
        const IrNative& native = ir_.natives[nativeIndex];
        NativeImport import;
        import.nameIndex = intern(native.name);
        for (const Type type : native.parameters) {
            import.parameters.push_back(toScriptType(type));
        }
        import.returnType = toScriptType(native.returnType);
        const auto index = static_cast<std::uint16_t>(module_.imports.size());
        module_.imports.push_back(std::move(import));
        imports_.emplace(nativeIndex, index);
        return index;
    }

    void generate(const IrFunction& function, Function& out)
    {
        // Dense register numbering in order of first appearance; parameters keep their slots.
        std::vector<std::uint32_t> map(function.registerCount(), noVReg);
        std::uint32_t next = 0;
        for (std::size_t p = 0; p < function.parameters.size(); ++p) {
            map[p] = next++;
        }
        const auto use = [&](VReg value) {
            if (value != noVReg && map[value] == noVReg) map[value] = next++;
        };
        std::size_t window = 0;
        for (const IrBlock& block : function.blocks) {
            for (const IrInstruction& instruction : block.instructions) {
                for (const VReg operand : instruction.operands) use(operand);
                use(instruction.dest);
            }
            use(block.terminator.value);
        }
        // Calls whose arguments are not consecutive registers copy them into a scratch window.
        const auto consecutive = [&](const std::vector<VReg>& arguments) {
            for (std::size_t i = 1; i < arguments.size(); ++i) {
                if (map[arguments[i]] != map[arguments[0]] + i) return false;
            }
            return true;
        };
        for (const IrBlock& block : function.blocks) {
            for (const IrInstruction& instruction : block.instructions) {
                if ((instruction.op == IrOp::Call || instruction.op == IrOp::CallNative) && !consecutive(instruction.operands)) {
                    window = std::max(window, instruction.operands.size());
                }
            }
        }
        const std::size_t total = next + window;
        if (total > maxRegisters) {
            fail(concat("Function '", displayName(function, ir_), "' needs ", total, " registers; the limit is ", maxRegisters, "."));
            return;
        }
        out.registerCount = static_cast<std::uint16_t>(total);
        const auto r = [&](VReg value) {
            return value == noVReg ? noRegister : static_cast<std::uint16_t>(map[value]);
        };
        const auto windowBase = static_cast<std::uint16_t>(next);

        std::vector<std::uint32_t> blockStart(function.blocks.size(), 0);
        std::vector<std::pair<std::size_t, BlockId>> patches; // instruction index -> target block
        const auto emit = [&](Opcode opcode, std::uint16_t a, std::uint16_t b, std::uint16_t c, SourcePosition position) {
            out.code.push_back({opcode, a, b, c});
            out.positions.push_back(position);
        };
        const auto emitJump = [&](Opcode opcode, std::uint16_t a, BlockId target, SourcePosition position) {
            patches.emplace_back(out.code.size(), target);
            emit(opcode, a, 0, 0, position);
        };

        for (std::size_t b = 0; b < function.blocks.size(); ++b) {
            blockStart[b] = static_cast<std::uint32_t>(out.code.size());
            const IrBlock& block = function.blocks[b];
            for (const IrInstruction& instruction : block.instructions) {
                const SourcePosition position = instruction.position;
                switch (instruction.op) {
                case IrOp::LoadInt:
                    emit(Opcode::LoadConst, r(instruction.dest),
                         constant({ScriptType::Int, instruction.intValue, 0.0, 0}), 0, position);
                    break;
                case IrOp::LoadFloat:
                    emit(Opcode::LoadConst, r(instruction.dest),
                         constant({ScriptType::Float, 0, instruction.floatValue, 0}), 0, position);
                    break;
                case IrOp::LoadString:
                    emit(Opcode::LoadConst, r(instruction.dest),
                         constant({ScriptType::String, 0, 0.0, intern(instruction.stringValue)}), 0, position);
                    break;
                case IrOp::LoadBool:
                    emit(instruction.boolValue ? Opcode::LoadTrue : Opcode::LoadFalse, r(instruction.dest), 0, 0, position);
                    break;
                case IrOp::LoadNone: emit(Opcode::LoadNone, r(instruction.dest), 0, 0, position); break;
                case IrOp::Call:
                case IrOp::CallNative: {
                    const std::vector<VReg>& arguments = instruction.operands;
                    std::uint16_t first = 0;
                    if (!arguments.empty()) {
                        if (consecutive(arguments)) {
                            first = r(arguments[0]);
                        } else {
                            first = windowBase;
                            for (std::size_t i = 0; i < arguments.size(); ++i) {
                                emit(Opcode::Move, static_cast<std::uint16_t>(windowBase + i), r(arguments[i]), 0, position);
                            }
                        }
                    }
                    const bool native = instruction.op == IrOp::CallNative;
                    const std::uint16_t callee =
                        native ? nativeImport(instruction.callee) : static_cast<std::uint16_t>(instruction.callee);
                    emit(native ? Opcode::CallNative : Opcode::Call, r(instruction.dest), callee, first, position);
                    break;
                }
                case IrOp::CheckRangeStep: emit(Opcode::CheckRangeStep, r(instruction.operands[0]), 0, 0, position); break;
                default: {
                    const Opcode opcode = opcodeFor(instruction.op);
                    const std::uint16_t a = r(instruction.dest);
                    const std::uint16_t b1 = instruction.operands.size() > 0 ? r(instruction.operands[0]) : 0;
                    const std::uint16_t c1 = instruction.operands.size() > 1 ? r(instruction.operands[1]) : 0;
                    emit(opcode, a, b1, c1, position);
                    break;
                }
                }
            }
            const Terminator& t = block.terminator;
            const BlockId following = static_cast<BlockId>(b + 1);
            const bool isLast = b + 1 == function.blocks.size();
            switch (t.kind) {
            case TerminatorKind::Jump:
                if (t.target != following || isLast) emitJump(Opcode::Jump, 0, t.target, t.position);
                break;
            case TerminatorKind::Branch:
                if (t.falseTarget == following && !isLast) {
                    emitJump(Opcode::JumpIfTrue, r(t.value), t.target, t.position);
                } else if (t.target == following && !isLast) {
                    emitJump(Opcode::JumpIfFalse, r(t.value), t.falseTarget, t.position);
                } else {
                    emitJump(Opcode::JumpIfTrue, r(t.value), t.target, t.position);
                    emitJump(Opcode::Jump, 0, t.falseTarget, t.position);
                }
                break;
            case TerminatorKind::Return: emit(Opcode::Return, r(t.value), 0, 0, t.position); break;
            case TerminatorKind::None:
            case TerminatorKind::Unreachable: emit(Opcode::Unreachable, 0, 0, 0, t.position); break;
            }
        }
        for (const auto& [index, target] : patches) {
            out.code[index].setTarget(blockStart[target]);
        }
    }

    const IrModule& ir_;
    Diagnostics& diagnostics_;
    Module module_;
    std::unordered_map<std::string, std::uint32_t> strings_;
    std::map<std::pair<int, std::uint64_t>, std::uint16_t> constants_;
    std::unordered_map<std::uint32_t, std::uint16_t> imports_;
};

} // namespace

Module generateBytecode(const IrModule& module, Diagnostics& diagnostics)
{
    return Generator(module, diagnostics).run();
}

} // namespace schuimm::script
