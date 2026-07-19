// 文件职责：实现 RVV 浮点四则运算的严格字段译码，并复用核心软件浮点实现计算单个元素。
// 边界：此处不复制 IEEE-754 算法；所有舍入、NaN 与异常位语义只存在于 core::soft_float 中。

#include "rvemu/vector/vector_floating.hpp"

namespace rvemu::vector {

std::optional<VectorFloatingInstruction> decode_vector_floating_instruction(
    std::uint32_t instruction) noexcept {
    const auto funct3 = static_cast<std::uint8_t>((instruction >> 12U) & 0x7U);
    VectorFloatingOperandForm form{};
    if (funct3 == 0x1U) {
        form = VectorFloatingOperandForm::VectorVector;
    } else if (funct3 == 0x5U) {
        form = VectorFloatingOperandForm::VectorScalarFloating;
    } else {
        return std::nullopt;
    }
    const auto funct6 = static_cast<std::uint8_t>((instruction >> 26U) & 0x3FU);
    VectorFloatingOperation operation{};
    switch (funct6) {
        case 0x00U:
            operation = VectorFloatingOperation::Add;
            break;
        case 0x02U:
            operation = VectorFloatingOperation::Subtract;
            break;
        case 0x20U:
            operation = VectorFloatingOperation::Divide;
            break;
        case 0x24U:
            operation = VectorFloatingOperation::Multiply;
            break;
        default:
            return std::nullopt;
    }
    return VectorFloatingInstruction{operation,
                                     form,
                                     (instruction & (1U << 25U)) == 0U,
                                     static_cast<std::uint8_t>((instruction >> 7U) & 0x1FU),
                                     static_cast<std::uint8_t>((instruction >> 20U) & 0x1FU),
                                     static_cast<std::uint8_t>((instruction >> 15U) & 0x1FU)};
}

core::FloatingResult execute_vector_floating_operation(
    VectorFloatingOperation operation,
    core::FloatingFormat format,
    std::uint64_t lhs,
    std::uint64_t rhs,
    core::FloatingRoundingMode rounding) noexcept {
    switch (operation) {
        case VectorFloatingOperation::Add:
            return core::floating_add(format, lhs, rhs, rounding);
        case VectorFloatingOperation::Subtract:
            return core::floating_subtract(format, lhs, rhs, rounding);
        case VectorFloatingOperation::Multiply:
            return core::floating_multiply(format, lhs, rhs, rounding);
        case VectorFloatingOperation::Divide:
            return core::floating_divide(format, lhs, rhs, rounding);
    }
    return core::FloatingResult{};
}

}  // namespace rvemu::vector
