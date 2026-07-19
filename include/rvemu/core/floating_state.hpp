// 文件职责：定义 F/D 共用的舍入模式、异常标志和 NaN boxing 纯状态语义。
// 边界：本文件不执行浮点算术、不访问 CSR/总线，也不决定指令是否合法退休。

#pragma once

#include <cstdint>
#include <optional>

namespace rvemu::core {

enum class FloatingRoundingMode : std::uint8_t {
    NearestTiesToEven = 0U,
    TowardZero = 1U,
    Down = 2U,
    Up = 3U,
    NearestTiesToMaximumMagnitude = 4U,
};

enum class FloatingExceptionFlag : std::uint8_t {
    Inexact = 1U << 0U,
    Underflow = 1U << 1U,
    Overflow = 1U << 2U,
    DivideByZero = 1U << 3U,
    InvalidOperation = 1U << 4U,
};

inline constexpr std::uint8_t kFloatingExceptionMask = 0x1FU;
inline constexpr std::uint32_t kCanonicalSingleNan = 0x7FC0'0000U;
inline constexpr std::uint64_t kCanonicalDoubleNan = 0x7FF8'0000'0000'0000ULL;

// 解析指令 rm。0..4 直接选择模式，7 使用 frm；任何保留编码都返回空。
[[nodiscard]] std::optional<FloatingRoundingMode> resolve_floating_rounding_mode(
    std::uint8_t instruction_rounding_mode, std::uint8_t dynamic_rounding_mode) noexcept;

// fflags 只承认低五位；调用方可安全传入宿主或软件浮点后端产生的更宽标志集合。
[[nodiscard]] constexpr std::uint8_t normalize_floating_exception_flags(
    std::uint8_t flags) noexcept {
    return static_cast<std::uint8_t>(flags & kFloatingExceptionMask);
}

// 单精度写入 64 位 f 寄存器时，上 32 位必须全部置一形成合法 NaN box。
[[nodiscard]] constexpr std::uint64_t box_single_precision(std::uint32_t bits) noexcept {
    return 0xFFFF'FFFF'0000'0000ULL | static_cast<std::uint64_t>(bits);
}

// 非法 NaN box 作为 canonical quiet NaN 输入；合法 box 原样返回低 32 位。
[[nodiscard]] constexpr std::uint32_t unbox_single_precision(std::uint64_t bits) noexcept {
    return (bits >> 32U) == 0xFFFF'FFFFULL ? static_cast<std::uint32_t>(bits) : kCanonicalSingleNan;
}

}  // namespace rvemu::core
