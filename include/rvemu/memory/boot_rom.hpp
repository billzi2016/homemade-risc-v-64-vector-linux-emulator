// 文件职责：实现启动阶段可受控装载、运行阶段只读的小端 Boot ROM 区域。
// 边界：ROM 不生成复位跳板或 FDT，也不允许通过运行期总线写入修改内容。

#pragma once

#include "rvemu/bus/address_region.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace rvemu::memory {

class BootRom final : public bus::AddressRegion {
   public:
    BootRom(bus::PhysicalAddress base, std::size_t size, std::string name = "boot-rom");

    // load 是唯一初始化写入口；密封后再次装载返回 ReadOnly，防止运行期篡改固件。
    [[nodiscard]] bus::AccessResult load(std::uint64_t offset,
                                         const std::vector<std::uint8_t>& data);
    void seal();
    [[nodiscard]] bool sealed() const;

    [[nodiscard]] bus::AccessResult read(std::uint64_t offset,
                                         bus::AccessWidth width,
                                         bus::AccessType type) override;

    [[nodiscard]] bus::AccessResult write(std::uint64_t offset,
                                          bus::AccessWidth width,
                                          std::uint64_t value,
                                          bus::AccessType type) override;

    [[nodiscard]] bus::AccessResult compare_exchange(std::uint64_t offset,
                                                     bus::AccessWidth width,
                                                     std::uint64_t expected,
                                                     std::uint64_t desired,
                                                     bus::AccessType type) override;

   private:
    [[nodiscard]] bus::AccessResult validate(std::uint64_t offset, bus::AccessWidth width) const;

    std::vector<std::uint8_t> bytes_;
    bool sealed_{false};
    mutable std::mutex mutex_;
};

}  // namespace rvemu::memory
