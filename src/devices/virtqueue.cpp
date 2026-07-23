// 文件职责：实现 VirtIO split virtqueue 直接和间接描述符链的统一安全解析。
// 边界：本文件不读取或写入描述符指向的数据缓冲，不提交 used ring，也不执行宿主 I/O。
// 主要依赖：Bus::read 的 DMA 访问结果；设备层只消费不可变解析视图。
// 关键不变量：索引、循环、方向和地址范围全部通过后才返回 ok。

#include "rvemu/devices/virtqueue.hpp"

#include <limits>
#include <utility>

namespace rvemu::devices {
namespace {

constexpr std::uint16_t kDescriptorFlagNext = 1U << 0U;
constexpr std::uint16_t kDescriptorFlagWrite = 1U << 1U;
constexpr std::uint16_t kDescriptorFlagIndirect = 1U << 2U;
constexpr std::uint64_t kDescriptorSize = 16U;
constexpr std::uint64_t kAvailableIndexOffset = 2U;
constexpr std::uint64_t kAvailableRingOffset = 4U;
constexpr std::uint16_t kAvailableFlagNoInterrupt = 1U;
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

struct RawDescriptor final {
    std::uint64_t address{0U};
    std::uint32_t length{0U};
    std::uint16_t flags{0U};
    std::uint16_t next{0U};
};

[[nodiscard]] VirtqueueParseResult read_raw_descriptor(bus::Bus& bus,
                                                       std::uint64_t table_address,
                                                       std::uint16_t index,
                                                       RawDescriptor& descriptor) {
    const auto descriptor_address =
        table_address + static_cast<std::uint64_t>(index) * kDescriptorSize;
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
    descriptor = RawDescriptor{address.value,
                               static_cast<std::uint32_t>(length.value),
                               static_cast<std::uint16_t>(flags.value),
                               static_cast<std::uint16_t>(next.value)};
    return VirtqueueParseResult{};
}

[[nodiscard]] VirtqueueParseResult parse_table(bus::Bus& bus,
                                               std::uint64_t table_address,
                                               std::uint16_t table_size,
                                               std::uint16_t head,
                                               bool allow_indirect,
                                               const std::vector<VirtqueueDescriptorDirection>& expected);

[[nodiscard]] VirtqueueParseResult parse_indirect_descriptor(
    bus::Bus& bus,
    const RawDescriptor& descriptor,
    const std::vector<VirtqueueDescriptorDirection>& expected) {
    if ((descriptor.flags & kDescriptorFlagWrite) != 0U || (descriptor.flags & kDescriptorFlagNext) != 0U
        || descriptor.length == 0U || (descriptor.length % kDescriptorSize) != 0U) {
        return failure(VirtqueueParseErrorCode::IndirectUnsupported,
                       "间接描述符必须只指向非空 16 字节对齐描述符表");
    }
    const auto indirect_count = descriptor.length / kDescriptorSize;
    if (indirect_count == 0U || indirect_count > std::numeric_limits<std::uint16_t>::max()) {
        return failure(VirtqueueParseErrorCode::ChainTooLong, "间接描述符表长度超出实现上限");
    }
    return parse_table(
        bus, descriptor.address, static_cast<std::uint16_t>(indirect_count), 0U, false, expected);
}

VirtqueueParseResult parse_table(bus::Bus& bus,
                                 std::uint64_t table_address,
                                 std::uint16_t table_size,
                                 std::uint16_t head,
                                 bool allow_indirect,
                                 const std::vector<VirtqueueDescriptorDirection>& expected) {
    if (head >= table_size) {
        return failure(VirtqueueParseErrorCode::IndexOutOfRange, "head 描述符索引越界");
    }

    std::vector<bool> visited(table_size, false);
    std::vector<VirtqueueSegment> segments;
    std::uint64_t total_length = 0U;
    auto index = head;
    for (std::uint16_t count = 0U; count < table_size; ++count) {
        if (index >= table_size) {
            return failure(VirtqueueParseErrorCode::IndexOutOfRange, "next 描述符索引越界");
        }
        if (visited[index]) {
            return failure(VirtqueueParseErrorCode::ChainLoop, "描述符链存在循环");
        }
        visited[index] = true;

        RawDescriptor descriptor{};
        const auto read = read_raw_descriptor(bus, table_address, index, descriptor);
        if (!read.ok()) {
            return read;
        }

        const auto descriptor_write = (descriptor.flags & kDescriptorFlagWrite) != 0U;
        if ((descriptor.flags & kDescriptorFlagIndirect) != 0U) {
            if (!allow_indirect || !segments.empty()) {
                return failure(VirtqueueParseErrorCode::IndirectUnsupported,
                               "未协商、嵌套或非 head 间接描述符被拒绝");
            }
            return parse_indirect_descriptor(bus, descriptor, expected);
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
        if (descriptor.length != 0U
            && add_overflows(descriptor.address, static_cast<std::uint64_t>(descriptor.length) - 1U)) {
            return failure(VirtqueueParseErrorCode::AddressOverflow,
                           "描述符 DMA 范围发生 64 位溢出");
        }
        if (add_overflows(total_length, descriptor.length)) {
            return failure(VirtqueueParseErrorCode::AddressOverflow,
                           "描述符链总长度发生 64 位溢出");
        }
        total_length += descriptor.length;

        segments.push_back(
            VirtqueueSegment{
                bus::PhysicalAddress{descriptor.address}, descriptor.length, descriptor_write});
        if ((descriptor.flags & kDescriptorFlagNext) == 0U) {
            if (!expected.empty() && segments.size() != expected.size()) {
                return failure(VirtqueueParseErrorCode::DirectionMismatch,
                               "描述符数量少于设备请求布局");
            }
            return VirtqueueParseResult{VirtqueueParseErrorCode::None, {}, std::move(segments)};
        }
        index = descriptor.next;
    }
    return failure(VirtqueueParseErrorCode::ChainTooLong, "描述符链长度超过队列大小");
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

VirtqueueRingErrorCode VirtqueueRuntimeState::driver_notification_needed(
    bus::Bus& bus,
    const VirtqueueLayout& layout,
    bool& needed) const {
    needed = false;
    if (!layout.ready || layout.size == 0U) {
        return VirtqueueRingErrorCode::QueueNotReady;
    }
    if (!layout.event_idx_enabled) {
        const auto flags = bus.read(layout.available_ring, bus::AccessWidth::HalfWord, bus::AccessType::DmaRead);
        if (!flags.ok()) {
            return VirtqueueRingErrorCode::DmaReadFailure;
        }
        needed = (static_cast<std::uint16_t>(flags.value) & kAvailableFlagNoInterrupt) == 0U;
        return VirtqueueRingErrorCode::None;
    }
    const auto used_event_address = layout.available_ring.value() + kAvailableRingOffset
                                    + static_cast<std::uint64_t>(layout.size) * 2U;
    const auto used_event = bus.read(
        bus::PhysicalAddress{used_event_address}, bus::AccessWidth::HalfWord, bus::AccessType::DmaRead);
    if (!used_event.ok()) {
        return VirtqueueRingErrorCode::DmaReadFailure;
    }
    const auto old_index = static_cast<std::uint16_t>(used_idx_ - 1U);
    needed = virtqueue_event_needed(
        static_cast<std::uint16_t>(used_event.value), used_idx_, old_index);
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

    return parse_table(bus,
                       layout.descriptor_table.value(),
                       layout.size,
                       head,
                       layout.indirect_enabled,
                       expected);
}

}  // namespace rvemu::devices
