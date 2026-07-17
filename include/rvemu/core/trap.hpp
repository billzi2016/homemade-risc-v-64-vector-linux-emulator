// 文件职责：定义 CPU 同步异常原因、精确 Trap 信息和 M/S/U 特权级编码。
// 边界：本文件只描述异常结果，不负责写 CSR、执行委托或跳转 Trap 向量。

#pragma once

#include <cstdint>

namespace rvemu::core {

enum class PrivilegeMode : std::uint8_t {
    User = 0U,
    Supervisor = 1U,
    Machine = 3U,
};

enum class ExceptionCause : std::uint64_t {
    InstructionAddressMisaligned = 0U,
    InstructionAccessFault = 1U,
    IllegalInstruction = 2U,
    Breakpoint = 3U,
    LoadAddressMisaligned = 4U,
    LoadAccessFault = 5U,
    StoreAddressMisaligned = 6U,
    StoreAccessFault = 7U,
    EnvironmentCallFromUser = 8U,
    EnvironmentCallFromSupervisor = 9U,
    EnvironmentCallFromMachine = 11U,
    InstructionPageFault = 12U,
    LoadPageFault = 13U,
    StorePageFault = 15U,
};

struct Trap final {
    ExceptionCause cause{ExceptionCause::IllegalInstruction};
    std::uint64_t value{0U};
    std::uint64_t program_counter{0U};
    std::uint32_t instruction{0U};
};

}  // namespace rvemu::core
