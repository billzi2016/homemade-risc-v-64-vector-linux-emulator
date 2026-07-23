// 文件职责：实现 VirtIO split virtqueue 直接描述符链的统一安全解析。
// 边界：本文件不读取或写入描述符指向的数据缓冲，不提交 used ring，也不执行宿主 I/O。
// 主要依赖：Bus::read 的 DMA 访问结果；设备层只消费不可变解析视图。
// 关键不变量：索引、循环、方向和地址范围全部通过后才返回 ok。

#include "rvemu/devices/virtqueue.hpp"

#include <limits>

namespace rvemu::devices {
namespace {

constexpr std::uint16_t kDescriptorFlagNext = 1U << 0U;
constexpr std::uint16_t kDescriptorFlagWrite = 1U << 1U;
constexpr std::uint16_t kDescriptorFlagIndirect = 1U << 2U;
constexpr std::uint64_t kDescriptorSize = 16U;
constexpr std::uint64_t kAvailableIndexOffset = 2U;
constexpr std::uint64_t kAvailableRingOffset = 4U;
constexpr std::uint64_t kUsedIndexOffset = 2U;
constexpr std::uint64_t kUsedRingOffset = 4U;
constexpr std::uint64_t kUsedElementSize = 8U;

[[nodiscard]] VirtqueueParseResult failure(VirtqueueParseErrorCode code, const char* detail) {
    return VirtqueueParseResult{code, detail, {}};
}

[[nodiscard]] bus::AccessResult read_descriptor_field(bus::Bus& bus,
                                                      bus::PhysicalAddress address,
                                                      bus::AccessWidth width) {
    return bus.read(address, width, bus::AccessType::DmaRead);
}

[[nodiscard]] bool add_overflows(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    return lhs > std::numeric_limits<std::uint64_t>::max() - rhs;
}

[[nodiscard]] bool direction_matches(bool descriptor_write,
                                     VirtqueueDescriptorDirection expected) noexcept {
    switch (expected) {
        case VirtqueueDescriptorDirection::Any:
            return true;
        case VirtqueueDescriptorDirection::DeviceReads:
            return !descriptor_write;
        case VirtqueueDescriptorDirection::DeviceWrites:
            return descriptor_write;
    }
    return false;
}

}  // namespace

void VirtqueueRuntimeState::reset(std::uint16_t initial_index) noexcept {
    last_available_idx_ = initial_index;
    used_idx_ = initial_index;
    ++generation_;
}

VirtqueueAvailableResult VirtqueueRuntimeState::consume_available(bus::Bus& bus,
                                                                  const VirtqueueLayout& layout) {
    if (!layout.ready || layout.size == 0U) {
        return VirtqueueAvailableResult{VirtqueueRingErrorCode::QueueNotReady, 0U, 0U, 0U};
    }
    const auto idx = bus.read(bus::PhysicalAddress{layout.available_ring.value()
                                                  + kAvailableIndexOffset},
                              bus::AccessWidth::HalfWord,
                              bus::AccessType::DmaRead);
    if (!idx.ok()) {
        return VirtqueueAvailableResult{VirtqueueRingErrorCode::DmaReadFailure, 0U, 0U, 0U};
    }
    const auto available_index = static_cast<std::uint16_t>(idx.value);
    const auto pending = virtqueue_index_delta(available_index, last_available_idx_);
    if (pending == 0U) {
        return VirtqueueAvailableResult{
            VirtqueueRingErrorCode::None, available_index, 0U, 0U};
    }
    if (pending > layout.size) {
        return VirtqueueAvailableResult{
            VirtqueueRingErrorCode::TooManyPending, available_index, 0U, pending};
    }
    const auto slot = virtqueue_ring_slot(last_available_idx_, layout.size);
    const auto head = bus.read(bus::PhysicalAddress{layout.available_ring.value()
                                                   + kAvailableRingOffset
                                                   + static_cast<std::uint64_t>(slot) * 2U},
                               bus::AccessWidth::HalfWord,
                               bus::AccessType::DmaRead);
    if (!head.ok()) {
        return VirtqueueAvailableResult{VirtqueueRingErrorCode::DmaReadFailure,
                                        available_index,
                                        0U,
                                        pending};
    }
    ++last_available_idx_;
    return VirtqueueAvailableResult{
        VirtqueueRingErrorCode::None, available_index, static_cast<std::uint16_t>(head.value), pending};
}

VirtqueueRingErrorCode VirtqueueRuntimeState::publish_used(bus::Bus& bus,
                                                           const VirtqueueLayout& layout,
                                                           std::uint32_t id,
                                                           std::uint32_t length) {
    if (!layout.ready || layout.size == 0U) {
        return VirtqueueRingErrorCode::QueueNotReady;
    }
    const auto slot = virtqueue_ring_slot(used_idx_, layout.size);
    const auto element = layout.used_ring.value() + kUsedRingOffset
                         + static_cast<std::uint64_t>(slot) * kUsedElementSize;
    const auto id_write =
        bus.write(bus::PhysicalAddress{element}, bus::AccessWidth::Word, id, bus::AccessType::DmaWrite);
    if (!id_write.ok()) {
        return VirtqueueRingErrorCode::DmaWriteFailure;
    }
    const auto len_write = bus.write(bus::PhysicalAddress{element + 4U},
                                     bus::AccessWidth::Word,
                                     length,
                                     bus::AccessType::DmaWrite);
    if (!len_write.ok()) {
        return VirtqueueRingErrorCode::DmaWriteFailure;
    }
    const auto next_used_idx = static_cast<std::uint16_t>(used_idx_ + 1U);
    const auto idx_write = bus.write(bus::PhysicalAddress{layout.used_ring.value()
                                                          + kUsedIndexOffset},
                                     bus::AccessWidth::HalfWord,
                                     next_used_idx,
                                     bus::AccessType::DmaWrite);
    if (!idx_write.ok()) {
        return VirtqueueRingErrorCode::DmaWriteFailure;
    }
    used_idx_ = next_used_idx;
    return VirtqueueRingErrorCode::None;
}

VirtqueueParseResult parse_descriptor_chain(
    bus::Bus& bus,
    const VirtqueueLayout& layout,
    std::uint16_t head,
    const std::vector<VirtqueueDescriptorDirection>& expected) {
    if (!layout.ready || layout.size == 0U) {
        return failure(VirtqueueParseErrorCode::QueueNotReady, "virtqueue 尚未 ready");
    }
    if (head >= layout.size) {
        return failure(VirtqueueParseErrorCode::IndexOutOfRange, "head 描述符索引越界");
    }

    std::vector<bool> visited(layout.size, false);
    std::vector<VirtqueueSegment> segments;
    std::uint64_t total_length = 0U;
    auto index = head;
    for (std::uint16_t count = 0U; count < layout.size; ++count) {
        if (index >= layout.size) {
            return failure(VirtqueueParseErrorCode::IndexOutOfRange, "next 描述符索引越界");
        }
        if (visited[index]) {
            return failure(VirtqueueParseErrorCode::ChainLoop, "描述符链存在循环");
        }
        visited[index] = true;

        const auto descriptor_address =
            layout.descriptor_table.value() + static_cast<std::uint64_t>(index) * kDescriptorSize;
        const auto address = read_descriptor_field(
            bus, bus::PhysicalAddress{descriptor_address}, bus::AccessWidth::DoubleWord);
        const auto length = read_descriptor_field(
            bus, bus::PhysicalAddress{descriptor_address + 8U}, bus::AccessWidth::Word);
        const auto flags = read_descriptor_field(
            bus, bus::PhysicalAddress{descriptor_address + 12U}, bus::AccessWidth::HalfWord);
        const auto next = read_descriptor_field(
            bus, bus::PhysicalAddress{descriptor_address + 14U}, bus::AccessWidth::HalfWord);
        if (!address.ok() || !length.ok() || !flags.ok() || !next.ok()) {
            return failure(VirtqueueParseErrorCode::DmaReadFailure, "读取描述符元数据失败");
        }

        const auto descriptor_flags = static_cast<std::uint16_t>(flags.value);
        const auto descriptor_length = static_cast<std::uint32_t>(length.value);
        const auto descriptor_write = (descriptor_flags & kDescriptorFlagWrite) != 0U;
        if ((descriptor_flags & kDescriptorFlagIndirect) != 0U) {
            return failure(VirtqueueParseErrorCode::IndirectUnsupported,
                           "当前公共解析器未协商间接描述符");
        }
        if (!expected.empty()) {
            if (segments.size() >= expected.size()) {
                return failure(VirtqueueParseErrorCode::DirectionMismatch,
                               "描述符数量超过设备请求布局");
            }
            if (!direction_matches(descriptor_write, expected[segments.size()])) {
                return failure(VirtqueueParseErrorCode::DirectionMismatch,
                               "描述符方向与设备请求布局不匹配");
            }
        }
        if (descriptor_length != 0U
            && add_overflows(address.value, static_cast<std::uint64_t>(descriptor_length) - 1U)) {
            return failure(VirtqueueParseErrorCode::AddressOverflow,
                           "描述符 DMA 范围发生 64 位溢出");
        }
        if (add_overflows(total_length, descriptor_length)) {
            return failure(VirtqueueParseErrorCode::AddressOverflow,
                           "描述符链总长度发生 64 位溢出");
        }
        total_length += descriptor_length;

        segments.push_back(
            VirtqueueSegment{
                bus::PhysicalAddress{address.value}, descriptor_length, descriptor_write});
        if ((descriptor_flags & kDescriptorFlagNext) == 0U) {
            if (!expected.empty() && segments.size() != expected.size()) {
                return failure(VirtqueueParseErrorCode::DirectionMismatch,
                               "描述符数量少于设备请求布局");
            }
            return VirtqueueParseResult{VirtqueueParseErrorCode::None, {}, std::move(segments)};
        }
        index = static_cast<std::uint16_t>(next.value);
    }
    return failure(VirtqueueParseErrorCode::ChainTooLong, "描述符链长度超过队列大小");
}

}  // namespace rvemu::devices
