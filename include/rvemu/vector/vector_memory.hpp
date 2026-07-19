// 文件职责：集中译码 RVV unit-stride/strided 普通向量访存，并计算其 EEW 对应 EMUL 配置。
// 边界：本文件不访问 CPU、MMU 或总线；执行顺序、Trap 和元素提交全部由 CPU 统一控制。

#pragma once

#include "rvemu/vector/vector_configuration.hpp"

#include <cstdint>
#include <optional>

namespace rvemu::vector {

enum class VectorMemoryAddressingMode : std::uint8_t {
    UnitStride,
    Strided,
};

struct VectorMemoryOperation final {
    bool load{false};
    bool masked{false};
    std::uint8_t element_width_bits{0U};
    VectorMemoryAddressingMode addressing_mode{VectorMemoryAddressingMode::UnitStride};
    std::uint8_t vector_register{0U};
    std::uint8_t base_register{0U};
    std::uint8_t stride_register{0U};
};

// 仅接受本阶段声明的普通非分段 unit-stride/strided 编码；保留、索引、mask/whole-register 和 fault-only-first 形式返回空。
[[nodiscard]] std::optional<VectorMemoryOperation> decode_vector_memory_operation(
    std::uint32_t instruction,
    bool load) noexcept;

}  // namespace rvemu::vector
