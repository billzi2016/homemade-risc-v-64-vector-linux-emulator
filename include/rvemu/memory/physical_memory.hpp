// 文件职责：实现连续、零初始化、线程安全的小端来宾物理 RAM 区域。
// 边界：RAM 不处理虚拟地址、权限页表、MMIO 副作用或宿主文件映射。

#pragma once

#include "rvemu/bus/address_region.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace rvemu::memory {

class PhysicalMemory final : public bus::AddressRegion {
public:
    PhysicalMemory(
        bus::PhysicalAddress base,
        std::size_t size,
        std::string name = "ram");

    [[nodiscard]] bus::AccessResult read(
        std::uint64_t offset,
        bus::AccessWidth width,
        bus::AccessType type) override;

    [[nodiscard]] bus::AccessResult write(
        std::uint64_t offset,
        bus::AccessWidth width,
        std::uint64_t value,
        bus::AccessType type) override;

    [[nodiscard]] bus::AccessResult compare_exchange(
        std::uint64_t offset,
        bus::AccessWidth width,
        std::uint64_t expected,
        std::uint64_t desired,
        bus::AccessType type) override;

private:
    [[nodiscard]] bus::AccessResult validate(
        std::uint64_t offset,
        bus::AccessWidth width) const;
    [[nodiscard]] std::uint64_t read_unlocked(std::uint64_t offset, bus::AccessWidth width) const;
    void write_unlocked(std::uint64_t offset, bus::AccessWidth width, std::uint64_t value);

    std::vector<std::uint8_t> bytes_;
    mutable std::mutex mutex_;
};

}  // namespace rvemu::memory
