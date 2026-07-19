// 文件职责：实现物理地址半开区间的无溢出构造、包含、偏移和重叠判断。
// 边界：本文件不访问来宾内存，也不推断任何设备权限或访问副作用。

#include "rvemu/bus/address_range.hpp"

#include <limits>

namespace rvemu::bus {

std::optional<AddressRange> AddressRange::create(PhysicalAddress base,
                                                 std::uint64_t size) noexcept {
    if (size == 0U) {
        return std::nullopt;
    }

    // end_exclusive 必须能被 uint64_t 表示；因此覆盖 UINT64_MAX 本身的区间不合法。
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (base.value() > maximum - size) {
        return std::nullopt;
    }

    return AddressRange{base, size, base.value() + size};
}

bool AddressRange::contains(PhysicalAddress address) const noexcept {
    return address.value() >= base_.value() && address.value() < end_exclusive_;
}

bool AddressRange::contains(PhysicalAddress address, std::uint64_t length) const noexcept {
    if (length == 0U || !contains(address)) {
        return false;
    }

    // 先用减法计算剩余容量，避免 address + length 自身发生溢出。
    return length <= end_exclusive_ - address.value();
}

bool AddressRange::overlaps(const AddressRange& other) const noexcept {
    return base_.value() < other.end_exclusive_ && other.base_.value() < end_exclusive_;
}

std::uint64_t AddressRange::offset_of(PhysicalAddress address) const noexcept {
    return address.value() - base_.value();
}

}  // namespace rvemu::bus
