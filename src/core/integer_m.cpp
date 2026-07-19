// 文件职责：以可移植无符号算术完整实现 RV64M 2.0 的乘法、除法和余数语义。
// 边界：实现不依赖非标准 128 位类型或宿主有符号溢出，不修改任何 CPU 架构状态。

#include "rvemu/core/integer_m.hpp"

#include "rvemu/core/decoder.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace rvemu::core {
namespace {

struct UnsignedProduct128 final {
    std::uint64_t low{0U};
    std::uint64_t high{0U};
};

// 把两个 64 位操作数拆成 32 位 limb。四个局部乘积都不会溢出 uint64_t，
// middle 的最大值小于 2^34，因此进位合成同样完全处于已定义的无符号域。
[[nodiscard]] constexpr UnsignedProduct128 multiply_unsigned_64(std::uint64_t lhs,
                                                                std::uint64_t rhs) noexcept {
    constexpr std::uint64_t limb_mask = 0xFFFF'FFFFULL;
    const auto lhs_low = lhs & limb_mask;
    const auto lhs_high = lhs >> 32U;
    const auto rhs_low = rhs & limb_mask;
    const auto rhs_high = rhs >> 32U;

    const auto low_low = lhs_low * rhs_low;
    const auto low_high = lhs_low * rhs_high;
    const auto high_low = lhs_high * rhs_low;
    const auto high_high = lhs_high * rhs_high;

    const auto middle = (low_low >> 32U) + (low_high & limb_mask) + (high_low & limb_mask);
    const auto low = (low_low & limb_mask) | (middle << 32U);
    const auto high = high_high + (low_high >> 32U) + (high_low >> 32U) + (middle >> 32U);
    return UnsignedProduct128{low, high};
}

// 对固定宽度无符号位模式执行二补码取负；无符号回绕由 C++ 标准明确定义。
template <typename UInt>
[[nodiscard]] constexpr UInt twos_complement_negate(UInt value) noexcept {
    static_assert(std::is_unsigned_v<UInt>, "二补码辅助只接受无符号整数");
    return static_cast<UInt>(~value + static_cast<UInt>(1U));
}

template <typename UInt>
struct SignedDivisionResult final {
    UInt quotient{0U};
    UInt remainder{0U};
};

// 使用无符号幅值执行向零舍入的二补码有符号除法。
// 除数为零时直接返回全一商和原被除数；最小负数除以 -1 也只发生无符号运算，
// 结果自然回到最小负数位模式，从而避免宿主 INT_MIN/-1 的未定义行为。
template <typename UInt>
[[nodiscard]] constexpr SignedDivisionResult<UInt> divide_signed(UInt dividend,
                                                                 UInt divisor) noexcept {
    static_assert(std::is_unsigned_v<UInt>, "有符号除法位模式必须由无符号类型承载");
    constexpr auto sign_bit =
        static_cast<UInt>(static_cast<UInt>(1U) << (std::numeric_limits<UInt>::digits - 1U));

    if (divisor == 0U) {
        return SignedDivisionResult<UInt>{std::numeric_limits<UInt>::max(), dividend};
    }

    const auto dividend_negative = (dividend & sign_bit) != 0U;
    const auto divisor_negative = (divisor & sign_bit) != 0U;
    const auto dividend_magnitude = dividend_negative ? twos_complement_negate(dividend) : dividend;
    const auto divisor_magnitude = divisor_negative ? twos_complement_negate(divisor) : divisor;

    auto quotient = static_cast<UInt>(dividend_magnitude / divisor_magnitude);
    auto remainder = static_cast<UInt>(dividend_magnitude % divisor_magnitude);
    if (dividend_negative != divisor_negative) {
        quotient = twos_complement_negate(quotient);
    }
    if (dividend_negative) {
        remainder = twos_complement_negate(remainder);
    }
    return SignedDivisionResult<UInt>{quotient, remainder};
}

// 所有 RV64W 结果，包括无符号除法和除零结果，都必须把 bit31 符号扩展到 XLEN。
[[nodiscard]] constexpr std::uint64_t extend_word(std::uint32_t value) noexcept {
    return sign_extend(value, 32U);
}

// RV64 的八个 funct3 均有定义；高半有符号乘法从同一个无符号 128 位乘积校正，
// 公式 high_signed = high_unsigned - (lhs<0 ? rhs : 0) - (rhs<0 ? lhs : 0)。
[[nodiscard]] std::optional<std::uint64_t> execute_doubleword(std::uint8_t function3,
                                                              std::uint64_t lhs,
                                                              std::uint64_t rhs) noexcept {
    switch (function3) {
        case 0U:
            return multiply_unsigned_64(lhs, rhs).low;  // MUL
        case 1U: {                                      // MULH
            auto high = multiply_unsigned_64(lhs, rhs).high;
            if ((lhs >> 63U) != 0U) {
                high -= rhs;
            }
            if ((rhs >> 63U) != 0U) {
                high -= lhs;
            }
            return high;
        }
        case 2U: {  // MULHSU
            auto high = multiply_unsigned_64(lhs, rhs).high;
            if ((lhs >> 63U) != 0U) {
                high -= rhs;
            }
            return high;
        }
        case 3U:
            return multiply_unsigned_64(lhs, rhs).high;  // MULHU
        case 4U:
            return divide_signed(lhs, rhs).quotient;  // DIV
        case 5U:
            return rhs == 0U ? std::numeric_limits<std::uint64_t>::max() : lhs / rhs;  // DIVU
        case 6U:
            return divide_signed(lhs, rhs).remainder;  // REM
        case 7U:
            return rhs == 0U ? lhs : lhs % rhs;  // REMU
        default:
            return std::nullopt;
    }
}

// OP-32 中只有 funct3=0、4、5、6、7 属于 M 扩展；1..3 保留并必须由 CPU 报非法指令。
[[nodiscard]] std::optional<std::uint64_t> execute_word(std::uint8_t function3,
                                                        std::uint64_t lhs,
                                                        std::uint64_t rhs) noexcept {
    const auto lhs_word = static_cast<std::uint32_t>(lhs & 0xFFFF'FFFFULL);
    const auto rhs_word = static_cast<std::uint32_t>(rhs & 0xFFFF'FFFFULL);

    switch (function3) {
        case 0U: {  // MULW
            const auto product = static_cast<std::uint64_t>(lhs_word) * rhs_word;
            return extend_word(static_cast<std::uint32_t>(product & 0xFFFF'FFFFULL));
        }
        case 4U:
            return extend_word(divide_signed(lhs_word, rhs_word).quotient);  // DIVW
        case 5U: {                                                           // DIVUW
            const auto quotient = rhs_word == 0U ? std::numeric_limits<std::uint32_t>::max()
                                                 : static_cast<std::uint32_t>(lhs_word / rhs_word);
            return extend_word(quotient);
        }
        case 6U:
            return extend_word(divide_signed(lhs_word, rhs_word).remainder);  // REMW
        case 7U: {                                                            // REMUW
            const auto remainder =
                rhs_word == 0U ? lhs_word : static_cast<std::uint32_t>(lhs_word % rhs_word);
            return extend_word(remainder);
        }
        default:
            return std::nullopt;
    }
}

}  // namespace

std::optional<std::uint64_t> execute_integer_m(std::uint8_t function3,
                                               std::uint64_t lhs,
                                               std::uint64_t rhs,
                                               bool word_operation) noexcept {
    return word_operation ? execute_word(function3, lhs, rhs)
                          : execute_doubleword(function3, lhs, rhs);
}

}  // namespace rvemu::core
