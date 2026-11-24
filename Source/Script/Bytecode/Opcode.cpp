#include "Script/Bytecode/Opcode.h"

#include <array>

namespace schuimm::script {

namespace {
constexpr std::array opcodeTable{
#define SCHUIMM_X(name, a, b, c, terminator)                                                        \
    OpcodeInfo{#name, OperandKind::a, OperandKind::b, OperandKind::c, terminator},
    SCHUIMM_SCRIPT_OPCODES(SCHUIMM_X)
#undef SCHUIMM_X
};
} // namespace

std::size_t opcodeCount() noexcept { return opcodeTable.size(); }

std::optional<Opcode> opcodeFromByte(std::uint8_t value) noexcept
{
    if (value >= opcodeTable.size()) {
        return std::nullopt;
    }
    return static_cast<Opcode>(value);
}

const OpcodeInfo& opcodeInfo(Opcode opcode) noexcept
{
    return opcodeTable[static_cast<std::size_t>(opcode)];
}

} // namespace schuimm::script
