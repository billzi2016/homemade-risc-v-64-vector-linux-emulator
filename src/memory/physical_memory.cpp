// 文件职责：实现真实生产 RAM 的小端字节存储、完整范围验证和原子 compare-exchange。
// 边界：本文件不处理页表权限、TLB、设备 DMA 描述符或宿主磁盘文件。

#include "rvemu/memory/physical_memory.hpp"

#include "rvemu/bus/address_range.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace rvemu::memory {
namespace {

[[nodiscard]] bus::AddressRange require_range(bus::PhysicalAddress base, std::size_t size) {
    const auto size64 = static_cast<std::uint64_t>(size);
    const auto range = bus::AddressRange::create(base, size64);
    if (!range.has_value()) {
        throw std::invalid_argument("RAM 物理地址范围为空或发生 64 位溢出");
    }
    return *range;
}

[[nodiscard]] std::uint64_t value_mask(bus::AccessWidth width) noexcept {
    const auto byte_count = bus::width_in_bytes(width);
    if (byte_count == 8U) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    if (byte_count == 0U) {
        return 0U;
    }
    return (1ULL << (byte_count * 8U)) - 1U;
}

}  // namespace

PhysicalMemory::PhysicalMemory(bus::PhysicalAddress base, std::size_t size, std::string name)
    : AddressRegion(std::move(name), require_range(base, size)), bytes_(size, 0U) {
}

bus::AccessResult PhysicalMemory::validate(std::uint64_t offset, bus::AccessWidth width) const {
    const auto length = bus::width_in_bytes(width);
    if (length == 0U) {
        return bus::AccessResult::failure(bus::BusError{bus::BusErrorCode::InvalidWidth,
                                                        range().base(),
                                                        0U,
                                                        name(),
                                                        "RAM 收到不支持的访问宽度"});
    }

    const auto memory_size = static_cast<std::uint64_t>(bytes_.size());
    if (offset > memory_size || length > memory_size - offset) {
        return bus::AccessResult::failure(bus::BusError{
            bus::BusErrorCode::OutOfBounds, range().base(), length, name(), "RAM 区域内偏移越界"});
    }
    return bus::AccessResult::success();
}

std::uint64_t PhysicalMemory::read_unlocked(std::uint64_t offset, bus::AccessWidth width) const {
    std::uint64_t value = 0U;
    const auto length = bus::width_in_bytes(width);
    for (std::uint64_t index = 0U; index < length; ++index) {
        const auto position = static_cast<std::size_t>(offset + index);
        value |= static_cast<std::uint64_t>(bytes_[position]) << (index * 8U);
    }
    return value;
}

void PhysicalMemory::write_unlocked(std::uint64_t offset,
                                    bus::AccessWidth width,
                                    std::uint64_t value) {
    const auto length = bus::width_in_bytes(width);
    for (std::uint64_t index = 0U; index < length; ++index) {
        const auto position = static_cast<std::size_t>(offset + index);
        const auto shifted = (value >> (index * 8U)) & 0xFFU;
        bytes_[position] = static_cast<std::uint8_t>(shifted);
    }
}

bus::AccessResult PhysicalMemory::read(std::uint64_t offset,
                                       bus::AccessWidth width,
                                       bus::AccessType type) {
    static_cast<void>(type);
    std::lock_guard<std::mutex> lock{mutex_};
    const auto validation = validate(offset, width);
    if (!validation.ok()) {
        return validation;
    }
    return bus::AccessResult::success(read_unlocked(offset, width));
}

bus::AccessResult PhysicalMemory::write(std::uint64_t offset,
                                        bus::AccessWidth width,
                                        std::uint64_t value,
                                        bus::AccessType type) {
    static_cast<void>(type);
    std::lock_guard<std::mutex> lock{mutex_};
    const auto validation = validate(offset, width);
    if (!validation.ok()) {
        return validation;
    }
    write_unlocked(offset, width, value);
    return bus::AccessResult::success();
}

bus::AccessResult PhysicalMemory::compare_exchange(std::uint64_t offset,
                                                   bus::AccessWidth width,
                                                   std::uint64_t expected,
                                                   std::uint64_t desired,
                                                   bus::AccessType type) {
    static_cast<void>(type);
    std::lock_guard<std::mutex> lock{mutex_};
    const auto validation = validate(offset, width);
    if (!validation.ok()) {
        return validation;
    }

    const auto observed = read_unlocked(offset, width);
    const auto masked_expected = expected & value_mask(width);
    if (observed != masked_expected) {
        return bus::AccessResult::success(observed, false);
    }

    write_unlocked(offset, width, desired & value_mask(width));
    return bus::AccessResult::success(observed, true);
}

}  // namespace rvemu::memory
