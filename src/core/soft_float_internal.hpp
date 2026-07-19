// 文件职责：提供软件浮点实现私有的格式解析、固定宽度大整数和统一装箱声明。
// 边界：该头文件只供 src/core 内部使用，不形成公共 API，也不访问任何架构状态。

#pragma once

#include "rvemu/core/soft_float.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace rvemu::core::soft_float_detail {

// 72 个 64 位 limb 足以覆盖 binary64 FMA 中最大指数差，并保留完整精确尾数。
constexpr std::size_t kWideLimbs = 72U;

class WideUnsigned final {
   public:
    constexpr WideUnsigned() noexcept = default;
    explicit constexpr WideUnsigned(std::uint64_t value) noexcept {
        limbs_[0] = value;
    }

    [[nodiscard]] bool zero() const noexcept;
    [[nodiscard]] std::size_t bit_length() const noexcept;
    [[nodiscard]] bool bit(std::size_t index) const noexcept;
    void set_bit(std::size_t index) noexcept;
    void shift_left(std::size_t amount) noexcept;
    void shift_right(std::size_t amount) noexcept;
    [[nodiscard]] bool any_low_bits(std::size_t count) const noexcept;
    [[nodiscard]] int compare(const WideUnsigned& other) const noexcept;
    void add(const WideUnsigned& other) noexcept;
    void subtract(const WideUnsigned& other) noexcept;
    [[nodiscard]] std::uint64_t low64() const noexcept {
        return limbs_[0];
    }

   private:
    std::array<std::uint64_t, kWideLimbs> limbs_{};
};

struct Format final {
    std::uint8_t exponent_bits{0U};
    std::uint8_t fraction_bits{0U};
    std::int16_t bias{0};
    std::int16_t minimum_exponent{0};
    std::int16_t maximum_exponent{0};
    std::uint64_t sign_mask{0U};
    std::uint64_t exponent_mask{0U};
    std::uint64_t fraction_mask{0U};
    std::uint64_t canonical_nan{0U};
};

enum class Category : std::uint8_t {
    Zero,
    Finite,
    Infinity,
    QuietNan,
    SignalingNan,
};

struct Unpacked final {
    bool sign{false};
    Category category{Category::Zero};
    std::uint64_t significand{0U};
    std::int32_t exponent{0};
};

[[nodiscard]] const Format& format_of(FloatingFormat format) noexcept;
[[nodiscard]] Unpacked unpack(FloatingFormat format, std::uint64_t bits) noexcept;
[[nodiscard]] bool is_nan(Category category) noexcept;
[[nodiscard]] std::uint8_t invalid_flag() noexcept;
[[nodiscard]] std::uint64_t signed_zero(const Format& format, bool sign) noexcept;
[[nodiscard]] std::uint64_t infinity(const Format& format, bool sign) noexcept;

// magnitude * 2^exponent 只在此入口舍入并编码，保证全部算术共享相同 flags 规则。
[[nodiscard]] FloatingResult round_and_pack(const Format& format,
                                            bool sign,
                                            WideUnsigned magnitude,
                                            std::int32_t exponent,
                                            FloatingRoundingMode rounding) noexcept;

// 浮点转整数使用同一 half/sticky 与五舍入模式判定，避免另建一套舍入逻辑。
[[nodiscard]] WideUnsigned round_to_integer_magnitude(WideUnsigned magnitude,
                                                      std::int32_t exponent,
                                                      bool sign,
                                                      FloatingRoundingMode rounding,
                                                      bool& inexact) noexcept;

[[nodiscard]] WideUnsigned multiply_significands(std::uint64_t lhs, std::uint64_t rhs) noexcept;
[[nodiscard]] WideUnsigned divide_significands(std::uint64_t numerator,
                                               std::uint64_t denominator,
                                               std::size_t precision,
                                               bool& inexact) noexcept;
[[nodiscard]] WideUnsigned square_root_significand(std::uint64_t significand,
                                                   std::size_t shift_pairs,
                                                   bool& inexact) noexcept;

}  // namespace rvemu::core::soft_float_detail
