#include "Script/Compiler/Optimizer.h"

#include "Script/Bytecode/Semantics.h"

#include <algorithm>
#include <optional>

namespace schuimm::script {

namespace {

// --- Constant lattice ---------------------------------------------------------------------------

enum class State : std::uint8_t { Undefined, Constant, Varying };

struct Lattice {
    State state = State::Undefined;
    Type type = Type::Error;
    std::int64_t i = 0;
    double f = 0.0;
    bool b = false;
    std::string s;

    static Lattice varying() { return {State::Varying, Type::Error, 0, 0.0, false, {}}; }
    static Lattice ofInt(std::int64_t value) { return {State::Constant, Type::Int, value, 0.0, false, {}}; }
    static Lattice ofFloat(double value) { return {State::Constant, Type::Float, 0, value, false, {}}; }
    static Lattice ofBool(bool value) { return {State::Constant, Type::Bool, 0, 0.0, value, {}}; }
    static Lattice ofString(std::string value) { return {State::Constant, Type::String, 0, 0.0, false, std::move(value)}; }
};

bool sameConstant(const Lattice& a, const Lattice& b)
{
    if (a.type != b.type) return false;
    switch (a.type) {
    case Type::Int: return a.i == b.i;
    case Type::Float: return sameFloatBits(a.f, b.f);
    case Type::Bool: return a.b == b.b;
    case Type::String: return a.s == b.s;
    default: return false;
    }
}

// Meets `incoming` into `target`; true if `target` changed.
bool meetInto(Lattice& target, const Lattice& incoming)
{
    if (incoming.state == State::Undefined || target.state == State::Varying) return false;
    if (target.state == State::Undefined) {
        target = incoming;
        return true;
    }
    if (incoming.state == State::Varying || !sameConstant(target, incoming)) {
        target = Lattice::varying();
        return true;
    }
    return false;
}

// Compile-time evaluation with the VM's semantics. nullopt: not foldable (e.g. would trap).
std::optional<Lattice> fold(IrOp op, const std::vector<const Lattice*>& v)
{
    const auto intResult = [](IntResult r) -> std::optional<Lattice> {
        if (r.error != ArithmeticError::None) return std::nullopt;
        return Lattice::ofInt(r.value);
    };
    const auto floatResult = [](FloatResult r) -> std::optional<Lattice> {
        if (r.error != ArithmeticError::None) return std::nullopt;
        return Lattice::ofFloat(r.value);
    };
    const auto a = [&]() -> const Lattice& { return *v[0]; };
    const auto b = [&]() -> const Lattice& { return *v[1]; };
    switch (op) {
    case IrOp::Move: return a();
    case IrOp::NegInt: return intResult(negInt(a().i));
    case IrOp::NegFloat: return Lattice::ofFloat(-a().f);
    case IrOp::Not: return Lattice::ofBool(!a().b);
    case IrOp::AddInt: return intResult(addInt(a().i, b().i));
    case IrOp::SubInt: return intResult(subInt(a().i, b().i));
    case IrOp::MulInt: return intResult(mulInt(a().i, b().i));
    case IrOp::DivInt: return intResult(floorDivInt(a().i, b().i));
    case IrOp::ModInt: return intResult(floorModInt(a().i, b().i));
    case IrOp::AddIntSat: return Lattice::ofInt(addIntSaturating(a().i, b().i));
    case IrOp::AddFloat: return Lattice::ofFloat(a().f + b().f);
    case IrOp::SubFloat: return Lattice::ofFloat(a().f - b().f);
    case IrOp::MulFloat: return Lattice::ofFloat(a().f * b().f);
    case IrOp::DivFloat: return floatResult(divFloat(a().f, b().f));
    case IrOp::FloorDivFloat: return floatResult(floorDivFloat(a().f, b().f));
    case IrOp::ModFloat: return floatResult(floorModFloat(a().f, b().f));
    case IrOp::Concat: return Lattice::ofString(a().s + b().s);
    case IrOp::EqInt: return Lattice::ofBool(a().i == b().i);
    case IrOp::NeInt: return Lattice::ofBool(a().i != b().i);
    case IrOp::LtInt: return Lattice::ofBool(a().i < b().i);
    case IrOp::LeInt: return Lattice::ofBool(a().i <= b().i);
    case IrOp::GtInt: return Lattice::ofBool(a().i > b().i);
    case IrOp::GeInt: return Lattice::ofBool(a().i >= b().i);
    case IrOp::EqFloat: return Lattice::ofBool(a().f == b().f);
    case IrOp::NeFloat: return Lattice::ofBool(a().f != b().f);
    case IrOp::LtFloat: return Lattice::ofBool(a().f < b().f);
    case IrOp::LeFloat: return Lattice::ofBool(a().f <= b().f);
    case IrOp::GtFloat: return Lattice::ofBool(a().f > b().f);
    case IrOp::GeFloat: return Lattice::ofBool(a().f >= b().f);
    case IrOp::EqBool: return Lattice::ofBool(a().b == b().b);
    case IrOp::NeBool: return Lattice::ofBool(a().b != b().b);
    case IrOp::EqString: return Lattice::ofBool(a().s == b().s);
    case IrOp::NeString: return Lattice::ofBool(a().s != b().s);
    case IrOp::LtString: return Lattice::ofBool(compareStrings(a().s, b().s) < 0);
    case IrOp::LeString: return Lattice::ofBool(compareStrings(a().s, b().s) <= 0);
    case IrOp::GtString: return Lattice::ofBool(compareStrings(a().s, b().s) > 0);
    case IrOp::GeString: return Lattice::ofBool(compareStrings(a().s, b().s) >= 0);
    case IrOp::IntToFloat: return Lattice::ofFloat(static_cast<double>(a().i));
    case IrOp::FloatToInt: return intResult(floatToInt(a().f));
    case IrOp::BoolToInt: return Lattice::ofInt(a().b ? 1 : 0);
    case IrOp::IntToString: return Lattice::ofString(formatInt(a().i));
    case IrOp::FloatToString: return Lattice::ofString(formatFloat(a().f));
    case IrOp::BoolToString: return Lattice::ofString(formatBool(a().b));
    default: return std::nullopt;
    }
}

// The value an instruction assigns to its destination, given the operand states.
Lattice evaluate(const IrInstruction& instruction, const std::vector<Lattice>& state)
{
    switch (instruction.op) {
    case IrOp::LoadInt: return Lattice::ofInt(instruction.intValue);
    case IrOp::LoadFloat: return Lattice::ofFloat(instruction.floatValue);
    case IrOp::LoadBool: return Lattice::ofBool(instruction.boolValue);
    case IrOp::LoadString: return Lattice::ofString(instruction.stringValue);
    case IrOp::LoadNone:
    case IrOp::Call:
    case IrOp::CallNative:
    case IrOp::CheckRangeStep: return Lattice::varying();
    default: break;
    }
    std::vector<const Lattice*> operands;
    bool undefined = false;
    for (const VReg operand : instruction.operands) {
        const Lattice& value = state[operand];
        if (value.state == State::Varying) return Lattice::varying();
        undefined = undefined || value.state == State::Undefined;
        operands.push_back(&value);
    }
    if (undefined) {
        return {}; // Optimistic: not (yet) known to be reachable with a value.
    }
    if (auto folded = fold(instruction.op, operands)) {
        return std::move(*folded);
    }
    return Lattice::varying();
}

void transfer(const IrInstruction& instruction, std::vector<Lattice>& state)
{
    if (instruction.dest != noVReg) {
        state[instruction.dest] = evaluate(instruction, state);
    }
}

IrInstruction loadOf(const Lattice& value, VReg dest, SourcePosition position)
{
    IrInstruction load;
    load.dest = dest;
    load.position = position;
    switch (value.type) {
    case Type::Int: load.op = IrOp::LoadInt; load.intValue = value.i; break;
    case Type::Float: load.op = IrOp::LoadFloat; load.floatValue = value.f; break;
    case Type::Bool: load.op = IrOp::LoadBool; load.boolValue = value.b; break;
    default: load.op = IrOp::LoadString; load.stringValue = value.s; break;
    }
    return load;
}

bool isLoad(IrOp op) noexcept
{
    return op == IrOp::LoadInt || op == IrOp::LoadFloat || op == IrOp::LoadBool || op == IrOp::LoadString ||
           op == IrOp::LoadNone;
}

} // namespace

bool propagateConstants(IrFunction& function)
{
    const std::size_t blockCount = function.blocks.size();
    const std::size_t registerCount = function.registerCount();
    std::vector<std::vector<Lattice>> in(blockCount);
    std::vector<bool> executable(blockCount, false);
    in[0].assign(registerCount, Lattice{});
    for (std::size_t p = 0; p < function.parameters.size(); ++p) {
        in[0][p] = Lattice::varying();
    }
    executable[0] = true;
    std::vector<BlockId> worklist{0};
    while (!worklist.empty()) {
        const BlockId b = worklist.back();
        worklist.pop_back();
        std::vector<Lattice> state = in[b];
        for (const IrInstruction& instruction : function.blocks[b].instructions) {
            transfer(instruction, state);
        }
        const Terminator& terminator = function.blocks[b].terminator;
        std::vector<BlockId> next = successors(terminator);
        if (terminator.kind == TerminatorKind::Branch) {
            const Lattice& condition = state[terminator.value];
            if (condition.state == State::Constant) {
                next = {condition.b ? terminator.target : terminator.falseTarget};
            } else if (condition.state == State::Undefined) {
                next.clear();
            }
        }
        for (const BlockId successor : next) {
            if (!executable[successor]) {
                executable[successor] = true;
                in[successor] = state;
                worklist.push_back(successor);
                continue;
            }
            bool changed = false;
            for (std::size_t r = 0; r < registerCount; ++r) {
                changed = meetInto(in[successor][r], state[r]) || changed;
            }
            if (changed) {
                worklist.push_back(successor);
            }
        }
    }

    bool changed = false;
    for (std::size_t b = 0; b < blockCount; ++b) {
        if (!executable[b]) {
            continue;
        }
        IrBlock& block = function.blocks[b];
        std::vector<Lattice> state = in[b];
        std::vector<IrInstruction> rewritten;
        rewritten.reserve(block.instructions.size());
        for (IrInstruction& instruction : block.instructions) {
            if (instruction.op == IrOp::CheckRangeStep) {
                const Lattice& step = state[instruction.operands[0]];
                if (step.state == State::Constant && step.i != 0) {
                    changed = true;
                    continue; // The check cannot fail.
                }
            }
            const Lattice value = instruction.dest != noVReg ? evaluate(instruction, state) : Lattice::varying();
            if (value.state == State::Constant && !isLoad(instruction.op)) {
                rewritten.push_back(loadOf(value, instruction.dest, instruction.position));
                changed = true;
            } else {
                rewritten.push_back(std::move(instruction));
            }
            transfer(rewritten.back(), state);
        }
        block.instructions = std::move(rewritten);
        Terminator& terminator = block.terminator;
        if (terminator.kind == TerminatorKind::Branch) {
            const Lattice& condition = state[terminator.value];
            if (condition.state == State::Constant) {
                terminator.kind = TerminatorKind::Jump;
                terminator.target = condition.b ? terminator.target : terminator.falseTarget;
                terminator.value = noVReg;
                changed = true;
            }
        }
    }
    return changed;
}

bool removeUnreachableBlocks(IrFunction& function)
{
    const std::size_t count = function.blocks.size();
    std::vector<bool> reachable(count, false);
    std::vector<BlockId> stack{0};
    reachable[0] = true;
    while (!stack.empty()) {
        const BlockId b = stack.back();
        stack.pop_back();
        for (const BlockId s : successors(function.blocks[b].terminator)) {
            if (!reachable[s]) {
                reachable[s] = true;
                stack.push_back(s);
            }
        }
    }
    if (std::all_of(reachable.begin(), reachable.end(), [](bool r) { return r; })) {
        return false;
    }
    std::vector<BlockId> remap(count, 0);
    std::vector<IrBlock> blocks;
    for (std::size_t b = 0; b < count; ++b) {
        if (reachable[b]) {
            remap[b] = static_cast<BlockId>(blocks.size());
            blocks.push_back(std::move(function.blocks[b]));
        }
    }
    for (IrBlock& block : blocks) {
        block.terminator.target = remap[block.terminator.target];
        block.terminator.falseTarget = remap[block.terminator.falseTarget];
    }
    function.blocks = std::move(blocks);
    return true;
}

bool simplifyControlFlow(IrFunction& function)
{
    bool changed = false;
    std::vector<IrBlock>& blocks = function.blocks;
    const std::size_t count = blocks.size();

    // Follow chains of empty blocks that only jump on.
    const auto resolve = [&](BlockId target) {
        for (std::size_t steps = 0; steps < count; ++steps) {
            const IrBlock& block = blocks[target];
            if (!block.instructions.empty() || block.terminator.kind != TerminatorKind::Jump ||
                block.terminator.target == target) {
                break;
            }
            target = block.terminator.target;
        }
        return target;
    };
    for (IrBlock& block : blocks) {
        Terminator& t = block.terminator;
        if (t.kind == TerminatorKind::Jump || t.kind == TerminatorKind::Branch) {
            const BlockId target = resolve(t.target);
            if (target != t.target) {
                t.target = target;
                changed = true;
            }
        }
        if (t.kind == TerminatorKind::Branch) {
            const BlockId falseTarget = resolve(t.falseTarget);
            if (falseTarget != t.falseTarget) {
                t.falseTarget = falseTarget;
                changed = true;
            }
            if (t.target == t.falseTarget) {
                t.kind = TerminatorKind::Jump;
                t.value = noVReg;
                changed = true;
            }
        }
    }

    // Merge a block into its only predecessor when that predecessor ends with a jump to it.
    std::vector<int> predecessors(count, 0);
    for (const IrBlock& block : blocks) {
        for (const BlockId s : successors(block.terminator)) {
            ++predecessors[s];
        }
    }
    for (std::size_t a = 0; a < count; ++a) {
        while (blocks[a].terminator.kind == TerminatorKind::Jump) {
            const BlockId b = blocks[a].terminator.target;
            if (b == a || b == 0 || predecessors[b] != 1) {
                break;
            }
            IrBlock& from = blocks[b];
            blocks[a].instructions.insert(blocks[a].instructions.end(), std::make_move_iterator(from.instructions.begin()),
                                          std::make_move_iterator(from.instructions.end()));
            blocks[a].terminator = from.terminator;
            from.instructions.clear();
            from.terminator = {TerminatorKind::Unreachable, noVReg, 0, 0, {}};
            predecessors[b] = 0;
            changed = true;
        }
    }
    return changed;
}

bool eliminateDeadCode(IrFunction& function, const IrModule& module)
{
    const std::size_t count = function.blocks.size();
    const std::size_t registers = function.registerCount();
    const auto removable = [&](const IrInstruction& instruction) {
        if (instruction.op == IrOp::CallNative) {
            return instruction.callee < module.natives.size() && module.natives[instruction.callee].pure;
        }
        return irOpInfo(instruction.op).removable;
    };
    const auto addTerminatorUses = [](const Terminator& t, std::vector<bool>& live) {
        if ((t.kind == TerminatorKind::Branch || t.kind == TerminatorKind::Return) && t.value != noVReg) {
            live[t.value] = true;
        }
    };
    // Backward liveness to a fixed point.
    std::vector<std::vector<bool>> liveIn(count, std::vector<bool>(registers, false));
    std::vector<std::vector<bool>> liveOut(count, std::vector<bool>(registers, false));
    bool changedLiveness = true;
    while (changedLiveness) {
        changedLiveness = false;
        for (std::size_t b = count; b-- > 0;) {
            std::vector<bool> live(registers, false);
            for (const BlockId s : successors(function.blocks[b].terminator)) {
                for (std::size_t r = 0; r < registers; ++r) {
                    live[r] = live[r] || liveIn[s][r];
                }
            }
            liveOut[b] = live;
            addTerminatorUses(function.blocks[b].terminator, live);
            const auto& instructions = function.blocks[b].instructions;
            for (auto it = instructions.rbegin(); it != instructions.rend(); ++it) {
                if (it->dest != noVReg) live[it->dest] = false;
                for (const VReg operand : it->operands) live[operand] = true;
            }
            if (live != liveIn[b]) {
                liveIn[b] = std::move(live);
                changedLiveness = true;
            }
        }
    }
    bool changed = false;
    for (std::size_t b = 0; b < count; ++b) {
        std::vector<bool> live = liveOut[b];
        addTerminatorUses(function.blocks[b].terminator, live);
        auto& instructions = function.blocks[b].instructions;
        std::vector<IrInstruction> kept;
        for (auto it = instructions.rbegin(); it != instructions.rend(); ++it) {
            const bool unused = it->dest == noVReg || !live[it->dest];
            if (unused && removable(*it)) {
                changed = true;
                continue;
            }
            if (it->dest != noVReg) live[it->dest] = false;
            for (const VReg operand : it->operands) live[operand] = true;
            kept.push_back(std::move(*it));
        }
        std::reverse(kept.begin(), kept.end());
        instructions = std::move(kept);
    }
    return changed;
}

void optimize(IrModule& module, OptimizerSettings settings)
{
    for (IrFunction& function : module.functions) {
        for (int iteration = 0; iteration < settings.maximumIterations; ++iteration) {
            bool changed = propagateConstants(function);
            changed = removeUnreachableBlocks(function) || changed;
            changed = simplifyControlFlow(function) || changed;
            changed = removeUnreachableBlocks(function) || changed;
            changed = eliminateDeadCode(function, module) || changed;
            if (!changed) {
                break;
            }
        }
    }
}

} // namespace schuimm::script
