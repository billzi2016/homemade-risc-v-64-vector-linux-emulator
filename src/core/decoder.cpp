// 文件职责：实现 RV64I 通用字段提取和所有基础立即数的无宿主未定义行为符号扩展。
// 边界：本文件不校验 opcode/funct 合法组合，也不访问寄存器或物理总线。

#include "rvemu/core/decoder.hpp"

namespace rvemu::core {

DecodedInstruction decode(std::uint32_t bits) noexcept {
    return DecodedInstruction{
        bits,
        static_cast<std::uint8_t>(bits & 0x7FU),
        static_cast<std::uint8_t>((bits >> 7U) & 0x1FU),
        static_cast<std::uint8_t>((bits >> 12U) & 0x07U),
        static_cast<std::uint8_t>((bits >> 15U) & 0x1FU),
        static_cast<std::uint8_t>((bits >> 20U) & 0x1FU),
        static_cast<std::uint8_t>((bits >> 25U) & 0x7FU),
    };
}

std::uint64_t sign_extend(std::uint64_t value, std::uint8_t width) noexcept {
    if (width == 0U || width >= 64U) {
        return value;
    }
    const auto mask = (1ULL << width) - 1U;
    const auto sign = 1ULL << (width - 1U);
    const auto narrowed = value & mask;
    // (x xor sign) - sign 在无符号算术中精确生成二补码符号扩展，不触发有符号溢出。
    return (narrowed ^ sign) - sign;
}

std::uint64_t immediate_i(std::uint32_t bits) noexcept {
    return sign_extend(static_cast<std::uint64_t>(bits >> 20U), 12U);
}

std::uint64_t immediate_s(std::uint32_t bits) noexcept {
    const auto upper = static_cast<std::uint64_t>((bits >> 25U) & 0x7FU) << 5U;
    const auto lower = static_cast<std::uint64_t>((bits >> 7U) & 0x1FU);
    return sign_extend(upper | lower, 12U);
}

std::uint64_t immediate_b(std::uint32_t bits) noexcept {
    const auto bit12 = static_cast<std::uint64_t>((bits >> 31U) & 0x01U) << 12U;
    const auto bit11 = static_cast<std::uint64_t>((bits >> 7U) & 0x01U) << 11U;
    const auto bits10_5 = static_cast<std::uint64_t>((bits >> 25U) & 0x3FU) << 5U;
    const auto bits4_1 = static_cast<std::uint64_t>((bits >> 8U) & 0x0FU) << 1U;
    return sign_extend(bit12 | bit11 | bits10_5 | bits4_1, 13U);
}

std::uint64_t immediate_u(std::uint32_t bits) noexcept {
    return sign_extend(static_cast<std::uint64_t>(bits & 0xFFFF'F000U), 32U);
}

std::uint64_t immediate_j(std::uint32_t bits) noexcept {
    const auto bit20 = static_cast<std::uint64_t>((bits >> 31U) & 0x01U) << 20U;
    const auto bits19_12 = static_cast<std::uint64_t>((bits >> 12U) & 0xFFU) << 12U;
    const auto bit11 = static_cast<std::uint64_t>((bits >> 20U) & 0x01U) << 11U;
    const auto bits10_1 = static_cast<std::uint64_t>((bits >> 21U) & 0x3FFU) << 1U;
    return sign_extend(bit20 | bits19_12 | bit11 | bits10_1, 21U);
}

}  // namespace rvemu::core
