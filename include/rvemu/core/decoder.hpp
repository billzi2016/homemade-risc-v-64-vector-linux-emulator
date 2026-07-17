// 文件职责：集中提取 RISC-V 32 位编码字段和 I/S/B/U/J 立即数。
// 边界：译码辅助函数不读取 CPU 状态，不决定扩展是否启用，也不产生任何架构副作用。

#pragma once

#include "rvemu/core/instruction.hpp"

#include <cstdint>

namespace rvemu::core {

[[nodiscard]] DecodedInstruction decode(std::uint32_t bits) noexcept;

[[nodiscard]] std::uint64_t sign_extend(std::uint64_t value, std::uint8_t width) noexcept;
[[nodiscard]] std::uint64_t immediate_i(std::uint32_t bits) noexcept;
[[nodiscard]] std::uint64_t immediate_s(std::uint32_t bits) noexcept;
[[nodiscard]] std::uint64_t immediate_b(std::uint32_t bits) noexcept;
[[nodiscard]] std::uint64_t immediate_u(std::uint32_t bits) noexcept;
[[nodiscard]] std::uint64_t immediate_j(std::uint32_t bits) noexcept;

}  // namespace rvemu::core
