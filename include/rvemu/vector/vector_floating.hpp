// 文件职责：定义 RVV 单宽度浮点四则运算的严格 OP-V 译码和纯位模式数值分发接口。
// 边界：不访问 CPU 寄存器、CSR 或 PC；调用方负责向量掩码、tail、vstart 与异常标志提交。

#pragma once

#include "rvemu/core/soft_float.hpp"

#include <cstdint>
#include <optional>

namespace rvemu::vector {

// RVV 浮点指令的第二操作数只能来自向量寄存器或标量浮点寄存器。
enum class VectorFloatingOperandForm : std::uint8_t { VectorVector, VectorScalarFloating };
enum class VectorFloatingOperation : std::uint8_t { Add, Subtract, Multiply, Divide };

struct VectorFloatingInstruction final {
    VectorFloatingOperation operation{VectorFloatingOperation::Add};
    VectorFloatingOperandForm form{VectorFloatingOperandForm::VectorVector};
    bool masked{false};
    std::uint8_t destination{0U};
    std::uint8_t source2{0U};
    std::uint8_t source1{0U};
};

// 仅接受 RVV 1.0 首版承诺的 vfadd/vfsub/vfmul/vfdiv 的 vv/vf 编码；保留或其他 OP-V 编码返回空。
[[nodiscard]] std::optional<VectorFloatingInstruction> decode_vector_floating_instruction(
    std::uint32_t instruction) noexcept;
// 将一个已验证的活动元素交给唯一的软件浮点核心；输入和输出均是 SEW 宽度的原始 IEEE 位模式。
[[nodiscard]] core::FloatingResult execute_vector_floating_operation(
    VectorFloatingOperation operation,
    core::FloatingFormat format,
    std::uint64_t lhs,
    std::uint64_t rhs,
    core::FloatingRoundingMode rounding) noexcept;

}  // namespace rvemu::vector
