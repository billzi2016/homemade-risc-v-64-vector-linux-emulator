// 文件职责：实现 VirtIO 1.x MMIO transport 的共享寄存器、feature 协商、队列 ready 和通知门控。
// 边界：本文件不处理块/网设备配置空间，不解析具体请求，也不访问宿主磁盘或 TAP。
// 主要依赖：AddressRegion 统一 MMIO 接口；PLIC 只接收 interrupt status 电平。
// 关键不变量：复位清空全部运行态；DRIVER_OK 前 QueueNotify 不产生待处理请求。

#include "rvemu/devices/virtio_mmio.hpp"

#include "rvemu/devices/plic.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace rvemu::devices {
namespace {

constexpr std::uint64_t kMagicValueOffset = 0x000U;
constexpr std::uint64_t kVersionOffset = 0x004U;
constexpr std::uint64_t kDeviceIdOffset = 0x008U;
constexpr std::uint64_t kVendorIdOffset = 0x00CU;
constexpr std::uint64_t kDeviceFeaturesOffset = 0x010U;
constexpr std::uint64_t kDeviceFeaturesSelOffset = 0x014U;
constexpr std::uint64_t kDriverFeaturesOffset = 0x020U;
constexpr std::uint64_t kDriverFeaturesSelOffset = 0x024U;
constexpr std::uint64_t kQueueSelOffset = 0x030U;
constexpr std::uint64_t kQueueNumMaxOffset = 0x034U;
constexpr std::uint64_t kQueueNumOffset = 0x038U;
constexpr std::uint64_t kQueueReadyOffset = 0x044U;
constexpr std::uint64_t kQueueNotifyOffset = 0x050U;
constexpr std::uint64_t kInterruptStatusOffset = 0x060U;
constexpr std::uint64_t kInterruptAckOffset = 0x064U;
constexpr std::uint64_t kStatusOffset = 0x070U;
constexpr std::uint64_t kQueueDescLowOffset = 0x080U;
constexpr std::uint64_t kQueueDescHighOffset = 0x084U;
constexpr std::uint64_t kQueueDriverLowOffset = 0x090U;
constexpr std::uint64_t kQueueDriverHighOffset = 0x094U;
constexpr std::uint64_t kQueueDeviceLowOffset = 0x0A0U;
constexpr std::uint64_t kQueueDeviceHighOffset = 0x0A4U;
constexpr std::uint64_t kConfigGenerationOffset = 0x0FCU;

constexpr std::uint32_t kStatusAcknowledge = 1U;
constexpr std::uint32_t kStatusDriver = 2U;
constexpr std::uint32_t kStatusDriverOk = 4U;
constexpr std::uint32_t kStatusFeaturesOk = 8U;
constexpr std::uint32_t kStatusDeviceNeedsReset = 64U;
constexpr std::uint32_t kStatusFailed = 128U;
constexpr std::uint32_t kStatusKnownMask = kStatusAcknowledge | kStatusDriver | kStatusDriverOk
                                           | kStatusFeaturesOk | kStatusDeviceNeedsReset
                                           | kStatusFailed;
constexpr std::uint32_t kInterruptUsedBuffer = 1U;

[[nodiscard]] bus::AddressRange require_range(const VirtioMmioConfig& config) {
    const auto range =
        bus::AddressRange::create(bus::PhysicalAddress{config.base}, config.size);
    if (!range.has_value()) {
        throw std::logic_error("VirtIO MMIO 固定物理地址范围无效");
    }
    return *range;
}

[[nodiscard]] bus::AccessResult invalid_access(const VirtioMmioConfig& config,
                                               std::uint64_t offset,
                                               bus::AccessWidth width,
                                               const char* detail) {
    return bus::AccessResult::failure(
        bus::BusError{bus::BusErrorCode::InvalidWidth,
                      bus::PhysicalAddress{config.base + offset},
                      bus::width_in_bytes(width),
                      config.name,
                      detail});
}

[[nodiscard]] bool power_of_two(std::uint16_t value) noexcept {
    return value != 0U && (value & static_cast<std::uint16_t>(value - 1U)) == 0U;
}

[[nodiscard]] bool aligned(std::uint64_t value, std::uint64_t alignment) noexcept {
    return value != 0U && (value % alignment) == 0U;
}

[[nodiscard]] std::uint32_t low32(std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(value & 0xFFFF'FFFFULL);
}

[[nodiscard]] std::uint32_t high32(std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(value >> 32U);
}

void write_low32(std::uint64_t& target, std::uint32_t value) noexcept {
    target = (target & 0xFFFF'FFFF'0000'0000ULL) | value;
}

void write_high32(std::uint64_t& target, std::uint32_t value) noexcept {
    target = (target & 0x0000'0000'FFFF'FFFFULL) | (static_cast<std::uint64_t>(value) << 32U);
}

}  // namespace

VirtioMmioTransport::VirtioMmioTransport(VirtioMmioConfig config)
    : AddressRegion(config.name, require_range(config)),
      config_(config),
      offered_features_(config.device_features | kFeatureVersion1) {
    if (config_.queue_count == 0U || config_.queue_count > queues_.size()) {
        throw std::invalid_argument("VirtIO queue_count 超出当前 transport 上限");
    }
    if (!power_of_two(config_.queue_size_max)) {
        throw std::invalid_argument("VirtIO queue_size_max 必须为非零 2 的幂");
    }
}

void VirtioMmioTransport::synchronize(Plic& plic) const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    plic.set_source_level(config_.interrupt_source, interrupt_status_ != 0U);
}

bool VirtioMmioTransport::pop_notification(std::uint16_t& queue_index) noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    if (notifications_.empty()) {
        return false;
    }
    queue_index = notifications_.front();
    notifications_.pop_front();
    return true;
}

void VirtioMmioTransport::raise_used_buffer_interrupt() noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    interrupt_status_ |= kInterruptUsedBuffer;
}

VirtqueueLayout VirtioMmioTransport::queue_layout(std::uint16_t queue_index) const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    if (queue_index >= config_.queue_count) {
        return {};
    }
    const auto& queue = queues_[queue_index];
    return VirtqueueLayout{queue.size,
                           bus::PhysicalAddress{queue.descriptor_table},
                           bus::PhysicalAddress{queue.available_ring},
                           bus::PhysicalAddress{queue.used_ring},
                           queue.ready,
                           (driver_features_ & (1ULL << 28U)) != 0U};
}

bus::AccessResult VirtioMmioTransport::validate_register_access(std::uint64_t offset,
                                                                bus::AccessWidth width) const {
    if (width != bus::AccessWidth::Word || (offset & 0x3U) != 0U) {
        return invalid_access(config_, offset, width, "VirtIO MMIO 仅支持自然对齐 32 位寄存器访问");
    }
    return bus::AccessResult::success();
}

void VirtioMmioTransport::reset_locked() noexcept {
    driver_features_ = 0U;
    device_features_selector_ = 0U;
    driver_features_selector_ = 0U;
    queue_selector_ = 0U;
    interrupt_status_ = 0U;
    status_ = 0U;
    ++config_generation_;
    ++generation_;
    notifications_.clear();
    for (auto& queue : queues_) {
        queue = QueueState{};
    }
}

std::uint32_t VirtioMmioTransport::read_feature_page(std::uint64_t features,
                                                     std::uint32_t selector) const noexcept {
    if (selector == 0U) {
        return low32(features);
    }
    if (selector == 1U) {
        return high32(features);
    }
    return 0U;
}

bool VirtioMmioTransport::selected_queue_valid_locked() const noexcept {
    if (queue_selector_ >= config_.queue_count) {
        return false;
    }
    const auto& queue = queues_[queue_selector_];
    if (queue.size == 0U || queue.size > config_.queue_size_max || !power_of_two(queue.size)) {
        return false;
    }
    return aligned(queue.descriptor_table, 16U) && aligned(queue.available_ring, 2U)
           && aligned(queue.used_ring, 4U);
}

void VirtioMmioTransport::write_status_locked(std::uint32_t value) noexcept {
    if (value == 0U) {
        reset_locked();
        return;
    }
    auto requested = value & kStatusKnownMask;
    if ((requested & kStatusFailed) != 0U) {
        status_ = requested;
        return;
    }

    const auto base_ready =
        (requested & (kStatusAcknowledge | kStatusDriver)) == (kStatusAcknowledge | kStatusDriver);
    const auto feature_subset = (driver_features_ & ~offered_features_) == 0U;
    const auto modern_selected = (driver_features_ & kFeatureVersion1) != 0U;
    if ((requested & kStatusFeaturesOk) != 0U
        && (!base_ready || !feature_subset || !modern_selected)) {
        requested &= ~kStatusFeaturesOk;
    }
    const auto features_ok = (requested & kStatusFeaturesOk) != 0U;
    if ((requested & kStatusDriverOk) != 0U && (!base_ready || !features_ok)) {
        requested &= ~kStatusDriverOk;
    }
    status_ = requested;
}

bus::AccessResult VirtioMmioTransport::read(std::uint64_t offset,
                                            bus::AccessWidth width,
                                            bus::AccessType type) {
    static_cast<void>(type);
    const auto validation = validate_register_access(offset, width);
    if (!validation.ok()) {
        return validation;
    }

    std::lock_guard<std::mutex> lock{mutex_};
    const auto valid_queue = queue_selector_ < config_.queue_count;
    auto& queue = queues_[valid_queue ? queue_selector_ : 0U];
    switch (offset) {
        case kMagicValueOffset:
            return bus::AccessResult::success(kMagicValue);
        case kVersionOffset:
            return bus::AccessResult::success(kVersion);
        case kDeviceIdOffset:
            return bus::AccessResult::success(static_cast<std::uint32_t>(config_.device_id));
        case kVendorIdOffset:
            return bus::AccessResult::success(config_.vendor_id);
        case kDeviceFeaturesOffset:
            return bus::AccessResult::success(
                read_feature_page(offered_features_, device_features_selector_));
        case kQueueNumMaxOffset:
            return bus::AccessResult::success(valid_queue ? config_.queue_size_max : 0U);
        case kQueueNumOffset:
            return bus::AccessResult::success(valid_queue ? queue.size : 0U);
        case kQueueReadyOffset:
            return bus::AccessResult::success(valid_queue && queue.ready ? 1U : 0U);
        case kInterruptStatusOffset:
            return bus::AccessResult::success(interrupt_status_);
        case kStatusOffset:
            return bus::AccessResult::success(status_);
        case kQueueDescLowOffset:
            return bus::AccessResult::success(valid_queue ? low32(queue.descriptor_table) : 0U);
        case kQueueDescHighOffset:
            return bus::AccessResult::success(valid_queue ? high32(queue.descriptor_table) : 0U);
        case kQueueDriverLowOffset:
            return bus::AccessResult::success(valid_queue ? low32(queue.available_ring) : 0U);
        case kQueueDriverHighOffset:
            return bus::AccessResult::success(valid_queue ? high32(queue.available_ring) : 0U);
        case kQueueDeviceLowOffset:
            return bus::AccessResult::success(valid_queue ? low32(queue.used_ring) : 0U);
        case kQueueDeviceHighOffset:
            return bus::AccessResult::success(valid_queue ? high32(queue.used_ring) : 0U);
        case kConfigGenerationOffset:
            return bus::AccessResult::success(config_generation_);
        default:
            return bus::AccessResult::success(0U);
    }
}

bus::AccessResult VirtioMmioTransport::write(std::uint64_t offset,
                                             bus::AccessWidth width,
                                             std::uint64_t value,
                                             bus::AccessType type) {
    static_cast<void>(type);
    const auto validation = validate_register_access(offset, width);
    if (!validation.ok()) {
        return validation;
    }

    const auto word = static_cast<std::uint32_t>(value);
    std::lock_guard<std::mutex> lock{mutex_};
    const auto valid_queue = queue_selector_ < config_.queue_count;
    auto& queue = queues_[valid_queue ? queue_selector_ : 0U];
    switch (offset) {
        case kDeviceFeaturesSelOffset:
            device_features_selector_ = word;
            return bus::AccessResult::success();
        case kDriverFeaturesSelOffset:
            driver_features_selector_ = word;
            return bus::AccessResult::success();
        case kDriverFeaturesOffset:
            if (driver_features_selector_ == 0U) {
                write_low32(driver_features_, word);
            } else if (driver_features_selector_ == 1U) {
                write_high32(driver_features_, word);
            }
            return bus::AccessResult::success();
        case kQueueSelOffset:
            queue_selector_ = static_cast<std::uint16_t>(word);
            return bus::AccessResult::success();
        case kQueueNumOffset:
            if (valid_queue && !queue.ready) {
                queue.size = static_cast<std::uint16_t>(word);
            }
            return bus::AccessResult::success();
        case kQueueReadyOffset:
            if (valid_queue) {
                queue.ready = word != 0U && selected_queue_valid_locked();
            }
            return bus::AccessResult::success();
        case kQueueNotifyOffset:
            if ((status_ & kStatusDriverOk) != 0U && word < config_.queue_count
                && queues_[word].ready) {
                notifications_.push_back(static_cast<std::uint16_t>(word));
            }
            return bus::AccessResult::success();
        case kInterruptAckOffset:
            interrupt_status_ &= ~word;
            return bus::AccessResult::success();
        case kStatusOffset:
            write_status_locked(word);
            return bus::AccessResult::success();
        case kQueueDescLowOffset:
            if (valid_queue && !queue.ready) {
                write_low32(queue.descriptor_table, word);
            }
            return bus::AccessResult::success();
        case kQueueDescHighOffset:
            if (valid_queue && !queue.ready) {
                write_high32(queue.descriptor_table, word);
            }
            return bus::AccessResult::success();
        case kQueueDriverLowOffset:
            if (valid_queue && !queue.ready) {
                write_low32(queue.available_ring, word);
            }
            return bus::AccessResult::success();
        case kQueueDriverHighOffset:
            if (valid_queue && !queue.ready) {
                write_high32(queue.available_ring, word);
            }
            return bus::AccessResult::success();
        case kQueueDeviceLowOffset:
            if (valid_queue && !queue.ready) {
                write_low32(queue.used_ring, word);
            }
            return bus::AccessResult::success();
        case kQueueDeviceHighOffset:
            if (valid_queue && !queue.ready) {
                write_high32(queue.used_ring, word);
            }
            return bus::AccessResult::success();
        default:
            return bus::AccessResult::success();
    }
}

bus::AccessResult VirtioMmioTransport::compare_exchange(std::uint64_t offset,
                                                        bus::AccessWidth width,
                                                        std::uint64_t expected,
                                                        std::uint64_t desired,
                                                        bus::AccessType type) {
    static_cast<void>(expected);
    static_cast<void>(desired);
    static_cast<void>(type);
    return invalid_access(config_, offset, width, "VirtIO MMIO 不支持原子访问");
}

}  // namespace rvemu::devices
