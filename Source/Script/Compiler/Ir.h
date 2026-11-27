#pragma once

// Intermediate representation: per function a list of basic blocks of three-address
// instructions on virtual registers, with explicit control flow. Independent of the bytecode
// encoding (CodeGen translates it). There is no SSA form: a variable keeps one virtual register
// that is reassigned, temporaries get fresh ones.
//
//   bb0:
//     r2 = LoadInt 10
//     r3 = AddInt r0, r2        ; line 3
//     Branch r4, bb1, bb2

#include "Script/Bytecode/Module.h"
#include "Script/Compiler/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace schuimm::script {

using VReg = std::uint32_t;
using BlockId = std::uint32_t;
inline constexpr VReg noVReg = 0xFFFFFFFFu;

// X(name, operand count (-1: variable), removable when unused, may trap)
//   LoadEnum: enum `callee` (an index into IrModule::enums), member ordinal `intValue`.
//   SetVec2X/Y: dest = operand 0 with component x/y replaced by operand 1 (a float).
//   MulVec2/DivVec2: vector operand 0, float operand 1.
//   Spawn: starts function `callee` with the operands as arguments; dest (optional) receives the
//   coroutine.
//   removable: no side effect and cannot fail, so an unused result may be deleted.
//   may trap:  can raise a runtime error, which is observable; never removed or folded into one.
#define SCHUIMM_IR_OPS(X)                                                                           \
    X(LoadInt, 0, true, false)                                                                      \
    X(LoadFloat, 0, true, false)                                                                    \
    X(LoadBool, 0, true, false)                                                                     \
    X(LoadString, 0, true, false)                                                                   \
    X(LoadNone, 0, true, false)                                                                     \
    X(Move, 1, true, false)                                                                         \
    X(NegInt, 1, false, true)                                                                       \
    X(NegFloat, 1, true, false)                                                                     \
    X(Not, 1, true, false)                                                                          \
    X(AddInt, 2, false, true)                                                                       \
    X(SubInt, 2, false, true)                                                                       \
    X(MulInt, 2, false, true)                                                                       \
    X(DivInt, 2, false, true)                                                                       \
    X(ModInt, 2, false, true)                                                                       \
    X(AddIntSat, 2, true, false)                                                                    \
    X(AddFloat, 2, true, false)                                                                     \
    X(SubFloat, 2, true, false)                                                                     \
    X(MulFloat, 2, true, false)                                                                     \
    X(DivFloat, 2, false, true)                                                                     \
    X(FloorDivFloat, 2, false, true)                                                                \
    X(ModFloat, 2, false, true)                                                                     \
    X(Concat, 2, true, false)                                                                       \
    X(EqInt, 2, true, false)                                                                        \
    X(NeInt, 2, true, false)                                                                        \
    X(LtInt, 2, true, false)                                                                        \
    X(LeInt, 2, true, false)                                                                        \
    X(GtInt, 2, true, false)                                                                        \
    X(GeInt, 2, true, false)                                                                        \
    X(EqFloat, 2, true, false)                                                                      \
    X(NeFloat, 2, true, false)                                                                      \
    X(LtFloat, 2, true, false)                                                                      \
    X(LeFloat, 2, true, false)                                                                      \
    X(GtFloat, 2, true, false)                                                                      \
    X(GeFloat, 2, true, false)                                                                      \
    X(EqBool, 2, true, false)                                                                       \
    X(NeBool, 2, true, false)                                                                       \
    X(EqString, 2, true, false)                                                                     \
    X(NeString, 2, true, false)                                                                     \
    X(LtString, 2, true, false)                                                                     \
    X(LeString, 2, true, false)                                                                     \
    X(GtString, 2, true, false)                                                                     \
    X(GeString, 2, true, false)                                                                     \
    X(IntToFloat, 1, true, false)                                                                   \
    X(FloatToInt, 1, false, true)                                                                   \
    X(BoolToInt, 1, true, false)                                                                    \
    X(IntToString, 1, true, false)                                                                  \
    X(FloatToString, 1, true, false)                                                                \
    X(BoolToString, 1, true, false)                                                                 \
    X(CheckRangeStep, 1, false, true)                                                               \
    X(EqRef, 2, true, false)                                                                        \
    X(NeRef, 2, true, false)                                                                        \
    X(LoadEnum, 0, true, false)                                                                     \
    X(EqEnum, 2, true, false)                                                                       \
    X(NeEnum, 2, true, false)                                                                       \
    X(MakeVec2, 2, true, false)                                                                     \
    X(Vec2X, 1, true, false)                                                                        \
    X(Vec2Y, 1, true, false)                                                                        \
    X(SetVec2X, 2, true, false)                                                                     \
    X(SetVec2Y, 2, true, false)                                                                     \
    X(AddVec2, 2, true, false)                                                                      \
    X(SubVec2, 2, true, false)                                                                      \
    X(MulVec2, 2, true, false)                                                                      \
    X(DivVec2, 2, false, true)                                                                      \
    X(NegVec2, 1, true, false)                                                                      \
    X(EqVec2, 2, true, false)                                                                       \
    X(NeVec2, 2, true, false)                                                                       \
    X(Call, -1, false, true)                                                                        \
    X(CallNative, -1, false, true)                                                                  \
    X(Spawn, -1, false, true)

enum class IrOp : std::uint8_t {
#define SCHUIMM_X(name, operands, removable, traps) name,
    SCHUIMM_IR_OPS(SCHUIMM_X)
#undef SCHUIMM_X
};

struct IrOpInfo {
    const char* name;
    int operandCount;
    bool removable;
    bool mayTrap;
};

[[nodiscard]] const IrOpInfo& irOpInfo(IrOp op) noexcept;

struct IrInstruction {
    IrOp op = IrOp::LoadNone;
    VReg dest = noVReg; // noVReg: no result (CheckRangeStep, calls without a result).
    std::vector<VReg> operands;
    // Load* immediates.
    std::int64_t intValue = 0;
    double floatValue = 0.0;
    bool boolValue = false;
    std::string stringValue;
    std::uint32_t callee = 0; // Call: function index; CallNative: index into IrModule::natives; LoadEnum: enum.
    SourcePosition position;
};

enum class TerminatorKind : std::uint8_t {
    None,        // Only while building.
    Jump,        // -> target
    Branch,      // value ? target : falseTarget
    Return,      // value, or noVReg in functions without a result
    Unreachable, // The end of a function with a result that the type checker proved unreachable.
};

struct Terminator {
    TerminatorKind kind = TerminatorKind::None;
    VReg value = noVReg;
    BlockId target = 0;
    BlockId falseTarget = 0;
    SourcePosition position;
};

struct IrBlock {
    std::vector<IrInstruction> instructions;
    Terminator terminator;
};

inline constexpr std::uint32_t noBehaviour = 0xFFFFFFFFu;

// A function. Block 0 is the entry; a block's id is its index. Parameters are r0..rn-1; for an
// object handler r0 is the hidden "self" parameter (type object), followed by the declared
// parameters.
struct IrFunction {
    std::string name;
    std::uint32_t source = noSource;       // Index into IrModule::sources; noSource if generated.
    std::uint32_t behaviour = noBehaviour; // Handlers: index into IrModule::behaviours.
    std::vector<Type> parameters;
    Type returnType = Type::None;
    std::vector<Type> registerTypes; // Static type of each virtual register.
    std::vector<IrBlock> blocks;

    [[nodiscard]] std::uint32_t registerCount() const noexcept { return static_cast<std::uint32_t>(registerTypes.size()); }
};

struct IrNative {
    std::string name;
    std::vector<Type> parameters;
    Type returnType = Type::None;
    bool pure = false;
};

// A behaviour declaration: handler (event) names mapped to the functions implementing them.
struct IrHandler {
    std::string name;
    std::uint32_t function = 0; // Index into IrModule::functions.
};

struct IrBehaviour {
    BehaviourKind kind = BehaviourKind::Object;
    std::uint32_t source = 0;
    std::string name; // The object id; empty for room and actor behaviours.
    std::vector<IrHandler> handlers;
};

struct IrSource {
    std::string name;
    ScriptRole role = ScriptRole::Room;
};

// An enum type (Type enumType(i) is enums[i]). Engine enums have a qualifier (the interface).
struct IrEnum {
    std::string name;
    std::string qualifier;
    std::vector<std::string> members;
};

struct IrModule {
    std::vector<IrSource> sources;
    std::vector<IrFunction> functions;
    std::vector<IrNative> natives;
    std::vector<IrBehaviour> behaviours;
    std::vector<IrEnum> enums;
};

// The source spelling of a type, with enum names.
[[nodiscard]] std::string typeName(Type type, const IrModule& module);

// "blender.look_at" for a handler, the plain name otherwise (dumps and messages only).
[[nodiscard]] std::string displayName(const IrFunction& function, const IrModule& module);

// Successor blocks of a terminator (0, 1 or 2 entries).
[[nodiscard]] std::vector<BlockId> successors(const Terminator& terminator);

// Text dumps for debugging and tests.
[[nodiscard]] std::string dumpIr(const IrModule& module);
[[nodiscard]] std::string dumpIr(const IrFunction& function, const IrModule& module);
[[nodiscard]] std::string formatIrInstruction(const IrInstruction& instruction, const IrModule& module);

// Structural check (terminators, targets, register ranges, operand counts). Returns a
// description of the first problem, or an empty string.
[[nodiscard]] std::string verifyIr(const IrFunction& function);

} // namespace schuimm::script
