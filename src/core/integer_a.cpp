// 文件职责：实现 RV64A 全部 AMO 的 32/64 位二补码运算与 funct5 映射。
// 边界：这里只计算候选新值；原子提交、重试、保留失效和寄存器写回由 CPU/总线完成。

#include "rvemu/core/integer_a.hpp"

#include <cstdint>

namespace rvemu::core {
namespace {

// 翻转符号位把二补码有符号序映射为无符号序，避免依赖宿主有符号转换结果。
template <typename UInt>
[[nodiscard]] constexpr bool signed_less(UInt lhs, UInt rhs) noexcept {
    constexpr auto sign_bit = static_cast<UInt>(UInt{1U} << ((sizeof(UInt) * 8U) - 1U));
    return static_cast<UInt>(lhs ^ sign_bit) < static_cast<UInt>(rhs ^ sign_bit);
}

template <typename UInt>
[[nodiscard]] constexpr UInt calculate(
    AtomicOperation operation,
    UInt observed,
    UInt operand) noexcept {
    switch (operation) {
    case AtomicOperation::Swap:
        return operand;
    case AtomicOperation::Add:
        return static_cast<UInt>(observed + operand);
    case AtomicOperation::Xor:
        return static_cast<UInt>(observed ^ operand);
    case AtomicOperation::And:
        return static_cast<UInt>(observed & operand);
    case AtomicOperation::Or:
        return static_cast<UInt>(observed | operand);
    case AtomicOperation::MinimumSigned:
        return signed_less(observed, operand) ? observed : operand;
    case AtomicOperation::MaximumSigned:
        return signed_less(observed, operand) ? operand : observed;
    case AtomicOperation::MinimumUnsigned:
        return observed < operand ? observed : operand;
    case AtomicOperation::MaximumUnsigned:
        return observed < operand ? operand : observed;
    }
    return observed;
}

}  // namespace

std::optional<AtomicOperation> decode_atomic_operation(std::uint8_t function5) noexcept {
    switch (function5) {
    case 0x01U:
        return AtomicOperation::Swap;
    case 0x00U:
        return AtomicOperation::Add;
    case 0x04U:
        return AtomicOperation::Xor;
    case 0x0CU:
        return AtomicOperation::And;
    case 0x08U:
        return AtomicOperation::Or;
    case 0x10U:
        return AtomicOperation::MinimumSigned;
    case 0x14U:
        return AtomicOperation::MaximumSigned;
    case 0x18U:
        return AtomicOperation::MinimumUnsigned;
    case 0x1CU:
        return AtomicOperation::MaximumUnsigned;
    default:
        return std::nullopt;
    }
}

std::uint64_t execute_atomic_operation(
    AtomicOperation operation,
    std::uint64_t observed,
    std::uint64_t operand,
    bool word_operation) noexcept {
    if (word_operation) {
        return calculate(
            operation,
            static_cast<std::uint32_t>(observed),
            static_cast<std::uint32_t>(operand));
    }
    return calculate(operation, observed, operand);
}

}  // namespace rvemu::core
