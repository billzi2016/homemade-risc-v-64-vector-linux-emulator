// 文件职责：定义 RV64M 乘除扩展的唯一无副作用算术执行接口。
// 边界：本文件不译码 opcode、不读取寄存器、不提交 PC，也不声明来宾 Trap。

#pragma once

#include <cstdint>
#include <optional>

namespace rvemu::core {

// 根据 M 扩展 funct3 计算 RV64 或 RV64W 结果。
// 返回空表示该 funct3 在指定宽度中保留；除零和有符号溢出是合法结果而不是异常。
[[nodiscard]] std::optional<std::uint64_t> execute_integer_m(std::uint8_t function3,
                                                             std::uint64_t lhs,
                                                             std::uint64_t rhs,
                                                             bool word_operation) noexcept;

}  // namespace rvemu::core
