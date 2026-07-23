// 文件职责：声明 VirtIO split virtqueue 的共享描述符链解析与安全请求视图。
// 边界：本模块不执行块设备、网卡或宿主 I/O；解析阶段只读取来宾内存并验证结构。
// 主要依赖：统一物理总线 DMA 读取入口。
// 关键不变量：整条链验证成功前不得产生设备 DMA 写、used ring 提交或宿主 I/O 副作用。

#pragma once

#include "rvemu/bus/bus.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace rvemu::devices {

enum class VirtqueueDescriptorDirection : std::uint8_t {
    DeviceReads,
    DeviceWrites,
    Any,
};

enum class VirtqueueParseErrorCode : std::uint8_t {
    None,
    QueueNotReady,
    IndexOutOfRange,
    ChainLoop,
    ChainTooLong,
    DirectionMismatch,
    AddressOverflow,
    DmaReadFailure,
    IndirectUnsupported,
};

enum class VirtqueueRingErrorCode : std::uint8_t {
    None,
    QueueNotReady,
    TooManyPending,
    DmaReadFailure,
    DmaWriteFailure,
};

/** 经过验证的描述符片段；方向以设备视角表达，addr/length 是来宾物理 DMA 范围。 */
struct VirtqueueSegment final {
    bus::PhysicalAddress address{};
    std::uint32_t length{0U};
    bool device_writes{false};
};

/** 描述符链解析结果；失败时 segments 不可作为请求执行依据。 */
struct VirtqueueParseResult final {
    VirtqueueParseErrorCode code{VirtqueueParseErrorCode::None};
    std::string detail{};
    std::vector<VirtqueueSegment> segments{};

    [[nodiscard]] bool ok() const noexcept {
        return code == VirtqueueParseErrorCode::None;
    }
};

/** 描述一个已由 VirtIO MMIO transport 配置并 ready 的 split queue 内存布局。 */
struct VirtqueueLayout final {
    std::uint16_t size{0U};
    bus::PhysicalAddress descriptor_table{};
    bus::PhysicalAddress available_ring{};
    bus::PhysicalAddress used_ring{};
    bool ready{false};
    bool indirect_enabled{false};
    bool event_idx_enabled{false};
};

/** 描述一次 available ring 观察到的可处理 head；available_index 保留完整 16 位回绕计数。 */
struct VirtqueueAvailableResult final {
    VirtqueueRingErrorCode code{VirtqueueRingErrorCode::None};
    std::uint16_t available_index{0U};
    std::uint16_t head{0U};
    std::uint16_t pending_count{0U};

    [[nodiscard]] bool ok() const noexcept {
        return code == VirtqueueRingErrorCode::None;
    }
};

/** 计算 VirtIO 规定的 16 位回绕差值，不把计数器提前截断成 ring 槽位。 */
[[nodiscard]] constexpr std::uint16_t virtqueue_index_delta(std::uint16_t newer,
                                                            std::uint16_t older) noexcept {
    return static_cast<std::uint16_t>(newer - older);
}

/** 将单调 16 位 idx 映射到 ring 数组槽位；调用方必须先验证 queue size 非零。 */
[[nodiscard]] constexpr std::uint16_t virtqueue_ring_slot(std::uint16_t index,
                                                          std::uint16_t queue_size) noexcept {
    return static_cast<std::uint16_t>(index % queue_size);
}

/**
 * VirtIO EVENT_IDX 通知抑制公式。
 *
 * 当且仅当设备/驱动协商 EVENT_IDX 后调用；old_index 是本次提交前 idx，new_index 是发布后 idx，
 * event_index 是对端写入的事件阈值。公式保留完整 16 位回绕语义。
 */
[[nodiscard]] constexpr bool virtqueue_event_needed(std::uint16_t event_index,
                                                    std::uint16_t new_index,
                                                    std::uint16_t old_index) noexcept {
    return static_cast<std::uint16_t>(new_index - event_index - 1U)
           < static_cast<std::uint16_t>(new_index - old_index);
}

/**
 * split virtqueue 的设备侧运行态，维护 last_available_idx、used_idx 与队列代际。
 *
 * 本类只处理公共 ring 索引和提交顺序，不解释描述符链内容。复位会递增 generation，
 * 调用方可用它隔离旧异步请求结果。
 */
class VirtqueueRuntimeState final {
   public:
    /** 复位设备侧运行态；initial_index 用于测试或设备恢复时对齐驱动 idx。 */
    void reset(std::uint16_t initial_index = 0U) noexcept;

    /** 读取 available idx 和下一个 head；成功后推进 last_available_idx 一个条目。 */
    [[nodiscard]] VirtqueueAvailableResult consume_available(bus::Bus& bus,
                                                             const VirtqueueLayout& layout);

    /** 先写 used element，再发布 used idx；失败时返回错误且不推进 used_idx。 */
    [[nodiscard]] VirtqueueRingErrorCode publish_used(bus::Bus& bus,
                                                      const VirtqueueLayout& layout,
                                                      std::uint32_t id,
                                                      std::uint32_t length);

    /** 按协商 feature 判断是否需要通知驱动；EVENT_IDX 未协商时使用 avail flags。 */
    [[nodiscard]] VirtqueueRingErrorCode driver_notification_needed(bus::Bus& bus,
                                                                    const VirtqueueLayout& layout,
                                                                    bool& needed) const;

    [[nodiscard]] std::uint16_t last_available_index() const noexcept {
        return last_available_idx_;
    }

    [[nodiscard]] std::uint16_t used_index() const noexcept {
        return used_idx_;
    }

    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }

   private:
    std::uint16_t last_available_idx_{0U};
    std::uint16_t used_idx_{0U};
    std::uint64_t generation_{0U};
};

/**
 * 从直接描述符表解析一条链，统一执行索引、循环、方向和地址溢出检查。
 *
 * expected 可以为空，表示只验证结构不绑定设备请求布局；非空时每个直接描述符必须有
 * 对应期望方向。函数只使用总线 DMA 读描述符元数据，不执行数据 DMA。
 */
[[nodiscard]] VirtqueueParseResult parse_descriptor_chain(
    bus::Bus& bus,
    const VirtqueueLayout& layout,
    std::uint16_t head,
    const std::vector<VirtqueueDescriptorDirection>& expected = {});

}  // namespace rvemu::devices
