// 文件职责：以无溢出的半开区间表示一个物理地址区域，并集中实现包含、偏移和重叠判断。
// 边界：本文件不拥有区域存储，也不决定访问权限或设备行为。

#pragma once

#include "rvemu/bus/physical_address.hpp"

#include <cstdint>
#include <optional>

namespace rvemu::bus {

class AddressRange final {
public:
    // size 必须非零，且 base + size 不能溢出 64 位地址空间。
    [[nodiscard]] static std::optional<AddressRange> create(
        PhysicalAddress base,
        std::uint64_t size) noexcept;

    [[nodiscard]] PhysicalAddress base() const noexcept { return base_; }
    [[nodiscard]] std::uint64_t size() const noexcept { return size_; }
    [[nodiscard]] std::uint64_t end_exclusive() const noexcept { return end_exclusive_; }
    [[nodiscard]] PhysicalAddress last_address() const noexcept {
        return PhysicalAddress{end_exclusive_ - 1U};
    }

    [[nodiscard]] bool contains(PhysicalAddress address) const noexcept;
    [[nodiscard]] bool contains(PhysicalAddress address, std::uint64_t length) const noexcept;
    [[nodiscard]] bool overlaps(const AddressRange& other) const noexcept;
    [[nodiscard]] std::uint64_t offset_of(PhysicalAddress address) const noexcept;

private:
    constexpr AddressRange(
        PhysicalAddress base,
        std::uint64_t size,
        std::uint64_t end_exclusive) noexcept
        : base_(base), size_(size), end_exclusive_(end_exclusive) {}

    PhysicalAddress base_;
    std::uint64_t size_;
    std::uint64_t end_exclusive_;
};

}  // namespace rvemu::bus
