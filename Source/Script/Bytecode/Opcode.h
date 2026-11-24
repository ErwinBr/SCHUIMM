#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace schuimm::script {

// How an instruction uses each of its three 16-bit operand fields (see Bytecode.md).
enum class OperandKind : std::uint8_t {
    Unused,      // Must be 0.
    Register,    // Register index < the function's register count.
    OptRegister, // A register, or noRegister ("no destination" / "no value").
    Constant,    // Index into the constant pool.
    Function,    // Index into the function table.
    Native,      // Index into the native import table.
    ArgWindow,   // First of N consecutive argument registers; N comes from the callee signature.
    Target,      // Field b and c together: absolute instruction index (b | c << 16) in the function.
};

inline constexpr std::uint16_t noRegister = 0xFFFF;

// X(name, a, b, c, terminator). The numeric opcode is the position in this list. Appending is
// backwards compatible within a format major version; reordering or removing is not.
#define SCHUIMM_SCRIPT_OPCODES(X)                                                                   \
    X(Nop, Unused, Unused, Unused, false)                                                           \
    X(LoadConst, Register, Constant, Unused, false)                                                 \
    X(LoadTrue, Register, Unused, Unused, false)                                                    \
    X(LoadFalse, Register, Unused, Unused, false)                                                   \
    X(LoadNone, Register, Unused, Unused, false)                                                    \
    X(Move, Register, Register, Unused, false)                                                      \
    X(NegInt, Register, Register, Unused, false)                                                    \
    X(NegFloat, Register, Register, Unused, false)                                                  \
    X(Not, Register, Register, Unused, false)                                                       \
    X(AddInt, Register, Register, Register, false)                                                  \
    X(SubInt, Register, Register, Register, false)                                                  \
    X(MulInt, Register, Register, Register, false)                                                  \
    X(DivInt, Register, Register, Register, false)                                                  \
    X(ModInt, Register, Register, Register, false)                                                  \
    X(AddIntSat, Register, Register, Register, false)                                               \
    X(AddFloat, Register, Register, Register, false)                                                \
    X(SubFloat, Register, Register, Register, false)                                                \
    X(MulFloat, Register, Register, Register, false)                                                \
    X(DivFloat, Register, Register, Register, false)                                                \
    X(FloorDivFloat, Register, Register, Register, false)                                           \
    X(ModFloat, Register, Register, Register, false)                                                \
    X(Concat, Register, Register, Register, false)                                                  \
    X(EqInt, Register, Register, Register, false)                                                   \
    X(NeInt, Register, Register, Register, false)                                                   \
    X(LtInt, Register, Register, Register, false)                                                   \
    X(LeInt, Register, Register, Register, false)                                                   \
    X(GtInt, Register, Register, Register, false)                                                   \
    X(GeInt, Register, Register, Register, false)                                                   \
    X(EqFloat, Register, Register, Register, false)                                                 \
    X(NeFloat, Register, Register, Register, false)                                                 \
    X(LtFloat, Register, Register, Register, false)                                                 \
    X(LeFloat, Register, Register, Register, false)                                                 \
    X(GtFloat, Register, Register, Register, false)                                                 \
    X(GeFloat, Register, Register, Register, false)                                                 \
    X(EqBool, Register, Register, Register, false)                                                  \
    X(NeBool, Register, Register, Register, false)                                                  \
    X(EqString, Register, Register, Register, false)                                                \
    X(NeString, Register, Register, Register, false)                                                \
    X(LtString, Register, Register, Register, false)                                                \
    X(LeString, Register, Register, Register, false)                                                \
    X(GtString, Register, Register, Register, false)                                                \
    X(GeString, Register, Register, Register, false)                                                \
    X(IntToFloat, Register, Register, Unused, false)                                                \
    X(FloatToInt, Register, Register, Unused, false)                                                \
    X(BoolToInt, Register, Register, Unused, false)                                                 \
    X(IntToString, Register, Register, Unused, false)                                               \
    X(FloatToString, Register, Register, Unused, false)                                             \
    X(BoolToString, Register, Register, Unused, false)                                              \
    X(CheckRangeStep, Register, Unused, Unused, false)                                              \
    X(Jump, Unused, Target, Unused, true)                                                           \
    X(JumpIfTrue, Register, Target, Unused, false)                                                  \
    X(JumpIfFalse, Register, Target, Unused, false)                                                 \
    X(Call, OptRegister, Function, ArgWindow, false)                                                \
    X(CallNative, OptRegister, Native, ArgWindow, false)                                            \
    X(Return, OptRegister, Unused, Unused, true)                                                    \
    X(Unreachable, Unused, Unused, Unused, true)

enum class Opcode : std::uint8_t {
#define SCHUIMM_X(name, a, b, c, terminator) name,
    SCHUIMM_SCRIPT_OPCODES(SCHUIMM_X)
#undef SCHUIMM_X
};

struct OpcodeInfo {
    const char* name;
    OperandKind a;
    OperandKind b; // Target: b and c form one 32-bit target; c is then not checked separately.
    OperandKind c;
    bool terminator; // Execution never falls through to the next instruction.
};

[[nodiscard]] std::size_t opcodeCount() noexcept;
[[nodiscard]] std::optional<Opcode> opcodeFromByte(std::uint8_t value) noexcept;
[[nodiscard]] const OpcodeInfo& opcodeInfo(Opcode opcode) noexcept;
[[nodiscard]] inline const char* toString(Opcode opcode) noexcept { return opcodeInfo(opcode).name; }

} // namespace schuimm::script
