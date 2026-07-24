// 文件职责：声明 VirtIO 1.x MMIO transport 的共享寄存器、状态机、feature 协商和队列配置。
// 边界：本模块不实现具体块/网设备请求，不读写宿主文件或 TAP，也不复制 virtqueue 描述符解析。
// 主要依赖：AddressRegion 提供 MMIO 分发，PLIC 只接收中断电平。
// 关键不变量：DRIVER_OK 前不接受队列通知；复位必须清除协商、队列运行态和中断原因。

#pragma once

#include "rvemu/bus/address_region.hpp"
#include "rvemu/devices/virtqueue.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <mutex>

namespace rvemu::devices {

class Plic;

enum class VirtioDeviceId : std::uint32_t {
    Block = 2U,
    Network = 1U,
};

/** 构造一个具体 VirtIO MMIO transport 的不可变设备身份和能力上限。 */
struct VirtioMmioConfig final {
    const char* name;
    std::uint64_t base;
    std::uint64_t size;
    VirtioDeviceId device_id;
    std::uint32_t vendor_id;
    std::uint64_t device_features;
    std::uint32_t interrupt_source;
    std::uint16_t queue_count;
    std::uint16_t queue_size_max;
};

/**
 * VirtIO 1.x MMIO 公共 transport。
 *
 * 具体块/网设备后续通过 pending 通知消费队列；本类只负责规范寄存器、feature
 * 协商、队列 ready 条件、中断 ACK 和复位代际。
 */
class VirtioMmioTransport final : public bus::AddressRegion {
   public:
    static constexpr std::uint32_t kMagicValue = 0x7472'6976U;
    static constexpr std::uint32_t kVersion = 2U;
    static constexpr std::uint64_t kFeatureVersion1 = 1ULL << 32U;

    explicit VirtioMmioTransport(VirtioMmioConfig config);

    VirtioMmioTransport(const VirtioMmioTransport&) = delete;
    VirtioMmioTransport& operator=(const VirtioMmioTransport&) = delete;
    VirtioMmioTransport(VirtioMmioTransport&&) = delete;
    VirtioMmioTransport& operator=(VirtioMmioTransport&&) = delete;

    /** 将 InterruptStatus 的非零状态投影为 PLIC source 电平；不解释 claim/complete。 */
    void synchronize(Plic& plic) const noexcept;

    /** 取出一个 DRIVER_OK 后收到的队列通知；返回 false 表示当前没有待处理队列。 */
    [[nodiscard]] bool pop_notification(std::uint16_t& queue_index) noexcept;

    /** 设备请求完成后设置 used-buffer interrupt status；PLIC 电平由 synchronize 投影。 */
    void raise_used_buffer_interrupt() noexcept;

    /** 为共享描述符解析器导出指定队列的不可变布局快照。 */
    [[nodiscard]] VirtqueueLayout queue_layout(std::uint16_t queue_index) const noexcept;

    /** 返回 transport 配置代际；设备层用它隔离复位前后的队列运行态。 */
    [[nodiscard]] std::uint64_t generation() const noexcept;

    [[nodiscard]] bus::AccessResult read(std::uint64_t offset,
                                         bus::AccessWidth width,
                                         bus::AccessType type) override;
    [[nodiscard]] bus::AccessResult write(std::uint64_t offset,
                                          bus::AccessWidth width,
                                          std::uint64_t value,
                                          bus::AccessType type) override;
    /** VirtIO MMIO transport 不支持 LR/SC/AMO；原子访问作为设备访问错误返回。 */
    [[nodiscard]] bus::AccessResult compare_exchange(std::uint64_t offset,
                                                     bus::AccessWidth width,
                                                     std::uint64_t expected,
                                                     std::uint64_t desired,
                                                     bus::AccessType type) override;

   private:
    struct QueueState final {
        std::uint16_t size{0U};
        bool ready{false};
        std::uint64_t descriptor_table{0U};
        std::uint64_t available_ring{0U};
        std::uint64_t used_ring{0U};
    };

    /** 验证 32 位自然对齐 MMIO 访问，避免跨寄存器副作用。 */
    [[nodiscard]] bus::AccessResult validate_register_access(std::uint64_t offset,
                                                             bus::AccessWidth width) const;
    /** 设备复位清除所有运行态，但保留不可变身份和 feature 上限。 */
    void reset_locked() noexcept;
    /** 根据当前 selected page 返回 32 位 feature 页。 */
    [[nodiscard]] std::uint32_t read_feature_page(std::uint64_t features,
                                                  std::uint32_t selector) const noexcept;
    /** 判断队列大小和三段地址是否满足 transport ready 前置条件。 */
    [[nodiscard]] bool selected_queue_valid_locked() const noexcept;
    /** 按 VirtIO 1.x 顺序和 feature 子集规则提交状态寄存器写入。 */
    void write_status_locked(std::uint32_t value) noexcept;

    VirtioMmioConfig config_;
    std::uint64_t offered_features_{0U};
    std::uint64_t driver_features_{0U};
    std::uint32_t device_features_selector_{0U};
    std::uint32_t driver_features_selector_{0U};
    std::uint16_t queue_selector_{0U};
    std::uint32_t interrupt_status_{0U};
    std::uint32_t status_{0U};
    std::uint32_t config_generation_{0U};
    std::uint64_t generation_{0U};
    std::array<QueueState, 8U> queues_{};
    std::deque<std::uint16_t> notifications_{};
    mutable std::mutex mutex_;
};

}  // namespace rvemu::devices
