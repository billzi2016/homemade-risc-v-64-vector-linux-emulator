// 文件职责：实现 F/D 指令 rm 与动态 frm 的统一合法性解析。
// 边界：NaN boxing 与标志掩码是头文件 constexpr 规则；本文件不调用宿主浮点环境。

#include "rvemu/core/floating_state.hpp"

#include <cstdint>
#include <optional>

namespace rvemu::core {
namespace {

[[nodiscard]] std::optional<FloatingRoundingMode> decode_rounding_mode(
    std::uint8_t encoded) noexcept {
    switch (encoded) {
        case 0U:
            return FloatingRoundingMode::NearestTiesToEven;
        case 1U:
            return FloatingRoundingMode::TowardZero;
        case 2U:
            return FloatingRoundingMode::Down;
        case 3U:
            return FloatingRoundingMode::Up;
        case 4U:
            return FloatingRoundingMode::NearestTiesToMaximumMagnitude;
        default:
            return std::nullopt;
    }
}

}  // namespace

std::optional<FloatingRoundingMode> resolve_floating_rounding_mode(
    std::uint8_t instruction_rounding_mode, std::uint8_t dynamic_rounding_mode) noexcept {
    const auto encoded = static_cast<std::uint8_t>(instruction_rounding_mode & 0x7U);
    if (encoded == 0x7U) {
        return decode_rounding_mode(static_cast<std::uint8_t>(dynamic_rounding_mode & 0x7U));
    }
    return decode_rounding_mode(encoded);
}

}  // namespace rvemu::core
