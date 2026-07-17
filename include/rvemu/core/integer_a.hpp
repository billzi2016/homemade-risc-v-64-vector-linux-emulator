// 文件职责：定义 RV64A AMO 运算的唯一无副作用整数计算接口。
// 边界：本文件不译码 LR/SC、不访问总线，也不负责保留集、Trap 或 aq/rl 排序。

#pragma once

#include <cstdint>
#include <optional>

namespace rvemu::core {

enum class AtomicOperation : std::uint8_t {
    Swap,
    Add,
    Xor,
    And,
    Or,
    MinimumSigned,
    MaximumSigned,
    MinimumUnsigned,
    MaximumUnsigned,
};

// 把 A 扩展 funct5 映射为 AMO 运算；LR、SC 和保留编码返回空，由 CPU 分支处理。
[[nodiscard]] std::optional<AtomicOperation> decode_atomic_operation(
    std::uint8_t function5) noexcept;

// 依据 .W/.D 宽度计算待提交的新内存位模式；返回值只在指定宽度内有意义。
[[nodiscard]] std::uint64_t execute_atomic_operation(
    AtomicOperation operation,
    std::uint64_t observed,
    std::uint64_t operand,
    bool word_operation) noexcept;

}  // namespace rvemu::core
