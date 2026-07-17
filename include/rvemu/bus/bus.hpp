// 文件职责：提供唯一的来宾物理地址分发入口，完成区域注册、重叠拒绝和结构化访问错误。
// 边界：总线不翻译虚拟地址、不解释 CPU 指令，也不实现具体 RAM、ROM 或设备寄存器。

#pragma once

#include "rvemu/bus/access.hpp"
#include "rvemu/bus/address_region.hpp"

#include <memory>
#include <vector>

namespace rvemu::bus {

class Bus final {
public:
    // 总线共享持有已注册区域，使平台层可同时保留设备句柄用于 tick 或中断更新。
    [[nodiscard]] AccessResult register_region(std::shared_ptr<AddressRegion> region);

    [[nodiscard]] AccessResult read(
        PhysicalAddress address,
        AccessWidth width,
        AccessType type);

    [[nodiscard]] AccessResult write(
        PhysicalAddress address,
        AccessWidth width,
        std::uint64_t value,
        AccessType type);

    [[nodiscard]] AccessResult compare_exchange(
        PhysicalAddress address,
        AccessWidth width,
        std::uint64_t expected,
        std::uint64_t desired,
        AccessType type = AccessType::Atomic);

    [[nodiscard]] std::size_t region_count() const noexcept { return regions_.size(); }

private:
    struct LocatedRegion final {
        AddressRegion* region{nullptr};
        std::uint64_t offset{0U};
        AccessResult failure{AccessResult::success()};
    };

    [[nodiscard]] LocatedRegion locate(PhysicalAddress address, AccessWidth width);

    std::vector<std::shared_ptr<AddressRegion>> regions_;
};

}  // namespace rvemu::bus
