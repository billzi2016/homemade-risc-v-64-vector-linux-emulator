// 文件职责：实现真实 Boot ROM 的初始化装载、密封状态、小端读取和运行期只读保护。
// 边界：本文件不生成固件内容，也不提供任何可绕过密封状态的裸存储指针。

#include "rvemu/memory/boot_rom.hpp"

#include "rvemu/bus/address_range.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace rvemu::memory {
namespace {

[[nodiscard]] bus::AddressRange require_range(bus::PhysicalAddress base, std::size_t size) {
    const auto range = bus::AddressRange::create(base, static_cast<std::uint64_t>(size));
    if (!range.has_value()) {
        throw std::invalid_argument("Boot ROM 物理地址范围为空或发生 64 位溢出");
    }
    return *range;
}

[[nodiscard]] bus::AccessResult read_only_failure(const bus::AddressRegion& region,
                                                  std::uint64_t offset,
                                                  std::uint64_t length,
                                                  std::string detail) {
    auto address_value = region.range().base().value();
    if (offset <= region.range().size()) {
        address_value += offset;
    }
    return bus::AccessResult::failure(bus::BusError{bus::BusErrorCode::ReadOnly,
                                                    bus::PhysicalAddress{address_value},
                                                    length,
                                                    region.name(),
                                                    std::move(detail)});
}

}  // namespace

BootRom::BootRom(bus::PhysicalAddress base, std::size_t size, std::string name)
    : AddressRegion(std::move(name), require_range(base, size)), bytes_(size, 0U) {
}

bus::AccessResult BootRom::load(std::uint64_t offset, const std::vector<std::uint8_t>& data) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (sealed_) {
        return read_only_failure(
            *this, offset, static_cast<std::uint64_t>(data.size()), "Boot ROM 已密封");
    }

    const auto rom_size = static_cast<std::uint64_t>(bytes_.size());
    const auto data_size = static_cast<std::uint64_t>(data.size());
    if (offset > rom_size || data_size > rom_size - offset) {
        return bus::AccessResult::failure(bus::BusError{bus::BusErrorCode::OutOfBounds,
                                                        range().base(),
                                                        data_size,
                                                        name(),
                                                        "初始化数据超出 Boot ROM 区域"});
    }

    const auto destination = bytes_.begin() + static_cast<std::ptrdiff_t>(offset);
    std::copy(data.begin(), data.end(), destination);
    return bus::AccessResult::success();
}

void BootRom::seal() {
    std::lock_guard<std::mutex> lock{mutex_};
    sealed_ = true;
}

bool BootRom::sealed() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return sealed_;
}

bus::AccessResult BootRom::validate(std::uint64_t offset, bus::AccessWidth width) const {
    const auto length = bus::width_in_bytes(width);
    if (length == 0U) {
        return bus::AccessResult::failure(bus::BusError{bus::BusErrorCode::InvalidWidth,
                                                        range().base(),
                                                        0U,
                                                        name(),
                                                        "Boot ROM 收到不支持的访问宽度"});
    }

    const auto rom_size = static_cast<std::uint64_t>(bytes_.size());
    if (offset > rom_size || length > rom_size - offset) {
        return bus::AccessResult::failure(bus::BusError{bus::BusErrorCode::OutOfBounds,
                                                        range().base(),
                                                        length,
                                                        name(),
                                                        "Boot ROM 区域内偏移越界"});
    }
    return bus::AccessResult::success();
}

bus::AccessResult BootRom::read(std::uint64_t offset,
                                bus::AccessWidth width,
                                bus::AccessType type) {
    static_cast<void>(type);
    std::lock_guard<std::mutex> lock{mutex_};
    const auto validation = validate(offset, width);
    if (!validation.ok()) {
        return validation;
    }

    std::uint64_t value = 0U;
    const auto length = bus::width_in_bytes(width);
    for (std::uint64_t index = 0U; index < length; ++index) {
        const auto position = static_cast<std::size_t>(offset + index);
        value |= static_cast<std::uint64_t>(bytes_[position]) << (index * 8U);
    }
    return bus::AccessResult::success(value);
}

bus::AccessResult BootRom::write(std::uint64_t offset,
                                 bus::AccessWidth width,
                                 std::uint64_t value,
                                 bus::AccessType type) {
    static_cast<void>(value);
    static_cast<void>(type);
    return read_only_failure(
        *this, offset, bus::width_in_bytes(width), "运行期不允许写入 Boot ROM");
}

bus::AccessResult BootRom::compare_exchange(std::uint64_t offset,
                                            bus::AccessWidth width,
                                            std::uint64_t expected,
                                            std::uint64_t desired,
                                            bus::AccessType type) {
    static_cast<void>(expected);
    static_cast<void>(desired);
    static_cast<void>(type);
    return read_only_failure(
        *this, offset, bus::width_in_bytes(width), "Boot ROM 不支持原子写事务");
}

}  // namespace rvemu::memory
