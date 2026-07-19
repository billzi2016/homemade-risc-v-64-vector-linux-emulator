// 文件职责：实现 RVV 单宽度整数算术、OP-V 字段校验与 RISC-V 定义的除法边界结果。
// 边界：本文件是纯值语义，不决定元素是否活动，也不处理 Trap、vstart 或向量寄存器写回。

#include "rvemu/vector/vector_integer.hpp"

namespace rvemu::vector {
namespace {
[[nodiscard]] constexpr std::uint64_t mask_for(std::uint8_t width) noexcept { return width == 64U ? ~0ULL : (1ULL << width) - 1ULL; }
[[nodiscard]] constexpr bool negative(std::uint64_t value, std::uint8_t width) noexcept { return (value & (1ULL << (width - 1U))) != 0U; }
[[nodiscard]] constexpr std::uint64_t negate(std::uint64_t value, std::uint64_t mask) noexcept { return (-value) & mask; }
[[nodiscard]] constexpr std::uint64_t signed_divide(std::uint64_t lhs, std::uint64_t rhs, std::uint8_t width) noexcept {
    const auto mask = mask_for(width); lhs &= mask; rhs &= mask;
    if (rhs == 0U) return mask;
    const auto minimum = 1ULL << (width - 1U);
    if (lhs == minimum && rhs == mask) return lhs;
    const auto lhs_negative = negative(lhs, width); const auto rhs_negative = negative(rhs, width);
    const auto quotient = (lhs_negative ? negate(lhs, mask) : lhs) / (rhs_negative ? negate(rhs, mask) : rhs);
    return lhs_negative != rhs_negative ? negate(quotient, mask) : quotient;
}
}  // namespace

std::optional<VectorIntegerInstruction> decode_vector_integer_instruction(std::uint32_t instruction) noexcept {
    const auto funct3 = static_cast<std::uint8_t>((instruction >> 12U) & 0x7U);
    VectorIntegerOperandForm form{};
    if (funct3 == 0U) form = VectorIntegerOperandForm::VectorVector;
    else if (funct3 == 4U) form = VectorIntegerOperandForm::VectorScalar;
    else if (funct3 == 3U) form = VectorIntegerOperandForm::VectorImmediate;
    else return std::nullopt;
    const auto funct6 = static_cast<std::uint8_t>((instruction >> 26U) & 0x3FU);
    VectorIntegerOperation operation{};
    switch (funct6) {
    case 0x00U: operation = VectorIntegerOperation::Add; break;
    case 0x02U: operation = VectorIntegerOperation::Subtract; break;
    case 0x25U: operation = VectorIntegerOperation::Multiply; break;
    case 0x20U: operation = VectorIntegerOperation::DivideUnsigned; break;
    case 0x21U: operation = VectorIntegerOperation::DivideSigned; break;
    case 0x22U: operation = VectorIntegerOperation::RemainderUnsigned; break;
    case 0x23U: operation = VectorIntegerOperation::RemainderSigned; break;
    default: return std::nullopt;
    }
    if (form == VectorIntegerOperandForm::VectorImmediate && operation != VectorIntegerOperation::Add) return std::nullopt;
    return VectorIntegerInstruction{operation, form, (instruction & (1U << 25U)) == 0U,
        static_cast<std::uint8_t>((instruction >> 7U) & 0x1FU), static_cast<std::uint8_t>((instruction >> 20U) & 0x1FU), static_cast<std::uint8_t>((instruction >> 15U) & 0x1FU)};
}

std::uint64_t execute_vector_integer_operation(VectorIntegerOperation operation, std::uint64_t lhs, std::uint64_t rhs, std::uint8_t sew_bits) noexcept {
    const auto mask = mask_for(sew_bits); lhs &= mask; rhs &= mask;
    switch (operation) {
    case VectorIntegerOperation::Add: return (lhs + rhs) & mask;
    case VectorIntegerOperation::Subtract: return (lhs - rhs) & mask;
    case VectorIntegerOperation::Multiply: return (lhs * rhs) & mask;
    case VectorIntegerOperation::DivideUnsigned: return rhs == 0U ? mask : lhs / rhs;
    case VectorIntegerOperation::RemainderUnsigned: return rhs == 0U ? lhs : lhs % rhs;
    case VectorIntegerOperation::DivideSigned: return signed_divide(lhs, rhs, sew_bits);
    case VectorIntegerOperation::RemainderSigned:
        if (rhs == 0U) return lhs;
        if (lhs == (1ULL << (sew_bits - 1U)) && rhs == mask) return 0U;
        return (lhs - signed_divide(lhs, rhs, sew_bits) * rhs) & mask;
    }
    return 0U;
}

std::uint64_t sign_extend_vector_immediate(std::uint8_t immediate, std::uint8_t sew_bits) noexcept {
    const auto signed_value = (immediate & 0x10U) != 0U ? static_cast<std::uint64_t>(immediate | 0xE0U) : immediate;
    return signed_value & mask_for(sew_bits);
}
}  // namespace rvemu::vector
