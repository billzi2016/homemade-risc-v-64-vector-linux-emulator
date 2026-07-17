// 文件职责：提供唯一的来宾物理地址分发入口，完成区域注册、重叠拒绝和结构化访问错误。
// 边界：总线不翻译虚拟地址、不解释 CPU 指令，也不实现具体 RAM、ROM 或设备寄存器。

#pragma once

#include "rvemu/bus/access.hpp"
#include "rvemu/bus/address_region.hpp"

#include <memory>
#include <mutex>
#include <optional>
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

    // 在同一总线事务内读取并建立精确到操作数字节范围的单 Hart 保留。
    [[nodiscard]] LoadReservedResult load_reserved(
        PhysicalAddress address,
        AccessWidth width);

    // 无论成功、失败还是地址错误，SC 都消费当前 token；exchanged 表示是否真正写入。
    [[nodiscard]] AccessResult store_conditional(
        ReservationToken token,
        PhysicalAddress address,
        AccessWidth width,
        std::uint64_t value);

    [[nodiscard]] std::size_t region_count() const noexcept { return regions_.size(); }

private:
    struct LocatedRegion final {
        AddressRegion* region{nullptr};
        std::uint64_t offset{0U};
        AccessResult failure{AccessResult::success()};
    };

    [[nodiscard]] LocatedRegion locate(PhysicalAddress address, AccessWidth width);

    struct ReservationRecord final {
        ReservationToken token{};
        PhysicalAddress address{};
        std::uint64_t length{0U};
    };

    // 调用方必须持有 transaction_mutex_；成功写入统一经过此处使重叠保留失效。
    void invalidate_reservation_locked(PhysicalAddress address, std::uint64_t length) noexcept;
    [[nodiscard]] ReservationToken next_reservation_token_locked() noexcept;

    std::vector<std::shared_ptr<AddressRegion>> regions_;
    std::mutex transaction_mutex_;
    std::optional<ReservationRecord> reservation_{};
    std::uint64_t next_reservation_token_{1U};
};

}  // namespace rvemu::bus
