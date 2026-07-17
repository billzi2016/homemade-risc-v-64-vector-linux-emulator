// 文件职责：实现 binary32/binary64 互转以及 RV64 浮点与 32/64 位整数之间的精确转换。
// 边界：所有舍入委托统一软件舍入器；本文件不执行指令译码或寄存器提交。

#include "soft_float_internal.hpp"

#include <cstdint>
#include <limits>

namespace rvemu::core {
namespace {

using soft_float_detail::Category;
using soft_float_detail::WideUnsigned;

[[nodiscard]] std::uint64_t width_mask(std::uint8_t width) noexcept {
    return width == 32U ? 0xFFFF'FFFFULL : std::numeric_limits<std::uint64_t>::max();
}

[[nodiscard]] std::uint64_t signed_maximum(std::uint8_t width) noexcept {
    return width == 32U ? 0x7FFF'FFFFULL : 0x7FFF'FFFF'FFFF'FFFFULL;
}

[[nodiscard]] std::uint64_t signed_minimum_bits(std::uint8_t width) noexcept {
    return width == 32U ? 0x8000'0000ULL : 0x8000'0000'0000'0000ULL;
}

[[nodiscard]] FloatingIntegerResult invalid_integer_result(
    bool sign,
    bool unsigned_integer,
    std::uint8_t width) noexcept {
    std::uint64_t value = 0U;
    if (unsigned_integer) {
        value = sign ? 0U : width_mask(width);
    } else {
        value = sign ? signed_minimum_bits(width) : signed_maximum(width);
    }
    return FloatingIntegerResult{
        value,
        static_cast<std::uint8_t>(FloatingExceptionFlag::InvalidOperation)};
}

}  // namespace

FloatingResult floating_convert_format(
    FloatingFormat destination,
    FloatingFormat source,
    std::uint64_t operand_bits,
    FloatingRoundingMode rounding) noexcept {
    const auto& target = soft_float_detail::format_of(destination);
    const auto operand = soft_float_detail::unpack(source, operand_bits);
    if (soft_float_detail::is_nan(operand.category)) {
        return FloatingResult{
            target.canonical_nan,
            static_cast<std::uint8_t>(
                operand.category == Category::SignalingNan ?
                    soft_float_detail::invalid_flag() : 0U)};
    }
    if (operand.category == Category::Infinity) {
        return FloatingResult{soft_float_detail::infinity(target, operand.sign), 0U};
    }
    if (operand.category == Category::Zero) {
        return FloatingResult{soft_float_detail::signed_zero(target, operand.sign), 0U};
    }
    return soft_float_detail::round_and_pack(
        target,
        operand.sign,
        WideUnsigned{operand.significand},
        operand.exponent,
        rounding);
}

FloatingResult floating_from_integer(
    FloatingFormat destination,
    std::uint64_t operand,
    bool unsigned_integer,
    std::uint8_t integer_width,
    FloatingRoundingMode rounding) noexcept {
    const auto mask = width_mask(integer_width);
    operand &= mask;
    bool sign = false;
    std::uint64_t magnitude = operand;
    if (!unsigned_integer) {
        const auto sign_bit = integer_width == 32U ? 0x8000'0000ULL :
                                                    0x8000'0000'0000'0000ULL;
        sign = (operand & sign_bit) != 0U;
        if (sign) {
            magnitude = ((~operand) + 1U) & mask;
        }
    }
    return soft_float_detail::round_and_pack(
        soft_float_detail::format_of(destination),
        sign,
        WideUnsigned{magnitude},
        0,
        rounding);
}

FloatingIntegerResult floating_to_integer(
    FloatingFormat source,
    std::uint64_t operand_bits,
    bool unsigned_integer,
    std::uint8_t integer_width,
    FloatingRoundingMode rounding) noexcept {
    const auto operand = soft_float_detail::unpack(source, operand_bits);
    if (soft_float_detail::is_nan(operand.category)) {
        // RISC-V 规定 NaN 与正溢出使用相同饱和值，而不读取 NaN 的符号位。
        return invalid_integer_result(false, unsigned_integer, integer_width);
    }
    if (operand.category == Category::Infinity) {
        return invalid_integer_result(operand.sign, unsigned_integer, integer_width);
    }
    if (operand.category == Category::Zero) {
        return FloatingIntegerResult{};
    }

    bool inexact = false;
    const auto magnitude = soft_float_detail::round_to_integer_magnitude(
        WideUnsigned{operand.significand},
        operand.exponent,
        operand.sign,
        rounding,
        inexact);

    if (magnitude.bit_length() > 64U) {
        return invalid_integer_result(operand.sign, unsigned_integer, integer_width);
    }
    const auto rounded = magnitude.low64();
    if (unsigned_integer) {
        if ((operand.sign && rounded != 0U) || rounded > width_mask(integer_width)) {
            return invalid_integer_result(operand.sign, true, integer_width);
        }
        return FloatingIntegerResult{
            rounded,
            static_cast<std::uint8_t>(
                inexact ? static_cast<std::uint8_t>(FloatingExceptionFlag::Inexact) : 0U)};
    }

    const auto negative_limit = signed_minimum_bits(integer_width);
    const auto positive_limit = signed_maximum(integer_width);
    if ((operand.sign && rounded > negative_limit) ||
        (!operand.sign && rounded > positive_limit)) {
        return invalid_integer_result(operand.sign, false, integer_width);
    }
    const auto mask = width_mask(integer_width);
    const auto result = operand.sign ? ((~rounded) + 1U) & mask : rounded;
    return FloatingIntegerResult{
        result,
        static_cast<std::uint8_t>(
            inexact ? static_cast<std::uint8_t>(FloatingExceptionFlag::Inexact) : 0U)};
}

}  // namespace rvemu::core
