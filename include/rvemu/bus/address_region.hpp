// 文件职责：定义 RAM、ROM 与 MMIO 设备共同遵循的生产物理地址区域接口。
// 边界：接口不决定区域所有权、全局地址分发顺序或虚拟内存翻译。

#pragma once

#include "rvemu/bus/access.hpp"
#include "rvemu/bus/address_range.hpp"

#include <string>
#include <utility>

namespace rvemu::bus {

class AddressRegion {
public:
    AddressRegion(std::string name, AddressRange range)
        : name_(std::move(name)), range_(range) {}
    virtual ~AddressRegion() = default;

    AddressRegion(const AddressRegion&) = delete;
    AddressRegion& operator=(const AddressRegion&) = delete;
    AddressRegion(AddressRegion&&) = delete;
    AddressRegion& operator=(AddressRegion&&) = delete;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const AddressRange& range() const noexcept { return range_; }

    [[nodiscard]] virtual AccessResult read(
        std::uint64_t offset,
        AccessWidth width,
        AccessType type) = 0;

    [[nodiscard]] virtual AccessResult write(
        std::uint64_t offset,
        AccessWidth width,
        std::uint64_t value,
        AccessType type) = 0;

    // 原子比较并更新必须在区域内部成为不可分割事务；value 返回提交时观察到的旧值。
    [[nodiscard]] virtual AccessResult compare_exchange(
        std::uint64_t offset,
        AccessWidth width,
        std::uint64_t expected,
        std::uint64_t desired,
        AccessType type) = 0;

private:
    std::string name_;
    AddressRange range_;
};

}  // namespace rvemu::bus
