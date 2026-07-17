// 文件职责：以固定宽度整数实现 IEEE 754 binary32/binary64 算术、比较、分类与统一舍入。
// 边界：本文件不使用宿主浮点类型或 fenv；格式/整数转换位于 soft_float_conversion.cpp。

#include "soft_float_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace rvemu::core::soft_float_detail {
namespace {

constexpr Format kSingleFormat{
    8U, 23U, 127, -126, 127,
    0x8000'0000ULL, 0x7F80'0000ULL, 0x007F'FFFFULL,
    kCanonicalSingleNan};
constexpr Format kDoubleFormat{
    11U, 52U, 1023, -1022, 1023,
    0x8000'0000'0000'0000ULL, 0x7FF0'0000'0000'0000ULL,
    0x000F'FFFF'FFFF'FFFFULL, kCanonicalDoubleNan};

[[nodiscard]] std::uint64_t exponent_all_ones(const Format& format) noexcept {
    return (1ULL << format.exponent_bits) - 1U;
}

[[nodiscard]] bool should_increment(
    FloatingRoundingMode rounding,
    bool sign,
    bool halfway_bit,
    bool lower_bits,
    bool retained_odd) noexcept {
    const auto discarded = halfway_bit || lower_bits;
    switch (rounding) {
    case FloatingRoundingMode::NearestTiesToEven:
        return halfway_bit && (lower_bits || retained_odd);
    case FloatingRoundingMode::TowardZero:
        return false;
    case FloatingRoundingMode::Down:
        return sign && discarded;
    case FloatingRoundingMode::Up:
        return !sign && discarded;
    case FloatingRoundingMode::NearestTiesToMaximumMagnitude:
        return halfway_bit;
    }
    return false;
}

// 丢弃低位前先提取 half/sticky；加一发生在截断后，因此不会把被丢弃区域的进位重复计算。
[[nodiscard]] WideUnsigned rounded_right_shift(
    WideUnsigned value,
    std::size_t shift,
    bool sign,
    FloatingRoundingMode rounding,
    bool& inexact) noexcept {
    if (shift == 0U) {
        return value;
    }
    const auto halfway = value.bit(shift - 1U);
    const auto lower = value.any_low_bits(shift - 1U);
    inexact = halfway || lower;
    value.shift_right(shift);
    if (should_increment(rounding, sign, halfway, lower, value.bit(0U))) {
        value.add(WideUnsigned{1U});
    }
    return value;
}

[[nodiscard]] bool overflow_to_infinity(
    FloatingRoundingMode rounding,
    bool sign) noexcept {
    return rounding == FloatingRoundingMode::NearestTiesToEven ||
           rounding == FloatingRoundingMode::NearestTiesToMaximumMagnitude ||
           (rounding == FloatingRoundingMode::Up && !sign) ||
           (rounding == FloatingRoundingMode::Down && sign);
}

[[nodiscard]] FloatingResult invalid_result(const Format& format) noexcept {
    return FloatingResult{format.canonical_nan, invalid_flag()};
}

[[nodiscard]] bool numeric_less(
    const Format& format,
    std::uint64_t lhs_bits,
    std::uint64_t rhs_bits) noexcept {
    const auto lhs_magnitude = lhs_bits & ~format.sign_mask;
    const auto rhs_magnitude = rhs_bits & ~format.sign_mask;
    if (lhs_magnitude == 0U && rhs_magnitude == 0U) {
        return false;
    }
    const auto lhs_sign = (lhs_bits & format.sign_mask) != 0U;
    const auto rhs_sign = (rhs_bits & format.sign_mask) != 0U;
    if (lhs_sign != rhs_sign) {
        return lhs_sign;
    }
    return lhs_sign ? lhs_magnitude > rhs_magnitude : lhs_magnitude < rhs_magnitude;
}

[[nodiscard]] FloatingResult add_unpacked(
    const Format& format,
    Unpacked lhs,
    Unpacked rhs,
    FloatingRoundingMode rounding) noexcept {
    if (is_nan(lhs.category) || is_nan(rhs.category)) {
        const auto signaling = lhs.category == Category::SignalingNan ||
                               rhs.category == Category::SignalingNan;
        return FloatingResult{
            format.canonical_nan,
            static_cast<std::uint8_t>(signaling ? invalid_flag() : 0U)};
    }
    if (lhs.category == Category::Infinity || rhs.category == Category::Infinity) {
        if (lhs.category == Category::Infinity && rhs.category == Category::Infinity &&
            lhs.sign != rhs.sign) {
            return invalid_result(format);
        }
        const auto& infinite = lhs.category == Category::Infinity ? lhs : rhs;
        return FloatingResult{infinity(format, infinite.sign), 0U};
    }
    if (lhs.category == Category::Zero && rhs.category == Category::Zero) {
        const auto sign = lhs.sign == rhs.sign ? lhs.sign :
                          rounding == FloatingRoundingMode::Down;
        return FloatingResult{signed_zero(format, sign), 0U};
    }
    if (lhs.category == Category::Zero) {
        return round_and_pack(format, rhs.sign, WideUnsigned{rhs.significand}, rhs.exponent, rounding);
    }
    if (rhs.category == Category::Zero) {
        return round_and_pack(format, lhs.sign, WideUnsigned{lhs.significand}, lhs.exponent, rounding);
    }

    const auto common_exponent = std::min(lhs.exponent, rhs.exponent);
    WideUnsigned left{lhs.significand};
    WideUnsigned right{rhs.significand};
    left.shift_left(static_cast<std::size_t>(lhs.exponent - common_exponent));
    right.shift_left(static_cast<std::size_t>(rhs.exponent - common_exponent));

    bool sign = lhs.sign;
    if (lhs.sign == rhs.sign) {
        left.add(right);
    } else {
        const auto ordering = left.compare(right);
        if (ordering == 0) {
            return FloatingResult{
                signed_zero(format, rounding == FloatingRoundingMode::Down), 0U};
        }
        if (ordering > 0) {
            left.subtract(right);
        } else {
            right.subtract(left);
            left = right;
            sign = rhs.sign;
        }
    }
    return round_and_pack(format, sign, left, common_exponent, rounding);
}

}  // namespace

bool WideUnsigned::zero() const noexcept {
    for (const auto limb : limbs_) {
        if (limb != 0U) {
            return false;
        }
    }
    return true;
}

std::size_t WideUnsigned::bit_length() const noexcept {
    for (std::size_t index = kWideLimbs; index > 0U; --index) {
        const auto value = limbs_[index - 1U];
        if (value == 0U) {
            continue;
        }
        std::size_t bits = 0U;
        auto remaining = value;
        while (remaining != 0U) {
            ++bits;
            remaining >>= 1U;
        }
        return (index - 1U) * 64U + bits;
    }
    return 0U;
}

bool WideUnsigned::bit(std::size_t index) const noexcept {
    return index < kWideLimbs * 64U &&
           ((limbs_[index / 64U] >> (index % 64U)) & 1U) != 0U;
}

void WideUnsigned::set_bit(std::size_t index) noexcept {
    if (index < kWideLimbs * 64U) {
        limbs_[index / 64U] |= 1ULL << (index % 64U);
    }
}

void WideUnsigned::shift_left(std::size_t amount) noexcept {
    if (amount == 0U) {
        return;
    }
    if (amount >= kWideLimbs * 64U) {
        limbs_.fill(0U);
        return;
    }
    const auto words = amount / 64U;
    const auto bits = amount % 64U;
    for (std::size_t destination = kWideLimbs; destination > 0U; --destination) {
        const auto index = destination - 1U;
        std::uint64_t value = 0U;
        if (index >= words) {
            value = limbs_[index - words] << bits;
            if (bits != 0U && index > words) {
                value |= limbs_[index - words - 1U] >> (64U - bits);
            }
        }
        limbs_[index] = value;
    }
}

void WideUnsigned::shift_right(std::size_t amount) noexcept {
    if (amount == 0U) {
        return;
    }
    if (amount >= kWideLimbs * 64U) {
        limbs_.fill(0U);
        return;
    }
    const auto words = amount / 64U;
    const auto bits = amount % 64U;
    for (std::size_t index = 0U; index < kWideLimbs; ++index) {
        std::uint64_t value = 0U;
        if (index + words < kWideLimbs) {
            value = limbs_[index + words] >> bits;
            if (bits != 0U && index + words + 1U < kWideLimbs) {
                value |= limbs_[index + words + 1U] << (64U - bits);
            }
        }
        limbs_[index] = value;
    }
}

bool WideUnsigned::any_low_bits(std::size_t count) const noexcept {
    const auto bounded = std::min(count, kWideLimbs * 64U);
    const auto whole_words = bounded / 64U;
    for (std::size_t index = 0U; index < whole_words; ++index) {
        if (limbs_[index] != 0U) {
            return true;
        }
    }
    const auto remainder = bounded % 64U;
    if (remainder != 0U && whole_words < kWideLimbs) {
        const auto mask = (1ULL << remainder) - 1U;
        return (limbs_[whole_words] & mask) != 0U;
    }
    return false;
}

int WideUnsigned::compare(const WideUnsigned& other) const noexcept {
    for (std::size_t index = kWideLimbs; index > 0U; --index) {
        if (limbs_[index - 1U] != other.limbs_[index - 1U]) {
            return limbs_[index - 1U] < other.limbs_[index - 1U] ? -1 : 1;
        }
    }
    return 0;
}

void WideUnsigned::add(const WideUnsigned& other) noexcept {
    std::uint64_t carry = 0U;
    for (std::size_t index = 0U; index < kWideLimbs; ++index) {
        const auto first = limbs_[index] + other.limbs_[index];
        const auto carry_first = first < limbs_[index];
        const auto result = first + carry;
        const auto carry_second = result < first;
        limbs_[index] = result;
        carry = (carry_first || carry_second) ? 1U : 0U;
    }
}

void WideUnsigned::subtract(const WideUnsigned& other) noexcept {
    std::uint64_t borrow = 0U;
    for (std::size_t index = 0U; index < kWideLimbs; ++index) {
        const auto subtrahend = other.limbs_[index] + borrow;
        const auto wrapped = subtrahend < other.limbs_[index];
        const auto next_borrow = wrapped || limbs_[index] < subtrahend;
        limbs_[index] -= subtrahend;
        borrow = next_borrow ? 1U : 0U;
    }
}

const Format& format_of(FloatingFormat format) noexcept {
    return format == FloatingFormat::Single ? kSingleFormat : kDoubleFormat;
}

Unpacked unpack(FloatingFormat floating_format, std::uint64_t bits) noexcept {
    const auto& format = format_of(floating_format);
    const auto sign = (bits & format.sign_mask) != 0U;
    const auto exponent_field = (bits & format.exponent_mask) >> format.fraction_bits;
    const auto fraction = bits & format.fraction_mask;
    if (exponent_field == exponent_all_ones(format)) {
        if (fraction == 0U) {
            return Unpacked{sign, Category::Infinity, 0U, 0};
        }
        const auto quiet_bit = 1ULL << (format.fraction_bits - 1U);
        return Unpacked{
            sign,
            (fraction & quiet_bit) != 0U ? Category::QuietNan : Category::SignalingNan,
            0U,
            0};
    }
    if (exponent_field == 0U) {
        if (fraction == 0U) {
            return Unpacked{sign, Category::Zero, 0U, 0};
        }
        return Unpacked{
            sign,
            Category::Finite,
            fraction,
            static_cast<std::int32_t>(format.minimum_exponent) - format.fraction_bits};
    }
    return Unpacked{
        sign,
        Category::Finite,
        (1ULL << format.fraction_bits) | fraction,
        static_cast<std::int32_t>(exponent_field) - format.bias - format.fraction_bits};
}

bool is_nan(Category category) noexcept {
    return category == Category::QuietNan || category == Category::SignalingNan;
}

std::uint8_t invalid_flag() noexcept {
    return static_cast<std::uint8_t>(FloatingExceptionFlag::InvalidOperation);
}

std::uint64_t signed_zero(const Format& format, bool sign) noexcept {
    return sign ? format.sign_mask : 0U;
}

std::uint64_t infinity(const Format& format, bool sign) noexcept {
    return (sign ? format.sign_mask : 0U) | format.exponent_mask;
}

FloatingResult round_and_pack(
    const Format& format,
    bool sign,
    WideUnsigned magnitude,
    std::int32_t exponent,
    FloatingRoundingMode rounding) noexcept {
    if (magnitude.zero()) {
        return FloatingResult{signed_zero(format, sign), 0U};
    }

    const auto precision = static_cast<std::size_t>(format.fraction_bits) + 1U;
    auto bit_length = magnitude.bit_length();
    auto result_exponent = exponent + static_cast<std::int32_t>(bit_length) - 1;
    bool inexact = false;

    if (result_exponent >= format.minimum_exponent) {
        if (bit_length > precision) {
            magnitude = rounded_right_shift(
                magnitude, bit_length - precision, sign, rounding, inexact);
        } else if (bit_length < precision) {
            magnitude.shift_left(precision - bit_length);
        }
        if (magnitude.bit_length() > precision) {
            magnitude.shift_right(1U);
            ++result_exponent;
        }
        if (result_exponent > format.maximum_exponent) {
            const auto flags = static_cast<std::uint8_t>(
                static_cast<std::uint8_t>(FloatingExceptionFlag::Overflow) |
                static_cast<std::uint8_t>(FloatingExceptionFlag::Inexact));
            if (overflow_to_infinity(rounding, sign)) {
                return FloatingResult{infinity(format, sign), flags};
            }
            const auto maximum_finite =
                ((exponent_all_ones(format) - 1U) << format.fraction_bits) |
                format.fraction_mask;
            return FloatingResult{(sign ? format.sign_mask : 0U) | maximum_finite, flags};
        }
        const auto exponent_field = static_cast<std::uint64_t>(result_exponent + format.bias);
        const auto fraction = magnitude.low64() & format.fraction_mask;
        const auto flags = static_cast<std::uint8_t>(inexact ?
            static_cast<std::uint8_t>(FloatingExceptionFlag::Inexact) : 0U);
        return FloatingResult{
            (sign ? format.sign_mask : 0U) |
                (exponent_field << format.fraction_bits) | fraction,
            flags};
    }

    // 次正规数的量化单位固定为 2^(emin-fractionBits)；舍入后跨过边界即成为最小正规数。
    const auto unit_exponent = static_cast<std::int32_t>(format.minimum_exponent) -
                               format.fraction_bits;
    if (exponent < unit_exponent) {
        magnitude = rounded_right_shift(
            magnitude,
            static_cast<std::size_t>(unit_exponent - exponent),
            sign,
            rounding,
            inexact);
    } else if (exponent > unit_exponent) {
        magnitude.shift_left(static_cast<std::size_t>(exponent - unit_exponent));
    }
    const auto minimum_normal = 1ULL << format.fraction_bits;
    const auto encoded_magnitude = magnitude.low64();
    std::uint64_t bits = sign ? format.sign_mask : 0U;
    if (encoded_magnitude >= minimum_normal) {
        bits |= 1ULL << format.fraction_bits;
    } else {
        bits |= encoded_magnitude;
    }
    std::uint8_t flags = 0U;
    if (inexact) {
        flags |= static_cast<std::uint8_t>(FloatingExceptionFlag::Inexact);
        if (encoded_magnitude < minimum_normal) {
            flags |= static_cast<std::uint8_t>(FloatingExceptionFlag::Underflow);
        }
    }
    return FloatingResult{bits, flags};
}

WideUnsigned round_to_integer_magnitude(
    WideUnsigned magnitude,
    std::int32_t exponent,
    bool sign,
    FloatingRoundingMode rounding,
    bool& inexact) noexcept {
    inexact = false;
    if (exponent >= 0) {
        magnitude.shift_left(static_cast<std::size_t>(exponent));
        return magnitude;
    }
    return rounded_right_shift(
        magnitude, static_cast<std::size_t>(-exponent), sign, rounding, inexact);
}

WideUnsigned multiply_significands(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    WideUnsigned result{};
    WideUnsigned shifted{lhs};
    while (rhs != 0U) {
        if ((rhs & 1U) != 0U) {
            result.add(shifted);
        }
        rhs >>= 1U;
        shifted.shift_left(1U);
    }
    return result;
}

WideUnsigned divide_significands(
    std::uint64_t numerator,
    std::uint64_t denominator,
    std::size_t precision,
    bool& inexact) noexcept {
    WideUnsigned dividend{numerator};
    dividend.shift_left(precision);
    WideUnsigned quotient{};
    std::uint64_t remainder = 0U;
    for (std::size_t index = dividend.bit_length(); index > 0U; --index) {
        remainder = (remainder << 1U) | (dividend.bit(index - 1U) ? 1U : 0U);
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient.set_bit(index - 1U);
        }
    }
    inexact = remainder != 0U;
    return quotient;
}

WideUnsigned square_root_significand(
    std::uint64_t significand,
    std::size_t shift_pairs,
    bool& inexact) noexcept {
    WideUnsigned radicand{significand};
    radicand.shift_left(shift_pairs * 2U);
    WideUnsigned root{};
    WideUnsigned remainder{};
    const auto pairs = (radicand.bit_length() + 1U) / 2U;
    for (std::size_t pair = pairs; pair > 0U; --pair) {
        remainder.shift_left(2U);
        const auto low_bit = (pair - 1U) * 2U;
        if (radicand.bit(low_bit)) {
            remainder.set_bit(0U);
        }
        if (radicand.bit(low_bit + 1U)) {
            remainder.set_bit(1U);
        }
        root.shift_left(1U);
        auto trial = root;
        trial.shift_left(1U);
        trial.add(WideUnsigned{1U});
        if (remainder.compare(trial) >= 0) {
            remainder.subtract(trial);
            root.add(WideUnsigned{1U});
        }
    }
    inexact = !remainder.zero();
    return root;
}

}  // namespace rvemu::core::soft_float_detail

namespace rvemu::core {
namespace {

using soft_float_detail::Category;
using soft_float_detail::WideUnsigned;

[[nodiscard]] FloatingResult multiply_impl(
    FloatingFormat format_name,
    std::uint64_t lhs_bits,
    std::uint64_t rhs_bits,
    FloatingRoundingMode rounding) noexcept {
    const auto& format = soft_float_detail::format_of(format_name);
    const auto lhs = soft_float_detail::unpack(format_name, lhs_bits);
    const auto rhs = soft_float_detail::unpack(format_name, rhs_bits);
    if (soft_float_detail::is_nan(lhs.category) || soft_float_detail::is_nan(rhs.category)) {
        const auto signaling = lhs.category == Category::SignalingNan ||
                               rhs.category == Category::SignalingNan;
        return FloatingResult{
            format.canonical_nan,
            static_cast<std::uint8_t>(
                signaling ? soft_float_detail::invalid_flag() : 0U)};
    }
    const auto sign = lhs.sign != rhs.sign;
    const auto zero = lhs.category == Category::Zero || rhs.category == Category::Zero;
    const auto infinite = lhs.category == Category::Infinity || rhs.category == Category::Infinity;
    if (zero && infinite) {
        return FloatingResult{format.canonical_nan, soft_float_detail::invalid_flag()};
    }
    if (infinite) {
        return FloatingResult{soft_float_detail::infinity(format, sign), 0U};
    }
    if (zero) {
        return FloatingResult{soft_float_detail::signed_zero(format, sign), 0U};
    }
    return soft_float_detail::round_and_pack(
        format,
        sign,
        soft_float_detail::multiply_significands(lhs.significand, rhs.significand),
        lhs.exponent + rhs.exponent,
        rounding);
}

}  // namespace

FloatingResult floating_add(
    FloatingFormat format,
    std::uint64_t lhs,
    std::uint64_t rhs,
    FloatingRoundingMode rounding) noexcept {
    return soft_float_detail::add_unpacked(
        soft_float_detail::format_of(format),
        soft_float_detail::unpack(format, lhs),
        soft_float_detail::unpack(format, rhs),
        rounding);
}

FloatingResult floating_subtract(
    FloatingFormat format,
    std::uint64_t lhs,
    std::uint64_t rhs,
    FloatingRoundingMode rounding) noexcept {
    const auto sign_mask = soft_float_detail::format_of(format).sign_mask;
    return floating_add(format, lhs, rhs ^ sign_mask, rounding);
}

FloatingResult floating_multiply(
    FloatingFormat format,
    std::uint64_t lhs,
    std::uint64_t rhs,
    FloatingRoundingMode rounding) noexcept {
    return multiply_impl(format, lhs, rhs, rounding);
}

FloatingResult floating_divide(
    FloatingFormat format_name,
    std::uint64_t lhs_bits,
    std::uint64_t rhs_bits,
    FloatingRoundingMode rounding) noexcept {
    const auto& format = soft_float_detail::format_of(format_name);
    const auto lhs = soft_float_detail::unpack(format_name, lhs_bits);
    const auto rhs = soft_float_detail::unpack(format_name, rhs_bits);
    if (soft_float_detail::is_nan(lhs.category) || soft_float_detail::is_nan(rhs.category)) {
        const auto signaling = lhs.category == Category::SignalingNan ||
                               rhs.category == Category::SignalingNan;
        return FloatingResult{
            format.canonical_nan,
            static_cast<std::uint8_t>(
                signaling ? soft_float_detail::invalid_flag() : 0U)};
    }
    const auto sign = lhs.sign != rhs.sign;
    if ((lhs.category == Category::Zero && rhs.category == Category::Zero) ||
        (lhs.category == Category::Infinity && rhs.category == Category::Infinity)) {
        return FloatingResult{format.canonical_nan, soft_float_detail::invalid_flag()};
    }
    if (lhs.category == Category::Infinity) {
        return FloatingResult{soft_float_detail::infinity(format, sign), 0U};
    }
    if (rhs.category == Category::Infinity) {
        return FloatingResult{soft_float_detail::signed_zero(format, sign), 0U};
    }
    if (rhs.category == Category::Zero) {
        return FloatingResult{
            soft_float_detail::infinity(format, sign),
            static_cast<std::uint8_t>(FloatingExceptionFlag::DivideByZero)};
    }
    if (lhs.category == Category::Zero) {
        return FloatingResult{soft_float_detail::signed_zero(format, sign), 0U};
    }

    const auto precision = static_cast<std::size_t>(format.fraction_bits) + 5U;
    bool remainder = false;
    auto quotient = soft_float_detail::divide_significands(
        lhs.significand, rhs.significand, precision, remainder);
    // 额外最低位只表示无限尾部是否非零，不冒充已计算的商位。
    quotient.shift_left(1U);
    if (remainder) {
        quotient.set_bit(0U);
    }
    return soft_float_detail::round_and_pack(
        format,
        sign,
        quotient,
        lhs.exponent - rhs.exponent - static_cast<std::int32_t>(precision) - 1,
        rounding);
}

FloatingResult floating_square_root(
    FloatingFormat format_name,
    std::uint64_t operand_bits,
    FloatingRoundingMode rounding) noexcept {
    const auto& format = soft_float_detail::format_of(format_name);
    auto operand = soft_float_detail::unpack(format_name, operand_bits);
    if (soft_float_detail::is_nan(operand.category)) {
        return FloatingResult{
            format.canonical_nan,
            static_cast<std::uint8_t>(
                operand.category == Category::SignalingNan ?
                    soft_float_detail::invalid_flag() : 0U)};
    }
    if (operand.category == Category::Zero) {
        return FloatingResult{soft_float_detail::signed_zero(format, operand.sign), 0U};
    }
    if (operand.sign) {
        return FloatingResult{format.canonical_nan, soft_float_detail::invalid_flag()};
    }
    if (operand.category == Category::Infinity) {
        return FloatingResult{soft_float_detail::infinity(format, false), 0U};
    }
    if ((operand.exponent & 1) != 0) {
        operand.significand <<= 1U;
        --operand.exponent;
    }
    const auto precision = static_cast<std::size_t>(format.fraction_bits) + 5U;
    const auto source_bits = WideUnsigned{operand.significand}.bit_length();
    const auto source_root_bits = (source_bits + 1U) / 2U;
    const auto shift_pairs = precision > source_root_bits ? precision - source_root_bits : 0U;
    bool remainder = false;
    auto root = soft_float_detail::square_root_significand(
        operand.significand, shift_pairs, remainder);
    root.shift_left(1U);
    if (remainder) {
        root.set_bit(0U);
    }
    return soft_float_detail::round_and_pack(
        format,
        false,
        root,
        operand.exponent / 2 - static_cast<std::int32_t>(shift_pairs) - 1,
        rounding);
}

FloatingResult floating_fused_multiply_add(
    FloatingFormat format_name,
    std::uint64_t multiplicand_bits,
    std::uint64_t multiplier_bits,
    std::uint64_t addend_bits,
    bool negate_product,
    bool negate_addend,
    FloatingRoundingMode rounding) noexcept {
    const auto& format = soft_float_detail::format_of(format_name);
    auto lhs = soft_float_detail::unpack(format_name, multiplicand_bits);
    auto rhs = soft_float_detail::unpack(format_name, multiplier_bits);
    auto addend = soft_float_detail::unpack(format_name, addend_bits);
    const auto invalid_product =
        (lhs.category == Category::Infinity && rhs.category == Category::Zero) ||
        (rhs.category == Category::Infinity && lhs.category == Category::Zero);
    if (invalid_product) {
        return FloatingResult{format.canonical_nan, soft_float_detail::invalid_flag()};
    }
    if (soft_float_detail::is_nan(lhs.category) || soft_float_detail::is_nan(rhs.category) ||
        soft_float_detail::is_nan(addend.category)) {
        const auto signaling = lhs.category == Category::SignalingNan ||
                               rhs.category == Category::SignalingNan ||
                               addend.category == Category::SignalingNan;
        return FloatingResult{
            format.canonical_nan,
            static_cast<std::uint8_t>(
                signaling ? soft_float_detail::invalid_flag() : 0U)};
    }

    auto product_sign = (lhs.sign != rhs.sign) != negate_product;
    addend.sign = addend.sign != negate_addend;
    const auto product_infinite = lhs.category == Category::Infinity ||
                                  rhs.category == Category::Infinity;
    if (product_infinite) {
        if (addend.category == Category::Infinity && product_sign != addend.sign) {
            return FloatingResult{format.canonical_nan, soft_float_detail::invalid_flag()};
        }
        return FloatingResult{soft_float_detail::infinity(format, product_sign), 0U};
    }
    if (addend.category == Category::Infinity) {
        return FloatingResult{soft_float_detail::infinity(format, addend.sign), 0U};
    }

    const auto product_zero = lhs.category == Category::Zero || rhs.category == Category::Zero;
    if (product_zero && addend.category == Category::Zero) {
        const auto sign = product_sign == addend.sign ? product_sign :
                          rounding == FloatingRoundingMode::Down;
        return FloatingResult{soft_float_detail::signed_zero(format, sign), 0U};
    }
    if (product_zero) {
        return soft_float_detail::round_and_pack(
            format, addend.sign, WideUnsigned{addend.significand}, addend.exponent, rounding);
    }

    auto product = soft_float_detail::multiply_significands(lhs.significand, rhs.significand);
    auto exponent = lhs.exponent + rhs.exponent;
    if (addend.category == Category::Zero) {
        return soft_float_detail::round_and_pack(format, product_sign, product, exponent, rounding);
    }
    auto addend_magnitude = WideUnsigned{addend.significand};
    const auto common_exponent = std::min(exponent, addend.exponent);
    product.shift_left(static_cast<std::size_t>(exponent - common_exponent));
    addend_magnitude.shift_left(static_cast<std::size_t>(addend.exponent - common_exponent));
    bool result_sign = product_sign;
    if (product_sign == addend.sign) {
        product.add(addend_magnitude);
    } else {
        const auto ordering = product.compare(addend_magnitude);
        if (ordering == 0) {
            return FloatingResult{
                soft_float_detail::signed_zero(
                    format, rounding == FloatingRoundingMode::Down),
                0U};
        }
        if (ordering > 0) {
            product.subtract(addend_magnitude);
        } else {
            addend_magnitude.subtract(product);
            product = addend_magnitude;
            result_sign = addend.sign;
        }
    }
    return soft_float_detail::round_and_pack(
        format, result_sign, product, common_exponent, rounding);
}

std::uint64_t floating_sign_inject(
    FloatingFormat format_name,
    std::uint64_t magnitude,
    std::uint64_t sign_source,
    std::uint8_t operation) noexcept {
    const auto sign_mask = soft_float_detail::format_of(format_name).sign_mask;
    const auto first_sign = magnitude & sign_mask;
    const auto second_sign = sign_source & sign_mask;
    std::uint64_t result_sign = second_sign;
    if (operation == 1U) {
        result_sign ^= sign_mask;
    } else if (operation == 2U) {
        result_sign = first_sign ^ second_sign;
    }
    return (magnitude & ~sign_mask) | result_sign;
}

FloatingResult floating_minimum_maximum(
    FloatingFormat format_name,
    std::uint64_t lhs_bits,
    std::uint64_t rhs_bits,
    bool maximum) noexcept {
    const auto& format = soft_float_detail::format_of(format_name);
    const auto lhs = soft_float_detail::unpack(format_name, lhs_bits);
    const auto rhs = soft_float_detail::unpack(format_name, rhs_bits);
    const auto signaling = lhs.category == Category::SignalingNan ||
                           rhs.category == Category::SignalingNan;
    const auto lhs_nan = soft_float_detail::is_nan(lhs.category);
    const auto rhs_nan = soft_float_detail::is_nan(rhs.category);
    const auto flags = static_cast<std::uint8_t>(
        signaling ? soft_float_detail::invalid_flag() : 0U);
    if (lhs_nan && rhs_nan) {
        return FloatingResult{format.canonical_nan, flags};
    }
    if (lhs_nan) {
        return FloatingResult{rhs_bits, flags};
    }
    if (rhs_nan) {
        return FloatingResult{lhs_bits, flags};
    }
    const auto less = soft_float_detail::numeric_less(format, lhs_bits, rhs_bits);
    const auto equal = !less &&
                       !soft_float_detail::numeric_less(format, rhs_bits, lhs_bits);
    if (equal) {
        if ((lhs_bits & ~format.sign_mask) == 0U &&
            (rhs_bits & ~format.sign_mask) == 0U) {
            return FloatingResult{
                maximum ? (lhs_bits & rhs_bits) : (lhs_bits | rhs_bits), 0U};
        }
        return FloatingResult{lhs_bits, 0U};
    }
    return FloatingResult{maximum ? (less ? rhs_bits : lhs_bits) :
                                    (less ? lhs_bits : rhs_bits), 0U};
}

FloatingComparisonResult floating_compare(
    FloatingFormat format_name,
    std::uint64_t lhs_bits,
    std::uint64_t rhs_bits,
    FloatingComparison comparison) noexcept {
    const auto& format = soft_float_detail::format_of(format_name);
    const auto lhs = soft_float_detail::unpack(format_name, lhs_bits);
    const auto rhs = soft_float_detail::unpack(format_name, rhs_bits);
    if (soft_float_detail::is_nan(lhs.category) || soft_float_detail::is_nan(rhs.category)) {
        const auto signaling_comparison = comparison != FloatingComparison::Equal;
        const auto signaling_nan = lhs.category == Category::SignalingNan ||
                                   rhs.category == Category::SignalingNan;
        return FloatingComparisonResult{
            false,
            static_cast<std::uint8_t>(
                (signaling_comparison || signaling_nan) ?
                    soft_float_detail::invalid_flag() : 0U)};
    }
    const auto less = soft_float_detail::numeric_less(format, lhs_bits, rhs_bits);
    const auto equal = !less &&
                       !soft_float_detail::numeric_less(format, rhs_bits, lhs_bits);
    if (comparison == FloatingComparison::Equal) {
        return FloatingComparisonResult{equal, 0U};
    }
    if (comparison == FloatingComparison::LessThan) {
        return FloatingComparisonResult{less, 0U};
    }
    return FloatingComparisonResult{less || equal, 0U};
}

std::uint16_t floating_classify(
    FloatingFormat format_name,
    std::uint64_t operand_bits) noexcept {
    const auto operand = soft_float_detail::unpack(format_name, operand_bits);
    if (operand.category == Category::Infinity) {
        return static_cast<std::uint16_t>(1U << (operand.sign ? 0U : 7U));
    }
    if (operand.category == Category::Zero) {
        return static_cast<std::uint16_t>(1U << (operand.sign ? 3U : 4U));
    }
    if (operand.category == Category::SignalingNan) {
        return 1U << 8U;
    }
    if (operand.category == Category::QuietNan) {
        return 1U << 9U;
    }
    const auto& format = soft_float_detail::format_of(format_name);
    const auto exponent_field = (operand_bits & format.exponent_mask) >> format.fraction_bits;
    const auto subnormal = exponent_field == 0U;
    if (operand.sign) {
        return static_cast<std::uint16_t>(1U << (subnormal ? 2U : 1U));
    }
    return static_cast<std::uint16_t>(1U << (subnormal ? 5U : 6U));
}

}  // namespace rvemu::core
