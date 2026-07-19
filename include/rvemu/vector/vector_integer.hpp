// 文件职责：集中定义 RVV 单宽度整数运算的严格译码、SEW 截断与无宿主未定义行为的算术语义。
// 边界：不访问寄存器、CSR、内存或 PC；CPU 负责操作数取得、掩码/tail 和架构状态提交。

#pragma once

#include <cstdint>
#include <optional>

namespace rvemu::vector {

enum class VectorIntegerOperation : std::uint8_t {
    Add,
    Subtract,
    Multiply,
    DivideUnsigned,
    DivideSigned,
    RemainderUnsigned,
    RemainderSigned
};
enum class VectorIntegerOperandForm : std::uint8_t { VectorVector, VectorScalar, VectorImmediate };

struct VectorIntegerInstruction final {
    VectorIntegerOperation operation{VectorIntegerOperation::Add};
    VectorIntegerOperandForm form{VectorIntegerOperandForm::VectorVector};
    bool masked{false};
    std::uint8_t destination{0U};
    std::uint8_t source2{0U};
    std::uint8_t source1{0U};
};

// 仅译码首版声明的 OP-V 单宽度算术；未实现 funct6、操作数形式或保留组合返回空。
[[nodiscard]] std::optional<VectorIntegerInstruction> decode_vector_integer_instruction(
    std::uint32_t instruction) noexcept;
// 以给定 SEW 计算一个截断结果；除零和 signed overflow 与标量 RISC-V M 扩展一致，不产生宿主异常。
[[nodiscard]] std::uint64_t execute_vector_integer_operation(VectorIntegerOperation operation,
                                                             std::uint64_t lhs,
                                                             std::uint64_t rhs,
                                                             std::uint8_t sew_bits) noexcept;
// 将 5 位 vi 立即数按 RVV 规则符号扩展并截断到 SEW。
[[nodiscard]] std::uint64_t sign_extend_vector_immediate(std::uint8_t immediate,
                                                         std::uint8_t sew_bits) noexcept;

}  // namespace rvemu::vector
