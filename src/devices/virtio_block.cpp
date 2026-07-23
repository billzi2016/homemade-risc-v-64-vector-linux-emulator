// 文件职责：实现 VirtIO-Blk IN/OUT 请求、状态字节、只读策略和 512 字节扇区边界检查。
// 边界：本文件不拥有宿主文件描述符，不实现 Linux 驱动探测之外的可选请求类型。
// 主要依赖：公共 VirtIO transport/virtqueue 与 DiskBackend 的完整 pread/pwrite。
// 关键不变量：请求链、方向和磁盘范围验证失败时只写受控 status，不执行越界宿主 I/O。

#include "rvemu/devices/virtio_block.hpp"

#include <limits>
#include <vector>

namespace rvemu::devices {
namespace {

[[nodiscard]] bool add_overflows(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    return lhs > std::numeric_limits<std::uint64_t>::max() - rhs;
}

[[nodiscard]] bool sector_range_valid(std::uint64_t sector,
                                      std::uint64_t byte_count,
                                      std::uint64_t capacity_sectors) noexcept {
    const auto sector_count = (byte_count + platform::DiskBackend::kSectorSize - 1U)
                              / platform::DiskBackend::kSectorSize;
    return !add_overflows(sector, sector_count) && sector + sector_count <= capacity_sectors;
}

}  // namespace

VirtioBlockDevice::VirtioBlockDevice(VirtioMmioTransport& transport,
                                     platform::DiskBackend& disk) noexcept
    : transport_(transport), disk_(disk) {
}

bool VirtioBlockDevice::read_header(bus::Bus& bus,
                                    const VirtqueueSegment& segment,
                                    RequestHeader& header) const {
    if (segment.device_writes || segment.length < 16U) {
        return false;
    }
    const auto type = bus.read(segment.address, bus::AccessWidth::Word, bus::AccessType::DmaRead);
    const auto sector = bus.read(bus::PhysicalAddress{segment.address.value() + 8U},
                                 bus::AccessWidth::DoubleWord,
                                 bus::AccessType::DmaRead);
    if (!type.ok() || !sector.ok()) {
        return false;
    }
    header.type = static_cast<std::uint32_t>(type.value);
    header.sector = sector.value;
    return true;
}

bool VirtioBlockDevice::copy_from_guest(bus::Bus& bus,
                                        const VirtqueueSegment& segment,
                                        std::vector<std::uint8_t>& output) const {
    if (segment.device_writes) {
        return false;
    }
    output.resize(segment.length);
    for (std::uint32_t index = 0U; index < segment.length; ++index) {
        const auto byte = bus.read(bus::PhysicalAddress{segment.address.value() + index},
                                   bus::AccessWidth::Byte,
                                   bus::AccessType::DmaRead);
        if (!byte.ok()) {
            return false;
        }
        output[index] = static_cast<std::uint8_t>(byte.value);
    }
    return true;
}

bool VirtioBlockDevice::copy_to_guest(bus::Bus& bus,
                                      const VirtqueueSegment& segment,
                                      const std::vector<std::uint8_t>& input) const {
    if (!segment.device_writes || segment.length != input.size()) {
        return false;
    }
    for (std::uint32_t index = 0U; index < segment.length; ++index) {
        const auto written = bus.write(bus::PhysicalAddress{segment.address.value() + index},
                                       bus::AccessWidth::Byte,
                                       input[index],
                                       bus::AccessType::DmaWrite);
        if (!written.ok()) {
            return false;
        }
    }
    return true;
}

bool VirtioBlockDevice::write_status(bus::Bus& bus,
                                     const VirtqueueSegment& segment,
                                     std::uint8_t status) const {
    if (!segment.device_writes || segment.length < 1U) {
        return false;
    }
    return bus.write(segment.address, bus::AccessWidth::Byte, status, bus::AccessType::DmaWrite).ok();
}

VirtioBlockProcessStatus VirtioBlockDevice::process_one(bus::Bus& bus) {
    std::uint16_t queue_index = 0U;
    if (!transport_.pop_notification(queue_index)) {
        return VirtioBlockProcessStatus::NoNotification;
    }
    const auto layout = transport_.queue_layout(queue_index);
    const auto available = queue_runtime_.consume_available(bus, layout);
    if (!available.ok() || available.pending_count == 0U) {
        return VirtioBlockProcessStatus::QueueError;
    }
    const auto parsed = parse_descriptor_chain(bus, layout, available.head);
    if (!parsed.ok() || parsed.segments.size() < 3U) {
        return VirtioBlockProcessStatus::QueueError;
    }

    RequestHeader header{};
    const auto& status_segment = parsed.segments.back();
    std::uint8_t status = kStatusIoError;
    std::uint32_t used_length = 1U;
    if (!read_header(bus, parsed.segments.front(), header) || !status_segment.device_writes
        || status_segment.length < 1U) {
        return VirtioBlockProcessStatus::QueueError;
    } else if (header.type != kRequestIn && header.type != kRequestOut) {
        status = kStatusUnsupported;
    } else {
        std::vector<std::uint8_t> data;
        for (std::size_t index = 1U; index + 1U < parsed.segments.size(); ++index) {
            const auto& segment = parsed.segments[index];
            if ((header.type == kRequestIn && !segment.device_writes)
                || (header.type == kRequestOut && segment.device_writes)) {
                status = kStatusIoError;
                data.clear();
                break;
            }
            std::vector<std::uint8_t> part;
            if (header.type == kRequestOut && !copy_from_guest(bus, segment, part)) {
                data.clear();
                break;
            }
            if (header.type == kRequestOut) {
                data.insert(data.end(), part.begin(), part.end());
            } else {
                data.resize(data.size() + segment.length);
            }
        }
        if (!data.empty() || parsed.segments.size() == 3U) {
            if (!sector_range_valid(header.sector, data.size(), disk_.capacity_sectors())) {
                status = kStatusIoError;
            } else if (header.type == kRequestOut) {
                status = disk_.write(header.sector, data).ok() ? kStatusOk : kStatusIoError;
            } else {
                status = disk_.read(header.sector, data).ok() ? kStatusOk : kStatusIoError;
                std::size_t offset = 0U;
                for (std::size_t index = 1U; status == kStatusOk && index + 1U < parsed.segments.size();
                     ++index) {
                    const auto length = parsed.segments[index].length;
                    const std::vector<std::uint8_t> part(data.begin() + static_cast<std::ptrdiff_t>(offset),
                                                         data.begin()
                                                             + static_cast<std::ptrdiff_t>(offset + length));
                    if (!copy_to_guest(bus, parsed.segments[index], part)) {
                        status = kStatusIoError;
                    }
                    offset += length;
                }
                if (status == kStatusOk) {
                    used_length += static_cast<std::uint32_t>(data.size());
                }
            }
        }
    }

    if (!write_status(bus, status_segment, status)) {
        return VirtioBlockProcessStatus::QueueError;
    }
    if (queue_runtime_.publish_used(bus, layout, available.head, used_length)
        != VirtqueueRingErrorCode::None) {
        return VirtioBlockProcessStatus::QueueError;
    }
    transport_.raise_used_buffer_interrupt();
    return VirtioBlockProcessStatus::Completed;
}

}  // namespace rvemu::devices
