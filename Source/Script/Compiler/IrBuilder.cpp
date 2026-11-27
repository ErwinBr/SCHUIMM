#include "Script/Compiler/IrBuilder.h"

#include "Core/Common/Assert.h"

#include <utility>

namespace schuimm::script {

namespace {

SourcePosition positionOf(const SourceRange& range) noexcept { return {range.begin.line, range.begin.column}; }

class FunctionBuilder {
public:
    FunctionBuilder(const SemanticModel& semantics, const FunctionSemantics& function, IrFunction& out)
        : semantics_(semantics)
        , function_(function)
        , out_(out)
    {
    }

    void build()
    {
        const FunctionDecl& declaration = *function_.declaration;
        out_.name = declaration.name;
        out_.source = function_.source;
        out_.behaviour = function_.isHandler() ? function_.behaviour : noBehaviour;
        out_.returnType = function_.returnType;
        for (const Type type : function_.parameters) {
            out_.parameters.push_back(type);
        }
        // Variable i lives in virtual register i (parameters first).
        for (const VariableInfo& variable : function_.variables) {
            out_.registerTypes.push_back(variable.type);
        }
        current_ = newBlock();
        block(*declaration.body);
        if (!terminated()) {
            const SourcePosition end = positionOf(declaration.body->range);
            terminate({function_.returnType == Type::None ? TerminatorKind::Return : TerminatorKind::Unreachable,
                       noVReg, 0, 0, end});
        }
    }

private:
    BlockId newBlock()
    {
        out_.blocks.emplace_back();
        return static_cast<BlockId>(out_.blocks.size() - 1);
    }

    [[nodiscard]] bool terminated() const { return out_.blocks[current_].terminator.kind != TerminatorKind::None; }

    void terminate(Terminator terminator)
    {
        SCHUIMM_ASSERT(!terminated(), "block terminated twice");
        out_.blocks[current_].terminator = terminator;
    }

    void jump(BlockId target, SourcePosition position) { terminate({TerminatorKind::Jump, noVReg, target, 0, position}); }

    void branch(VReg condition, BlockId whenTrue, BlockId whenFalse, SourcePosition position)
    {
        terminate({TerminatorKind::Branch, condition, whenTrue, whenFalse, position});
    }

    // Code after a return (or other terminator) is placed in a fresh, unreachable block.
    void ensureOpenBlock()
    {
        if (terminated()) {
            current_ = newBlock();
        }
    }

    VReg temporary(Type type)
    {
        out_.registerTypes.push_back(type);
        return static_cast<VReg>(out_.registerTypes.size() - 1);
    }

    IrInstruction& emit(IrOp op, VReg dest, std::vector<VReg> operands, SourcePosition position)
    {
        ensureOpenBlock();
        IrInstruction instruction;
        instruction.op = op;
        instruction.dest = dest;
        instruction.operands = std::move(operands);
        instruction.position = position;
        out_.blocks[current_].instructions.push_back(std::move(instruction));
        return out_.blocks[current_].instructions.back();
    }

    VReg loadInt(std::int64_t value, SourcePosition position)
    {
        const VReg dest = temporary(Type::Int);
        emit(IrOp::LoadInt, dest, {}, position).intValue = value;
        return dest;
    }

    // A fresh read of a global or instance variable: these may change between any two reads (a
    // call, a native, another coroutine while this one waits), so a read is never reused.
    VReg loadStorage(VariableStorage storage, std::uint32_t index, Type type, SourcePosition position)
    {
        const VReg dest = temporary(type);
        emit(storage == VariableStorage::Global ? IrOp::LoadGlobal : IrOp::LoadInstance, dest, {}, position).callee = index;
        return dest;
    }

    // The default value of `type` (defaultValue).
    VReg loadDefault(Type type, SourcePosition position)
    {
        const VReg dest = temporary(type);
        if (isEnum(type)) {
            IrInstruction& load = emit(IrOp::LoadEnum, dest, {}, position);
            load.callee = enumIndexOf(type);
            load.intValue = 0;
            return dest;
        }
        switch (type) {
        case Type::Int: emit(IrOp::LoadInt, dest, {}, position).intValue = 0; break;
        case Type::Float: emit(IrOp::LoadFloat, dest, {}, position).floatValue = 0.0; break;
        case Type::Bool: emit(IrOp::LoadBool, dest, {}, position).boolValue = false; break;
        case Type::String: emit(IrOp::LoadString, dest, {}, position); break;
        case Type::Vec2: {
            const VReg zero = temporary(Type::Float);
            emit(IrOp::LoadFloat, zero, {}, position).floatValue = 0.0;
            emit(IrOp::MakeVec2, dest, {zero, zero}, position);
            break;
        }
        default: emit(IrOp::LoadNull, dest, {}, position); break; // The nullable types.
        }
        return dest;
    }

    // --- Statements ---------------------------------------------------------------------------
    void block(const Block& b)
    {
        for (const auto& statement : b.statements) {
            ensureOpenBlock();
            this->statement(*statement);
        }
    }

    void statement(const Stmt& s)
    {
        const SourcePosition position = positionOf(s.range);
        switch (s.kind) {
        case StmtKind::Pass: return;
        case StmtKind::Expression: expression(*s.value, true); return;
        case StmtKind::Assign: {
            const StatementInfo& info = semantics_.statement(s);
            if (!s.value) {
                // "var name: type": the default value.
                const VReg value = loadDefault(function_.variables[info.variable].type, position);
                emit(IrOp::Move, info.variable, {value}, position);
                return;
            }
            const VReg value = expression(*s.value);
            if (info.storage == VariableStorage::Local) {
                if (info.component) {
                    emit(info.componentIndex == 0 ? IrOp::SetVec2X : IrOp::SetVec2Y, info.variable, {info.variable, value}, position);
                } else {
                    emit(IrOp::Move, info.variable, {value}, position);
                }
                return;
            }
            // A global or instance variable: read, change and write back a component; store a value.
            const bool global = info.storage == VariableStorage::Global;
            VReg stored = value;
            if (info.component) {
                stored = loadStorage(info.storage, info.variable, Type::Vec2, position);
                emit(info.componentIndex == 0 ? IrOp::SetVec2X : IrOp::SetVec2Y, stored, {stored, value}, position);
            }
            emit(global ? IrOp::StoreGlobal : IrOp::StoreInstance, noVReg, {stored}, position).callee = info.variable;
            return;
        }
        case StmtKind::If: {
            const BlockId end = newBlock();
            for (const IfBranch& ifBranch : s.branches) {
                const VReg condition = expression(*ifBranch.condition);
                const BlockId then = newBlock();
                const BlockId next = newBlock();
                branch(condition, then, next, positionOf(ifBranch.condition->range));
                current_ = then;
                block(*ifBranch.body);
                if (!terminated()) jump(end, position);
                current_ = next;
            }
            if (s.elseBlock) {
                block(*s.elseBlock);
            }
            if (!terminated()) jump(end, position);
            current_ = end;
            return;
        }
        case StmtKind::While: {
            const BlockId condition = newBlock();
            const BlockId body = newBlock();
            const BlockId exit = newBlock();
            jump(condition, position);
            current_ = condition;
            const VReg value = expression(*s.value);
            branch(value, body, exit, positionOf(s.value->range));
            current_ = body;
            block(*s.body);
            if (!terminated()) jump(condition, position);
            current_ = exit;
            return;
        }
        case StmtKind::For: forLoop(s); return;
        case StmtKind::Return: {
            VReg value = noVReg;
            if (s.value && semantics_.expression(*s.value).type != Type::None) {
                value = expression(*s.value);
            } else if (s.value) {
                expression(*s.value, true); // "return f()" with f returning None.
            }
            ensureOpenBlock();
            terminate({TerminatorKind::Return, value, 0, 0, position});
            return;
        }
        }
    }

    // for i in range(start, end, step): the bounds and the step are evaluated once, left to
    // right. A hidden counter drives the loop, so assigning to i in the body does not change the
    // iteration (as in Python). The counter step saturates, so the last iteration never overflows.
    void forLoop(const Stmt& s)
    {
        const SourcePosition position = positionOf(s.range);
        const auto& arguments = s.rangeArguments;
        VReg start = noVReg;
        VReg end = noVReg;
        VReg step = noVReg;
        if (arguments.size() == 1) {
            start = loadInt(0, position);
            end = expression(*arguments[0]);
        } else {
            start = expression(*arguments[0]);
            end = expression(*arguments[1]);
        }
        // A bound that is a plain variable must not follow later assignments to it.
        const auto snapshot = [&](VReg value) {
            const VReg copy = temporary(Type::Int);
            emit(IrOp::Move, copy, {value}, position);
            return copy;
        };
        end = snapshot(end);
        if (arguments.size() == 3) {
            step = snapshot(expression(*arguments[2]));
            emit(IrOp::CheckRangeStep, noVReg, {step}, positionOf(arguments[2]->range));
        } else {
            step = loadInt(1, position);
        }
        const VReg counter = temporary(Type::Int);
        emit(IrOp::Move, counter, {start}, position);
        const VReg zero = loadInt(0, position);
        const VReg ascending = temporary(Type::Bool);
        emit(IrOp::GtInt, ascending, {step, zero}, position);

        const BlockId test = newBlock();
        const BlockId testUp = newBlock();
        const BlockId testDown = newBlock();
        const BlockId body = newBlock();
        const BlockId advance = newBlock();
        const BlockId exit = newBlock();
        jump(test, position);

        current_ = test;
        branch(ascending, testUp, testDown, position);
        current_ = testUp;
        const VReg below = temporary(Type::Bool);
        emit(IrOp::LtInt, below, {counter, end}, position);
        branch(below, body, exit, position);
        current_ = testDown;
        const VReg above = temporary(Type::Bool);
        emit(IrOp::GtInt, above, {counter, end}, position);
        branch(above, body, exit, position);

        current_ = body;
        emit(IrOp::Move, semantics_.statement(s).variable, {counter}, position);
        block(*s.body);
        if (!terminated()) jump(advance, position);

        current_ = advance;
        emit(IrOp::AddIntSat, counter, {counter, step}, position);
        jump(test, position);
        current_ = exit;
    }

    // --- Expressions --------------------------------------------------------------------------
    // Evaluates an expression into a register (noVReg if it has no value, e.g. a call of a
    // function without a result) and applies the implicit int -> float widening.
    VReg expression(const Expr& e, bool discard = false)
    {
        const ExprInfo& info = semantics_.expression(e);
        VReg value = rawExpression(e, info, discard);
        if (info.widenToFloat && value != noVReg) {
            const VReg widened = temporary(Type::Float);
            emit(IrOp::IntToFloat, widened, {value}, positionOf(e.range));
            value = widened;
        }
        return value;
    }

    [[nodiscard]] Type operandType(const Expr& e) const
    {
        const ExprInfo& info = semantics_.expression(e);
        return info.widenToFloat ? Type::Float : info.type;
    }

    VReg rawExpression(const Expr& e, const ExprInfo& info, bool discard)
    {
        const SourcePosition position = positionOf(e.range);
        switch (e.kind) {
        case ExprKind::Error: return noVReg;
        case ExprKind::IntLiteral: return loadInt(e.intValue, position);
        case ExprKind::FloatLiteral: {
            const VReg dest = temporary(Type::Float);
            emit(IrOp::LoadFloat, dest, {}, position).floatValue = e.floatValue;
            return dest;
        }
        case ExprKind::StringLiteral: {
            const VReg dest = temporary(Type::String);
            emit(IrOp::LoadString, dest, {}, position).stringValue = e.text;
            return dest;
        }
        case ExprKind::BoolLiteral: {
            const VReg dest = temporary(Type::Bool);
            emit(IrOp::LoadBool, dest, {}, position).boolValue = e.boolValue;
            return dest;
        }
        case ExprKind::NoneLiteral: {
            if (!isNullable(info.type)) return noVReg; // "return None" in a function without a result.
            const VReg dest = temporary(info.type);
            emit(IrOp::LoadNull, dest, {}, position);
            return dest;
        }
        // A local: no copy. An expression cannot assign, so a function's own variable keeps its
        // value while the expression is evaluated. Values that must outlive a statement (range
        // bounds) are copied. Global and instance variables are read afresh (loadStorage).
        case ExprKind::Name:
            if (info.storage != VariableStorage::Local) return loadStorage(info.storage, info.variable, info.type, position);
            return info.variable;
        case ExprKind::Self: {
            // The entity of the behaviour instance the coroutine runs for.
            const VReg dest = temporary(info.type);
            emit(IrOp::LoadSelf, dest, {}, position);
            return dest;
        }
        case ExprKind::Member: {
            if (info.isEnumMember) {
                const VReg dest = temporary(info.type);
                IrInstruction& load = emit(IrOp::LoadEnum, dest, {}, position);
                load.callee = enumIndexOf(info.type);
                load.intValue = info.enumMember;
                return dest;
            }
            const VReg vector = expression(*e.left);
            const VReg dest = temporary(Type::Float);
            emit(info.component == 0 ? IrOp::Vec2X : IrOp::Vec2Y, dest, {vector}, position);
            return dest;
        }
        case ExprKind::Unary: {
            const VReg operand = expression(*e.left);
            const VReg dest = temporary(info.type);
            const IrOp op = e.unaryOp == UnaryOp::Not  ? IrOp::Not
                            : info.type == Type::Vec2  ? IrOp::NegVec2
                            : info.type == Type::Float ? IrOp::NegFloat
                                                       : IrOp::NegInt;
            emit(op, dest, {operand}, position);
            return dest;
        }
        case ExprKind::Binary:
            if (e.binaryOp == BinaryOp::And || e.binaryOp == BinaryOp::Or) {
                return shortCircuit(e, position);
            }
            return binary(e, info, position);
        case ExprKind::Call: return call(e, info, discard, position);
        case ExprKind::Spawn: {
            // Arguments are evaluated here, once, in the spawning coroutine.
            std::vector<VReg> arguments;
            for (const auto& argument : e.left->arguments) {
                arguments.push_back(expression(*argument));
            }
            const VReg dest = discard ? noVReg : temporary(Type::Coroutine);
            emit(IrOp::Spawn, dest, std::move(arguments), position).callee = info.callee;
            return dest;
        }
        }
        return noVReg;
    }

    // a and b:  r = a; if r: r = b      a or b:  r = a; if not r: r = b
    VReg shortCircuit(const Expr& e, SourcePosition position)
    {
        const VReg result = temporary(Type::Bool);
        const VReg left = expression(*e.left);
        emit(IrOp::Move, result, {left}, position);
        const BlockId right = newBlock();
        const BlockId end = newBlock();
        if (e.binaryOp == BinaryOp::And) {
            branch(result, right, end, position);
        } else {
            branch(result, end, right, position);
        }
        current_ = right;
        const VReg value = expression(*e.right);
        emit(IrOp::Move, result, {value}, position);
        jump(end, position);
        current_ = end;
        return result;
    }

    static IrOp comparison(BinaryOp op, Type type)
    {
        const int index = static_cast<int>(op) - static_cast<int>(BinaryOp::Equal);
        switch (type) {
        case Type::Int: return static_cast<IrOp>(static_cast<int>(IrOp::EqInt) + index);
        case Type::Float: return static_cast<IrOp>(static_cast<int>(IrOp::EqFloat) + index);
        case Type::String: return static_cast<IrOp>(static_cast<int>(IrOp::EqString) + index);
        case Type::Vec2: return op == BinaryOp::Equal ? IrOp::EqVec2 : IrOp::NeVec2;
        default:
            if (isReference(type)) return op == BinaryOp::Equal ? IrOp::EqRef : IrOp::NeRef;
            if (isEnum(type)) return op == BinaryOp::Equal ? IrOp::EqEnum : IrOp::NeEnum;
            return op == BinaryOp::Equal ? IrOp::EqBool : IrOp::NeBool;
        }
    }

    VReg binary(const Expr& e, const ExprInfo& info, SourcePosition position)
    {
        VReg left = expression(*e.left);
        VReg right = expression(*e.right);
        IrOp op = IrOp::AddInt;
        Type type = info.type;
        if (info.type == Type::Vec2) {
            switch (e.binaryOp) {
            case BinaryOp::Add: op = IrOp::AddVec2; break;
            case BinaryOp::Subtract: op = IrOp::SubVec2; break;
            case BinaryOp::Divide: op = IrOp::DivVec2; break;
            default: op = IrOp::MulVec2; break;
            }
            if (info.swapOperands) {
                std::swap(left, right); // scalar * vector: both are evaluated, left to right, already.
            }
        } else if (isComparison(e.binaryOp)) {
            op = comparison(e.binaryOp, operandType(*e.left));
            type = Type::Bool;
        } else if (info.type == Type::String) {
            op = IrOp::Concat;
        } else {
            const bool isFloat = info.type == Type::Float;
            switch (e.binaryOp) {
            case BinaryOp::Add: op = isFloat ? IrOp::AddFloat : IrOp::AddInt; break;
            case BinaryOp::Subtract: op = isFloat ? IrOp::SubFloat : IrOp::SubInt; break;
            case BinaryOp::Multiply: op = isFloat ? IrOp::MulFloat : IrOp::MulInt; break;
            case BinaryOp::Divide: op = IrOp::DivFloat; break;
            case BinaryOp::FloorDivide: op = isFloat ? IrOp::FloorDivFloat : IrOp::DivInt; break;
            case BinaryOp::Modulo: op = isFloat ? IrOp::ModFloat : IrOp::ModInt; break;
            default: break;
            }
        }
        const VReg dest = temporary(type);
        emit(op, dest, {left, right}, position);
        return dest;
    }

    VReg call(const Expr& e, const ExprInfo& info, bool discard, SourcePosition position)
    {
        std::vector<VReg> arguments;
        for (const auto& argument : e.arguments) {
            arguments.push_back(expression(*argument));
        }
        if (info.callKind == CallKind::Intrinsic && info.intrinsic == Intrinsic::MakeVec2) {
            const VReg dest = temporary(Type::Vec2);
            emit(IrOp::MakeVec2, dest, {arguments.at(0), arguments.at(1)}, position);
            return dest;
        }
        if (info.callKind == CallKind::Intrinsic) {
            const VReg argument = arguments.at(0);
            const Type from = semantics_.expression(*e.arguments[0]).type;
            IrOp op = IrOp::Move;
            switch (info.intrinsic) {
            case Intrinsic::ToInt:
                if (from == Type::Int) return argument;
                op = from == Type::Float ? IrOp::FloatToInt : IrOp::BoolToInt;
                break;
            case Intrinsic::ToFloat:
                if (from == Type::Float) return argument;
                op = IrOp::IntToFloat;
                break;
            case Intrinsic::ToString:
                if (from == Type::String) return argument;
                op = from == Type::Int ? IrOp::IntToString : (from == Type::Float ? IrOp::FloatToString : IrOp::BoolToString);
                break;
            case Intrinsic::MakeVec2: break;
            }
            const VReg dest = temporary(info.type);
            emit(op, dest, {argument}, position);
            return dest;
        }
        (void)discard; // A result is always stored when the callee has one (the bytecode requires it).
        const VReg dest = info.type == Type::None ? noVReg : temporary(info.type);
        IrInstruction& instruction =
            emit(info.callKind == CallKind::Native ? IrOp::CallNative : IrOp::Call, dest, std::move(arguments), position);
        instruction.callee = info.callee;
        return dest;
    }

    const SemanticModel& semantics_;
    const FunctionSemantics& function_;
    IrFunction& out_;
    BlockId current_ = 0;
};

} // namespace

IrModule buildIr(const SemanticModel& semantics)
{
    IrModule module;
    for (const SourceSemantics& source : semantics.sources()) {
        module.sources.push_back({source.name, source.role});
    }
    for (const PersistentVariable& variable : semantics.globals()) {
        module.globals.push_back({variable.name, variable.type, variable.initial});
    }
    for (const NativeSignature& native : semantics.natives()) {
        module.natives.push_back({native.name, native.parameters, native.returnType, native.pure});
    }
    for (const EnumInfo& declaration : semantics.enums()) {
        module.enums.push_back({declaration.name, declaration.qualifier, declaration.members});
    }
    for (const FunctionSemantics& function : semantics.functions()) {
        module.functions.emplace_back();
        FunctionBuilder(semantics, function, module.functions.back()).build();
    }
    // Spawn wrappers: "spawn:walk_to(params) -> result" calls the native and returns its result.
    for (const std::uint32_t nativeIndex : semantics.spawnWrappers()) {
        const NativeSignature& native = semantics.natives()[nativeIndex];
        IrFunction wrapper;
        wrapper.name = "spawn:" + native.name;
        wrapper.parameters = native.parameters;
        wrapper.returnType = native.returnType;
        wrapper.registerTypes = native.parameters;
        std::vector<VReg> arguments;
        for (std::size_t p = 0; p < native.parameters.size(); ++p) {
            arguments.push_back(static_cast<VReg>(p));
        }
        VReg result = noVReg;
        if (native.returnType != Type::None) {
            result = static_cast<VReg>(wrapper.registerTypes.size());
            wrapper.registerTypes.push_back(native.returnType);
        }
        IrBlock block;
        IrInstruction call;
        call.op = IrOp::CallNative;
        call.dest = result;
        call.operands = std::move(arguments);
        call.callee = nativeIndex;
        block.instructions.push_back(std::move(call));
        block.terminator = {TerminatorKind::Return, result, 0, 0, {}};
        wrapper.blocks.push_back(std::move(block));
        module.functions.push_back(std::move(wrapper));
    }
    for (const BehaviourSemantics& behaviour : semantics.behaviours()) {
        IrBehaviour out;
        out.kind = behaviour.kind;
        out.source = behaviour.source;
        out.name = behaviour.name;
        for (const PersistentVariable& variable : behaviour.variables) {
            out.variables.push_back({variable.name, variable.type, variable.initial});
        }
        for (const HandlerSemantics& handler : behaviour.handlers) {
            out.handlers.push_back({handler.name, handler.function});
        }
        module.behaviours.push_back(std::move(out));
    }
    return module;
}

} // namespace schuimm::script
