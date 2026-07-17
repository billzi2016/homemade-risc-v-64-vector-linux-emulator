// 文件职责：完整实现 RV64C 2.0 三个 quadrant 的寄存器、立即数、HINT 与保留编码规则。
// 边界：所有合法结果只映射到一条既有 RV64I/F/D 指令，绝不在解压层复制执行语义。

#include "rvemu/core/compressed_decoder.hpp"

#include "rvemu/core/decoder.hpp"

#include <cstdint>
#include <optional>

namespace rvemu::core {
namespace {

constexpr std::uint32_t kNop = 0x0000'0013U;
constexpr std::uint32_t kEbreak = 0x0010'0073U;

[[nodiscard]] constexpr std::uint8_t compact_register(std::uint16_t field) noexcept {
    return static_cast<std::uint8_t>(8U + (field & 0x7U));
}

[[nodiscard]] constexpr std::uint8_t full_register(
    std::uint16_t bits,
    std::uint8_t shift) noexcept {
    return static_cast<std::uint8_t>((bits >> shift) & 0x1FU);
}

[[nodiscard]] constexpr std::uint32_t encode_i(
    std::uint8_t opcode,
    std::uint8_t destination,
    std::uint8_t function3,
    std::uint8_t source,
    std::uint64_t immediate) noexcept {
    return (static_cast<std::uint32_t>(immediate & 0xFFFU) << 20U) |
           (static_cast<std::uint32_t>(source) << 15U) |
           (static_cast<std::uint32_t>(function3) << 12U) |
           (static_cast<std::uint32_t>(destination) << 7U) | opcode;
}

[[nodiscard]] constexpr std::uint32_t encode_r(
    std::uint8_t opcode,
    std::uint8_t destination,
    std::uint8_t function3,
    std::uint8_t source1,
    std::uint8_t source2,
    std::uint8_t function7) noexcept {
    return (static_cast<std::uint32_t>(function7) << 25U) |
           (static_cast<std::uint32_t>(source2) << 20U) |
           (static_cast<std::uint32_t>(source1) << 15U) |
           (static_cast<std::uint32_t>(function3) << 12U) |
           (static_cast<std::uint32_t>(destination) << 7U) | opcode;
}

[[nodiscard]] constexpr std::uint32_t encode_s(
    std::uint8_t opcode,
    std::uint8_t function3,
    std::uint8_t source1,
    std::uint8_t source2,
    std::uint64_t immediate) noexcept {
    const auto encoded = static_cast<std::uint32_t>(immediate & 0xFFFU);
    return ((encoded >> 5U) << 25U) |
           (static_cast<std::uint32_t>(source2) << 20U) |
           (static_cast<std::uint32_t>(source1) << 15U) |
           (static_cast<std::uint32_t>(function3) << 12U) |
           ((encoded & 0x1FU) << 7U) | opcode;
}

[[nodiscard]] constexpr std::uint32_t encode_b(
    std::uint8_t function3,
    std::uint8_t source1,
    std::uint8_t source2,
    std::uint64_t immediate) noexcept {
    const auto encoded = static_cast<std::uint32_t>(immediate & 0x1FFFU);
    return (((encoded >> 12U) & 1U) << 31U) |
           (((encoded >> 5U) & 0x3FU) << 25U) |
           (static_cast<std::uint32_t>(source2) << 20U) |
           (static_cast<std::uint32_t>(source1) << 15U) |
           (static_cast<std::uint32_t>(function3) << 12U) |
           (((encoded >> 1U) & 0x0FU) << 8U) |
           (((encoded >> 11U) & 1U) << 7U) | 0x63U;
}

[[nodiscard]] constexpr std::uint32_t encode_j(std::uint64_t immediate) noexcept {
    const auto encoded = static_cast<std::uint32_t>(immediate & 0x1F'FFFFU);
    return (((encoded >> 20U) & 1U) << 31U) |
           (((encoded >> 1U) & 0x3FFU) << 21U) |
           (((encoded >> 11U) & 1U) << 20U) |
           (((encoded >> 12U) & 0xFFU) << 12U) | 0x6FU;
}

[[nodiscard]] std::uint64_t ci_immediate(std::uint16_t bits) noexcept {
    const auto encoded = static_cast<std::uint64_t>(
        ((bits >> 7U) & 0x20U) | ((bits >> 2U) & 0x1FU));
    return sign_extend(encoded, 6U);
}

[[nodiscard]] std::uint64_t jump_immediate(std::uint16_t bits) noexcept {
    const auto encoded =
        (static_cast<std::uint64_t>((bits >> 12U) & 1U) << 11U) |
        (static_cast<std::uint64_t>((bits >> 11U) & 1U) << 4U) |
        (static_cast<std::uint64_t>((bits >> 9U) & 0x3U) << 8U) |
        (static_cast<std::uint64_t>((bits >> 8U) & 1U) << 10U) |
        (static_cast<std::uint64_t>((bits >> 7U) & 1U) << 6U) |
        (static_cast<std::uint64_t>((bits >> 6U) & 1U) << 7U) |
        (static_cast<std::uint64_t>((bits >> 3U) & 0x7U) << 1U) |
        (static_cast<std::uint64_t>((bits >> 2U) & 1U) << 5U);
    return sign_extend(encoded, 12U);
}

[[nodiscard]] std::uint64_t branch_immediate(std::uint16_t bits) noexcept {
    const auto encoded =
        (static_cast<std::uint64_t>((bits >> 12U) & 1U) << 8U) |
        (static_cast<std::uint64_t>((bits >> 10U) & 0x3U) << 3U) |
        (static_cast<std::uint64_t>((bits >> 5U) & 0x3U) << 6U) |
        (static_cast<std::uint64_t>((bits >> 3U) & 0x3U) << 1U) |
        (static_cast<std::uint64_t>((bits >> 2U) & 1U) << 5U);
    return sign_extend(encoded, 9U);
}

[[nodiscard]] std::optional<std::uint32_t> decompress_quadrant0(
    std::uint16_t bits,
    std::uint8_t function3) noexcept {
    const auto destination = compact_register(static_cast<std::uint16_t>(bits >> 2U));
    const auto source1 = compact_register(static_cast<std::uint16_t>(bits >> 7U));
    const auto source2 = compact_register(static_cast<std::uint16_t>(bits >> 2U));
    if (function3 == 0U) {  // C.ADDI4SPN
        const auto immediate =
            (static_cast<std::uint64_t>((bits >> 7U) & 0xFU) << 6U) |
            (static_cast<std::uint64_t>((bits >> 11U) & 0x3U) << 4U) |
            (static_cast<std::uint64_t>((bits >> 5U) & 1U) << 3U) |
            (static_cast<std::uint64_t>((bits >> 6U) & 1U) << 2U);
        if (immediate == 0U) {
            return std::nullopt;
        }
        return encode_i(0x13U, destination, 0U, 2U, immediate);
    }
    if (function3 == 1U || function3 == 3U) {  // C.FLD / C.LD
        const auto immediate =
            (static_cast<std::uint64_t>((bits >> 5U) & 0x3U) << 6U) |
            (static_cast<std::uint64_t>((bits >> 10U) & 0x7U) << 3U);
        return encode_i(function3 == 1U ? 0x07U : 0x03U,
                        destination, 3U, source1, immediate);
    }
    if (function3 == 2U) {  // C.LW
        const auto immediate =
            (static_cast<std::uint64_t>((bits >> 5U) & 1U) << 6U) |
            (static_cast<std::uint64_t>((bits >> 10U) & 0x7U) << 3U) |
            (static_cast<std::uint64_t>((bits >> 6U) & 1U) << 2U);
        return encode_i(0x03U, destination, 2U, source1, immediate);
    }
    if (function3 == 5U || function3 == 7U) {  // C.FSD / C.SD
        const auto immediate =
            (static_cast<std::uint64_t>((bits >> 5U) & 0x3U) << 6U) |
            (static_cast<std::uint64_t>((bits >> 10U) & 0x7U) << 3U);
        return encode_s(function3 == 5U ? 0x27U : 0x23U,
                        3U, source1, source2, immediate);
    }
    if (function3 == 6U) {  // C.SW
        const auto immediate =
            (static_cast<std::uint64_t>((bits >> 5U) & 1U) << 6U) |
            (static_cast<std::uint64_t>((bits >> 10U) & 0x7U) << 3U) |
            (static_cast<std::uint64_t>((bits >> 6U) & 1U) << 2U);
        return encode_s(0x23U, 2U, source1, source2, immediate);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> decompress_misc_alu(
    std::uint16_t bits) noexcept {
    const auto destination = compact_register(static_cast<std::uint16_t>(bits >> 7U));
    const auto selector = static_cast<std::uint8_t>((bits >> 10U) & 0x3U);
    if (selector < 2U) {  // C.SRLI / C.SRAI
        const auto shift = static_cast<std::uint64_t>(
            ((bits >> 7U) & 0x20U) | ((bits >> 2U) & 0x1FU));
        if (shift == 0U) {
            return kNop;
        }
        const auto immediate = selector == 0U ? shift : (0x400U | shift);
        return encode_i(0x13U, destination, 5U, destination, immediate);
    }
    if (selector == 2U) {  // C.ANDI
        return encode_i(0x13U, destination, 7U, destination, ci_immediate(bits));
    }

    const auto source2 = compact_register(static_cast<std::uint16_t>(bits >> 2U));
    const auto function2 = static_cast<std::uint8_t>((bits >> 5U) & 0x3U);
    const auto word_operation = ((bits >> 12U) & 1U) != 0U;
    if (!word_operation) {
        constexpr std::uint8_t functions[4U]{0U, 4U, 6U, 7U};
        return encode_r(
            0x33U,
            destination,
            functions[function2],
            destination,
            source2,
            function2 == 0U ? 0x20U : 0U);
    }
    if (function2 > 1U) {
        return std::nullopt;
    }
    return encode_r(
        0x3BU,
        destination,
        0U,
        destination,
        source2,
        function2 == 0U ? 0x20U : 0U);
}

[[nodiscard]] std::optional<std::uint32_t> decompress_quadrant1(
    std::uint16_t bits,
    std::uint8_t function3) noexcept {
    const auto destination = full_register(bits, 7U);
    const auto immediate = ci_immediate(bits);
    if (function3 == 0U) {  // C.NOP / C.ADDI / HINT
        if (destination == 0U || immediate == 0U) {
            return kNop;
        }
        return encode_i(0x13U, destination, 0U, destination, immediate);
    }
    if (function3 == 1U) {  // C.ADDIW（RV64）
        if (destination == 0U) {
            return std::nullopt;
        }
        return encode_i(0x1BU, destination, 0U, destination, immediate);
    }
    if (function3 == 2U) {  // C.LI / HINT
        if (destination == 0U) {
            return kNop;
        }
        return encode_i(0x13U, destination, 0U, 0U, immediate);
    }
    if (function3 == 3U) {  // C.ADDI16SP / C.LUI
        if (destination == 2U) {
            const auto encoded =
                (static_cast<std::uint64_t>((bits >> 12U) & 1U) << 9U) |
                (static_cast<std::uint64_t>((bits >> 6U) & 1U) << 4U) |
                (static_cast<std::uint64_t>((bits >> 5U) & 1U) << 6U) |
                (static_cast<std::uint64_t>((bits >> 3U) & 0x3U) << 7U) |
                (static_cast<std::uint64_t>((bits >> 2U) & 1U) << 5U);
            const auto stack_immediate = sign_extend(encoded, 10U);
            if (stack_immediate == 0U) {
                return std::nullopt;
            }
            return encode_i(0x13U, 2U, 0U, 2U, stack_immediate);
        }
        if (immediate == 0U) {
            return std::nullopt;
        }
        if (destination == 0U) {
            return kNop;
        }
        return static_cast<std::uint32_t>((immediate << 12U) & 0xFFFF'F000ULL) |
               (static_cast<std::uint32_t>(destination) << 7U) | 0x37U;
    }
    if (function3 == 4U) {
        return decompress_misc_alu(bits);
    }
    if (function3 == 5U) {  // C.J
        return encode_j(jump_immediate(bits));
    }
    if (function3 == 6U || function3 == 7U) {  // C.BEQZ / C.BNEZ
        const auto source = compact_register(static_cast<std::uint16_t>(bits >> 7U));
        return encode_b(
            function3 == 6U ? 0U : 1U, source, 0U, branch_immediate(bits));
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> decompress_cr(
    std::uint16_t bits) noexcept {
    const auto destination = full_register(bits, 7U);
    const auto source2 = full_register(bits, 2U);
    const auto high_bit = ((bits >> 12U) & 1U) != 0U;
    if (!high_bit && source2 == 0U) {  // C.JR
        if (destination == 0U) {
            return std::nullopt;
        }
        return encode_i(0x67U, 0U, 0U, destination, 0U);
    }
    if (!high_bit) {  // C.MV / HINT
        if (destination == 0U) {
            return kNop;
        }
        return encode_r(0x33U, destination, 0U, 0U, source2, 0U);
    }
    if (source2 == 0U) {
        if (destination == 0U) {
            return kEbreak;
        }
        return encode_i(0x67U, 1U, 0U, destination, 0U);  // C.JALR
    }
    if (destination == 0U) {  // C.ADD HINT，包括标准 C.NTL.* 编码
        return kNop;
    }
    return encode_r(0x33U, destination, 0U, destination, source2, 0U);
}

[[nodiscard]] std::optional<std::uint32_t> decompress_quadrant2(
    std::uint16_t bits,
    std::uint8_t function3) noexcept {
    const auto destination = full_register(bits, 7U);
    const auto source2 = full_register(bits, 2U);
    if (function3 == 0U) {  // C.SLLI / HINT
        const auto shift = static_cast<std::uint64_t>(
            ((bits >> 7U) & 0x20U) | ((bits >> 2U) & 0x1FU));
        if (destination == 0U || shift == 0U) {
            return kNop;
        }
        return encode_i(0x13U, destination, 1U, destination, shift);
    }
    if (function3 == 1U || function3 == 3U) {  // C.FLDSP / C.LDSP
        const auto immediate =
            (static_cast<std::uint64_t>((bits >> 2U) & 0x7U) << 6U) |
            (static_cast<std::uint64_t>((bits >> 12U) & 1U) << 5U) |
            (static_cast<std::uint64_t>((bits >> 5U) & 0x3U) << 3U);
        if (function3 == 3U && destination == 0U) {
            return std::nullopt;
        }
        return encode_i(function3 == 1U ? 0x07U : 0x03U,
                        destination, 3U, 2U, immediate);
    }
    if (function3 == 2U) {  // C.LWSP
        if (destination == 0U) {
            return std::nullopt;
        }
        const auto immediate =
            (static_cast<std::uint64_t>((bits >> 2U) & 0x3U) << 6U) |
            (static_cast<std::uint64_t>((bits >> 12U) & 1U) << 5U) |
            (static_cast<std::uint64_t>((bits >> 4U) & 0x7U) << 2U);
        return encode_i(0x03U, destination, 2U, 2U, immediate);
    }
    if (function3 == 4U) {
        return decompress_cr(bits);
    }
    if (function3 == 5U || function3 == 7U) {  // C.FSDSP / C.SDSP
        const auto immediate =
            (static_cast<std::uint64_t>((bits >> 7U) & 0x7U) << 6U) |
            (static_cast<std::uint64_t>((bits >> 10U) & 0x7U) << 3U);
        return encode_s(function3 == 5U ? 0x27U : 0x23U,
                        3U, 2U, source2, immediate);
    }
    if (function3 == 6U) {  // C.SWSP
        const auto immediate =
            (static_cast<std::uint64_t>((bits >> 7U) & 0x3U) << 6U) |
            (static_cast<std::uint64_t>((bits >> 9U) & 0xFU) << 2U);
        return encode_s(0x23U, 2U, 2U, source2, immediate);
    }
    return std::nullopt;
}

}  // namespace

std::optional<std::uint32_t> decompress_rv64c(std::uint16_t bits) noexcept {
    const auto quadrant = static_cast<std::uint8_t>(bits & 0x3U);
    const auto function3 = static_cast<std::uint8_t>((bits >> 13U) & 0x7U);
    if (quadrant == 0U) {
        return decompress_quadrant0(bits, function3);
    }
    if (quadrant == 1U) {
        return decompress_quadrant1(bits, function3);
    }
    if (quadrant == 2U) {
        return decompress_quadrant2(bits, function3);
    }
    return std::nullopt;
}

}  // namespace rvemu::core
