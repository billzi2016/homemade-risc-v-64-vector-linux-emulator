// 文件职责：声明与宿主浮点环境无关的 IEEE 754 二进制 32/64 位运算接口。
// 边界：本接口只处理无类型位模式和异常标志，不读取 CPU 寄存器、CSR 或物理总线。

#pragma once

#include "rvemu/core/floating_state.hpp"

#include <cstdint>

namespace rvemu::core {

enum class FloatingFormat : std::uint8_t {
    Single,
    Double,
};

enum class FloatingComparison : std::uint8_t {
    Equal,
    LessThan,
    LessThanOrEqual,
};

struct FloatingResult final {
    std::uint64_t bits{0U};
    std::uint8_t flags{0U};
};

struct FloatingIntegerResult final {
    std::uint64_t value{0U};
    std::uint8_t flags{0U};
};

struct FloatingComparisonResult final {
    bool value{false};
    std::uint8_t flags{0U};
};

// 四则运算返回目标格式的原始位模式；NaN 结果统一为 RISC-V canonical NaN。
[[nodiscard]] FloatingResult floating_add(FloatingFormat format,
                                          std::uint64_t lhs,
                                          std::uint64_t rhs,
                                          FloatingRoundingMode rounding) noexcept;
[[nodiscard]] FloatingResult floating_subtract(FloatingFormat format,
                                               std::uint64_t lhs,
                                               std::uint64_t rhs,
                                               FloatingRoundingMode rounding) noexcept;
[[nodiscard]] FloatingResult floating_multiply(FloatingFormat format,
                                               std::uint64_t lhs,
                                               std::uint64_t rhs,
                                               FloatingRoundingMode rounding) noexcept;
[[nodiscard]] FloatingResult floating_divide(FloatingFormat format,
                                             std::uint64_t lhs,
                                             std::uint64_t rhs,
                                             FloatingRoundingMode rounding) noexcept;
[[nodiscard]] FloatingResult floating_square_root(FloatingFormat format,
                                                  std::uint64_t operand,
                                                  FloatingRoundingMode rounding) noexcept;

// 四种 RISC-V FMA 通过两个显式取反开关共享一次舍入的精确执行路径。
[[nodiscard]] FloatingResult floating_fused_multiply_add(FloatingFormat format,
                                                         std::uint64_t multiplicand,
                                                         std::uint64_t multiplier,
                                                         std::uint64_t addend,
                                                         bool negate_product,
                                                         bool negate_addend,
                                                         FloatingRoundingMode rounding) noexcept;

// 符号注入、最值、比较和分类按位处理，不经过宿主浮点比较或算术。
[[nodiscard]] std::uint64_t floating_sign_inject(FloatingFormat format,
                                                 std::uint64_t magnitude,
                                                 std::uint64_t sign_source,
                                                 std::uint8_t operation) noexcept;
[[nodiscard]] FloatingResult floating_minimum_maximum(FloatingFormat format,
                                                      std::uint64_t lhs,
                                                      std::uint64_t rhs,
                                                      bool maximum) noexcept;
[[nodiscard]] FloatingComparisonResult floating_compare(FloatingFormat format,
                                                        std::uint64_t lhs,
                                                        std::uint64_t rhs,
                                                        FloatingComparison comparison) noexcept;
[[nodiscard]] std::uint16_t floating_classify(FloatingFormat format,
                                              std::uint64_t operand) noexcept;

// 格式和整数转换共用同一舍入器；整数位宽只接受 32 或 64。
[[nodiscard]] FloatingResult floating_convert_format(FloatingFormat destination,
                                                     FloatingFormat source,
                                                     std::uint64_t operand,
                                                     FloatingRoundingMode rounding) noexcept;
[[nodiscard]] FloatingResult floating_from_integer(FloatingFormat destination,
                                                   std::uint64_t operand,
                                                   bool unsigned_integer,
                                                   std::uint8_t integer_width,
                                                   FloatingRoundingMode rounding) noexcept;
[[nodiscard]] FloatingIntegerResult floating_to_integer(FloatingFormat source,
                                                        std::uint64_t operand,
                                                        bool unsigned_integer,
                                                        std::uint8_t integer_width,
                                                        FloatingRoundingMode rounding) noexcept;

}  // namespace rvemu::core
