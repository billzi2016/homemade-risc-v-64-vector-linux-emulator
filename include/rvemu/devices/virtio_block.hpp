// 文件职责：声明 VirtIO-Blk 请求处理器，连接公共 MMIO transport、split virtqueue 和宿主磁盘后端。
// 边界：本模块不打开磁盘路径、不实现文件生命周期，也不复制 VirtIO transport 寄存器状态机。
// 主要依赖：VirtioMmioTransport、VirtqueueRuntimeState、Bus DMA 和 platform::DiskBackend。
// 关键不变量：完整验证描述符链和磁盘范围后才执行磁盘 I/O；完成顺序为 status、used element、used
// idx、中断。

#pragma once

#include "rvemu/devices/virtio_mmio.hpp"
#include "rvemu/platform/disk_backend.hpp"

#include <cstdint>
#include <vector>

namespace rvemu::devices {

enum class VirtioBlockProcessStatus : std::uint8_t {
    NoNotification,
    Completed,
    QueueError,
};

class VirtioBlockDevice final {
   public:
    static constexpr std::uint32_t kRequestIn = 0U;
    static constexpr std::uint32_t kRequestOut = 1U;
    static constexpr std::uint8_t kStatusOk = 0U;
    static constexpr std::uint8_t kStatusIoError = 1U;
    static constexpr std::uint8_t kStatusUnsupported = 2U;

    VirtioBlockDevice(VirtioMmioTransport& transport, platform::DiskBackend& disk) noexcept;

    /** 处理一个已通知请求；无通知时不产生副作用。 */
    [[nodiscard]] VirtioBlockProcessStatus process_one(bus::Bus& bus);

   private:
    struct RequestHeader final {
        std::uint32_t type{0U};
        std::uint64_t sector{0U};
    };

    [[nodiscard]] bool read_header(bus::Bus& bus,
                                   const VirtqueueSegment& segment,
                                   RequestHeader& header) const;
    [[nodiscard]] bool copy_from_guest(bus::Bus& bus,
                                       const VirtqueueSegment& segment,
                                       std::vector<std::uint8_t>& output) const;
    [[nodiscard]] bool copy_to_guest(bus::Bus& bus,
                                     const VirtqueueSegment& segment,
                                     const std::vector<std::uint8_t>& input) const;
    [[nodiscard]] bool write_status(bus::Bus& bus,
                                    const VirtqueueSegment& segment,
                                    std::uint8_t status) const;
    void synchronize_queue_generation();

    VirtioMmioTransport& transport_;
    platform::DiskBackend& disk_;
    VirtqueueRuntimeState queue_runtime_{};
    std::uint64_t observed_transport_generation_{0U};
};

}  // namespace rvemu::devices
