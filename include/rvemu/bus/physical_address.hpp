// 文件职责：提供强类型的 64 位来宾物理地址，阻止把偏移、长度或虚拟地址误传给物理总线。
// 边界：本文件只表达地址值与比较，不执行地址翻译、总线分发或未经检查的地址加法。

#pragma once

#include <cstdint>

namespace rvemu::bus {

class PhysicalAddress final {
   public:
    explicit constexpr PhysicalAddress(std::uint64_t value = 0U) noexcept : value_(value) {
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

   private:
    std::uint64_t value_;
};

[[nodiscard]] constexpr bool operator==(PhysicalAddress lhs, PhysicalAddress rhs) noexcept {
    return lhs.value() == rhs.value();
}

[[nodiscard]] constexpr bool operator!=(PhysicalAddress lhs, PhysicalAddress rhs) noexcept {
    return !(lhs == rhs);
}

[[nodiscard]] constexpr bool operator<(PhysicalAddress lhs, PhysicalAddress rhs) noexcept {
    return lhs.value() < rhs.value();
}

[[nodiscard]] constexpr bool operator<=(PhysicalAddress lhs, PhysicalAddress rhs) noexcept {
    return lhs.value() <= rhs.value();
}

[[nodiscard]] constexpr bool operator>(PhysicalAddress lhs, PhysicalAddress rhs) noexcept {
    return rhs < lhs;
}

[[nodiscard]] constexpr bool operator>=(PhysicalAddress lhs, PhysicalAddress rhs) noexcept {
    return rhs <= lhs;
}

}  // namespace rvemu::bus
