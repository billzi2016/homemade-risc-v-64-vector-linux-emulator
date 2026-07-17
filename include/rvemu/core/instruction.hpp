// 文件职责：表达一次精确取指得到的指令包，以及 32 位指令的通用编码字段。
// 边界：本文件不判断具体操作是否合法，也不执行寄存器或内存副作用。

#pragma once

#include <cstdint>

namespace rvemu::core {

struct InstructionPacket final {
    std::uint64_t program_counter{0U};
    std::uint32_t bits{0U};
    std::uint8_t length{0U};

    [[nodiscard]] bool compressed() const noexcept { return length == 2U; }
};

struct DecodedInstruction final {
    std::uint32_t bits{0U};
    std::uint8_t opcode{0U};
    std::uint8_t destination{0U};
    std::uint8_t function3{0U};
    std::uint8_t source1{0U};
    std::uint8_t source2{0U};
    std::uint8_t function7{0U};
};

}  // namespace rvemu::core
