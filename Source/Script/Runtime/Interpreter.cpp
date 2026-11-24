#include "Script/Runtime/Interpreter.h"

#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/Disassembler.h"
#include "Script/Bytecode/Semantics.h"

namespace schuimm::script {

std::string formatTrace(const TraceEvent& event)
{
    const Module& module = event.program.module();
    const Function& function = module.functions[event.function];
    std::string text = concat(std::string(event.callDepth > 0 ? (event.callDepth - 1) * 2 : 0, ' '),
                              module.string(function.nameIndex), ":", event.ip);
    if (event.ip < function.positions.size() && function.positions[event.ip].line != 0) {
        text += concat(" (line ", function.positions[event.ip].line, ")");
    }
    text += "  " + formatInstruction(module, event.instruction);
    const OpcodeInfo& info = opcodeInfo(event.instruction.opcode);
    const auto show = [&](OperandKind kind, std::uint16_t value) {
        if ((kind == OperandKind::Register || kind == OperandKind::OptRegister) && value < event.registers.size()) {
            text += concat("  r", value, "=", toDisplayString(event.registers[value]));
        }
    };
    show(info.b, event.instruction.b);
    if (info.b != OperandKind::Target) {
        show(info.c, event.instruction.c);
    }
    if (event.instruction.opcode == Opcode::JumpIfTrue || event.instruction.opcode == Opcode::JumpIfFalse) {
        show(info.a, event.instruction.a);
    }
    return text;
}

struct Interpreter {
    static RunResult run(Coroutine& co, std::uint64_t budget, NativeContext& context, const TraceCallback* trace)
    {
        RunResult result;
        const Program& program = co.program();
        const Module& module = program.module();
        std::uint32_t pc = 0;

        const auto fail = [&](ScriptErrorKind kind, std::string message) {
            result.outcome = RunOutcome::Failed;
            result.error = co.makeError(kind, std::move(message), pc);
            return result;
        };
        const auto typeMismatch = [&]() {
            return fail(ScriptErrorKind::InvalidBytecode,
                        concat("Register type mismatch in ", toString(module.functions[co.frames_.back().function].code[pc].opcode)));
        };
        const auto arithmetic = [&](ArithmeticError error) { return fail(ScriptErrorKind::Runtime, describe(error)); };

        while (true) {
            CallFrame& frame = co.frames_.back();
            const Function& function = module.functions[frame.function];
            if (result.instructions >= budget) {
                result.outcome = RunOutcome::BudgetExhausted;
                result.error = co.makeError(ScriptErrorKind::ExecutionLimit,
                                            concat("Instruction budget of ", budget, " exceeded in one update"), frame.ip);
                return result;
            }
            pc = frame.ip;
            const Instruction& in = function.code[pc]; // pc is in range: validated targets and terminators.
            Value* r = co.registers_.data() + frame.base;
            if (trace != nullptr && *trace) {
                (*trace)(TraceEvent{program, frame.function, pc, in,
                                    std::span<const Value>(r, function.registerCount), co.frames_.size()});
            }
            ++result.instructions;
            frame.ip = pc + 1;

#define SCHUIMM_REQUIRE(reg, T)                                                                     \
    if (!r[reg].is(ScriptType::T)) return typeMismatch()
#define SCHUIMM_INT_OP(op, function_)                                                               \
    case Opcode::op: {                                                                              \
        SCHUIMM_REQUIRE(in.b, Int);                                                                 \
        SCHUIMM_REQUIRE(in.c, Int);                                                                 \
        const IntResult value = function_(r[in.b].asInt(), r[in.c].asInt());                        \
        if (value.error != ArithmeticError::None) return arithmetic(value.error);                   \
        r[in.a] = Value::fromInt(value.value);                                                      \
        break;                                                                                      \
    }
#define SCHUIMM_FLOAT_OP(op, expression)                                                            \
    case Opcode::op: {                                                                              \
        SCHUIMM_REQUIRE(in.b, Float);                                                               \
        SCHUIMM_REQUIRE(in.c, Float);                                                               \
        const double x = r[in.b].asFloat();                                                         \
        const double y = r[in.c].asFloat();                                                         \
        r[in.a] = Value::fromFloat(expression);                                                     \
        break;                                                                                      \
    }
#define SCHUIMM_FLOAT_CHECKED_OP(op, function_)                                                     \
    case Opcode::op: {                                                                              \
        SCHUIMM_REQUIRE(in.b, Float);                                                               \
        SCHUIMM_REQUIRE(in.c, Float);                                                               \
        const FloatResult value = function_(r[in.b].asFloat(), r[in.c].asFloat());                  \
        if (value.error != ArithmeticError::None) return arithmetic(value.error);                   \
        r[in.a] = Value::fromFloat(value.value);                                                    \
        break;                                                                                      \
    }
#define SCHUIMM_COMPARE(op, T, accessor, expression)                                                \
    case Opcode::op: {                                                                              \
        SCHUIMM_REQUIRE(in.b, T);                                                                   \
        SCHUIMM_REQUIRE(in.c, T);                                                                   \
        const auto x = r[in.b].accessor();                                                          \
        const auto y = r[in.c].accessor();                                                          \
        r[in.a] = Value::fromBool(expression);                                                      \
        break;                                                                                      \
    }

            switch (in.opcode) {
            case Opcode::Nop: break;
            case Opcode::LoadConst: r[in.a] = program.constant(in.b); break;
            case Opcode::LoadTrue: r[in.a] = Value::fromBool(true); break;
            case Opcode::LoadFalse: r[in.a] = Value::fromBool(false); break;
            case Opcode::LoadNone: r[in.a] = Value::none(); break;
            case Opcode::Move: r[in.a] = r[in.b]; break;
            case Opcode::NegInt: {
                SCHUIMM_REQUIRE(in.b, Int);
                const IntResult value = negInt(r[in.b].asInt());
                if (value.error != ArithmeticError::None) return arithmetic(value.error);
                r[in.a] = Value::fromInt(value.value);
                break;
            }
            case Opcode::NegFloat:
                SCHUIMM_REQUIRE(in.b, Float);
                r[in.a] = Value::fromFloat(-r[in.b].asFloat());
                break;
            case Opcode::Not:
                SCHUIMM_REQUIRE(in.b, Bool);
                r[in.a] = Value::fromBool(!r[in.b].asBool());
                break;
            SCHUIMM_INT_OP(AddInt, addInt)
            SCHUIMM_INT_OP(SubInt, subInt)
            SCHUIMM_INT_OP(MulInt, mulInt)
            SCHUIMM_INT_OP(DivInt, floorDivInt)
            SCHUIMM_INT_OP(ModInt, floorModInt)
            case Opcode::AddIntSat:
                SCHUIMM_REQUIRE(in.b, Int);
                SCHUIMM_REQUIRE(in.c, Int);
                r[in.a] = Value::fromInt(addIntSaturating(r[in.b].asInt(), r[in.c].asInt()));
                break;
            SCHUIMM_FLOAT_OP(AddFloat, x + y)
            SCHUIMM_FLOAT_OP(SubFloat, x - y)
            SCHUIMM_FLOAT_OP(MulFloat, x * y)
            SCHUIMM_FLOAT_CHECKED_OP(DivFloat, divFloat)
            SCHUIMM_FLOAT_CHECKED_OP(FloorDivFloat, floorDivFloat)
            SCHUIMM_FLOAT_CHECKED_OP(ModFloat, floorModFloat)
            case Opcode::Concat: {
                SCHUIMM_REQUIRE(in.b, String);
                SCHUIMM_REQUIRE(in.c, String);
                const std::string_view x = r[in.b].asString();
                const std::string_view y = r[in.c].asString();
                if (x.size() + y.size() > co.limits_.maxStringLength) {
                    return fail(ScriptErrorKind::Runtime,
                                concat("String too long (limit ", co.limits_.maxStringLength, " bytes)"));
                }
                std::string text;
                text.reserve(x.size() + y.size());
                text.append(x).append(y);
                r[in.a] = Value::fromString(std::move(text));
                break;
            }
            SCHUIMM_COMPARE(EqInt, Int, asInt, x == y)
            SCHUIMM_COMPARE(NeInt, Int, asInt, x != y)
            SCHUIMM_COMPARE(LtInt, Int, asInt, x < y)
            SCHUIMM_COMPARE(LeInt, Int, asInt, x <= y)
            SCHUIMM_COMPARE(GtInt, Int, asInt, x > y)
            SCHUIMM_COMPARE(GeInt, Int, asInt, x >= y)
            SCHUIMM_COMPARE(EqFloat, Float, asFloat, x == y)
            SCHUIMM_COMPARE(NeFloat, Float, asFloat, x != y)
            SCHUIMM_COMPARE(LtFloat, Float, asFloat, x < y)
            SCHUIMM_COMPARE(LeFloat, Float, asFloat, x <= y)
            SCHUIMM_COMPARE(GtFloat, Float, asFloat, x > y)
            SCHUIMM_COMPARE(GeFloat, Float, asFloat, x >= y)
            SCHUIMM_COMPARE(EqBool, Bool, asBool, x == y)
            SCHUIMM_COMPARE(NeBool, Bool, asBool, x != y)
            SCHUIMM_COMPARE(EqString, String, asString, x == y)
            SCHUIMM_COMPARE(NeString, String, asString, x != y)
            SCHUIMM_COMPARE(LtString, String, asString, compareStrings(x, y) < 0)
            SCHUIMM_COMPARE(LeString, String, asString, compareStrings(x, y) <= 0)
            SCHUIMM_COMPARE(GtString, String, asString, compareStrings(x, y) > 0)
            SCHUIMM_COMPARE(GeString, String, asString, compareStrings(x, y) >= 0)
            case Opcode::IntToFloat:
                SCHUIMM_REQUIRE(in.b, Int);
                r[in.a] = Value::fromFloat(static_cast<double>(r[in.b].asInt()));
                break;
            case Opcode::FloatToInt: {
                SCHUIMM_REQUIRE(in.b, Float);
                const IntResult value = floatToInt(r[in.b].asFloat());
                if (value.error != ArithmeticError::None) {
                    return fail(ScriptErrorKind::Runtime,
                                concat("Cannot convert float ", formatFloat(r[in.b].asFloat()), " to int"));
                }
                r[in.a] = Value::fromInt(value.value);
                break;
            }
            case Opcode::BoolToInt:
                SCHUIMM_REQUIRE(in.b, Bool);
                r[in.a] = Value::fromInt(r[in.b].asBool() ? 1 : 0);
                break;
            case Opcode::IntToString:
                SCHUIMM_REQUIRE(in.b, Int);
                r[in.a] = Value::fromString(formatInt(r[in.b].asInt()));
                break;
            case Opcode::FloatToString:
                SCHUIMM_REQUIRE(in.b, Float);
                r[in.a] = Value::fromString(formatFloat(r[in.b].asFloat()));
                break;
            case Opcode::BoolToString:
                SCHUIMM_REQUIRE(in.b, Bool);
                r[in.a] = Value::fromString(formatBool(r[in.b].asBool()));
                break;
            case Opcode::CheckRangeStep:
                SCHUIMM_REQUIRE(in.a, Int);
                if (r[in.a].asInt() == 0) {
                    return fail(ScriptErrorKind::Runtime, "range() step must not be zero");
                }
                break;
            case Opcode::Jump: frame.ip = in.target(); break;
            case Opcode::JumpIfTrue:
                SCHUIMM_REQUIRE(in.a, Bool);
                if (r[in.a].asBool()) frame.ip = in.target();
                break;
            case Opcode::JumpIfFalse:
                SCHUIMM_REQUIRE(in.a, Bool);
                if (!r[in.a].asBool()) frame.ip = in.target();
                break;
            case Opcode::Call: {
                const Function& callee = module.functions[in.b];
                if (co.frames_.size() >= co.limits_.maxCallDepth) {
                    return fail(ScriptErrorKind::Runtime,
                                concat("Call depth limit of ", co.limits_.maxCallDepth, " exceeded (unbounded recursion?)"));
                }
                for (std::size_t i = 0; i < callee.parameters.size(); ++i) {
                    if (!r[in.c + i].matches(callee.parameters[i])) return typeMismatch();
                }
                const std::size_t newBase = static_cast<std::size_t>(frame.base) + function.registerCount;
                const std::size_t top = newBase + callee.registerCount;
                if (top > co.limits_.maxRegisters) {
                    return fail(ScriptErrorKind::Runtime, "Script stack overflow (register limit exceeded)");
                }
                const std::uint32_t returnRegister = in.a == noRegister ? noReturnRegister : frame.base + in.a;
                const std::size_t argumentBase = static_cast<std::size_t>(frame.base) + in.c;
                const auto calleeIndex = static_cast<std::uint32_t>(in.b);
                co.registers_.resize(top); // Invalidates `r` and may move values; indices stay valid.
                for (std::size_t i = 0; i < callee.parameters.size(); ++i) {
                    co.registers_[newBase + i] = co.registers_[argumentBase + i];
                }
                co.frames_.push_back({calleeIndex, 0, static_cast<std::uint32_t>(newBase), returnRegister}); // Invalidates `frame`.
                break;
            }
            case Opcode::CallNative: {
                const NativeImport& import = module.imports[in.b];
                const NativeFunction& native = program.native(in.b);
                const std::span<const Value> arguments(r + in.c, import.parameters.size());
                for (std::size_t i = 0; i < arguments.size(); ++i) {
                    if (!arguments[i].matches(import.parameters[i])) return typeMismatch();
                }
                NativeResult outcome = native.implementation(context, arguments);
                switch (outcome.kind) {
                case NativeResult::Kind::Return:
                    if (import.returnType != ScriptType::None) {
                        if (!outcome.value.matches(import.returnType)) {
                            return fail(ScriptErrorKind::Native,
                                        concat("Native function '", native.name, "' returned ", toString(outcome.value.type()),
                                               " instead of ", toString(import.returnType)));
                        }
                        r[in.a] = std::move(outcome.value);
                    }
                    break;
                case NativeResult::Kind::Wait:
                    if (outcome.waitNanoseconds < 0) {
                        return fail(ScriptErrorKind::Native, concat("Native function '", native.name, "' requested a negative wait"));
                    }
                    result.outcome = RunOutcome::Waiting;
                    result.waitNanoseconds = outcome.waitNanoseconds;
                    return result;
                case NativeResult::Kind::Yield: result.outcome = RunOutcome::Yielded; return result;
                case NativeResult::Kind::Error:
                    return fail(ScriptErrorKind::Native, concat(native.name, "(): ", outcome.message));
                }
                break;
            }
            case Opcode::Return: {
                Value value;
                if (in.a != noRegister) {
                    value = std::move(r[in.a]);
                    if (!value.matches(function.returnType)) return typeMismatch();
                }
                const std::uint32_t returnRegister = frame.returnRegister;
                const std::uint32_t base = frame.base;
                co.frames_.pop_back(); // Invalidates `frame`.
                co.registers_.resize(base);
                if (co.frames_.empty()) {
                    co.result_ = std::move(value);
                    result.outcome = RunOutcome::Finished;
                    return result;
                }
                if (returnRegister != noReturnRegister) {
                    co.registers_[returnRegister] = std::move(value);
                }
                break;
            }
            case Opcode::Unreachable:
                return fail(ScriptErrorKind::Runtime, "Reached the end of a function that must return a value");
            }
#undef SCHUIMM_REQUIRE
#undef SCHUIMM_INT_OP
#undef SCHUIMM_FLOAT_OP
#undef SCHUIMM_FLOAT_CHECKED_OP
#undef SCHUIMM_COMPARE
        }
    }
};

RunResult runCoroutine(Coroutine& coroutine, std::uint64_t budget, NativeContext& context, const TraceCallback* trace)
{
    if (!coroutine.hasFrames()) {
        RunResult result;
        result.outcome = RunOutcome::Finished;
        return result;
    }
    return Interpreter::run(coroutine, budget, context, trace);
}

} // namespace schuimm::script
