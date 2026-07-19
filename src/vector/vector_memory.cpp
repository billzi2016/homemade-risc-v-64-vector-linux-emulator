// 文件职责：实现 RVV 1.0 普通 unit-stride/strided 访存字段的严格解码。
// 边界：不把未实现的索引、分段、whole-register、mask 或 fault-only-first 编码近似为普通访存。

#include "rvemu/vector/vector_memory.hpp"

namespace rvemu::vector {
namespace {

[[nodiscard]] std::optional<std::uint8_t> decode_element_width(std::uint8_t encoding) noexcept {
    switch (encoding) {
        case 0U:
            return 8U;
        case 5U:
            return 16U;
        case 6U:
            return 32U;
        case 7U:
            return 64U;
        default:
            return std::nullopt;
    }
}

}  // namespace

std::optional<VectorMemoryOperation> decode_vector_memory_operation(std::uint32_t instruction,
                                                                    bool load) noexcept {
    const auto nf = static_cast<std::uint8_t>((instruction >> 29U) & 0x7U);
    const auto mew = (instruction & (1U << 28U)) != 0U;
    const auto mop = static_cast<std::uint8_t>((instruction >> 26U) & 0x3U);
    const auto fields = static_cast<std::uint8_t>((instruction >> 20U) & 0x1FU);
    const auto width = decode_element_width(static_cast<std::uint8_t>((instruction >> 12U) & 0x7U));
    if (nf != 0U || mew || !width.has_value()) {
        return std::nullopt;
    }
    VectorMemoryAddressingMode mode{};
    if (mop == 0U) {
        // lumop/sumop=0 才是普通 unit-stride；其余值分别保留或属于未声明扩展。
        if (fields != 0U) {
            return std::nullopt;
        }
        mode = VectorMemoryAddressingMode::UnitStride;
    } else if (mop == 2U) {
        mode = VectorMemoryAddressingMode::Strided;
    } else {
        return std::nullopt;
    }
    return VectorMemoryOperation{
        load,
        (instruction & (1U << 25U)) == 0U,
        *width,
        mode,
        static_cast<std::uint8_t>((instruction >> 7U) & 0x1FU),
        static_cast<std::uint8_t>((instruction >> 15U) & 0x1FU),
        fields,
    };
}

}  // namespace rvemu::vector
