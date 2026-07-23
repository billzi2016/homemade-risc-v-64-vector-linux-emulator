// 文件职责：声明生产整机资源组装入口，连接 RAM、CPU、中断、UART 和 VirtIO-Blk。
// 边界：本模块不切换终端 Raw、不进入事件循环、不实现 macOS 网络，也不下载或创建镜像。
// 主要依赖：CLI/boot 配置、统一地址图、真实设备类和 DiskBackend。
// 关键不变量：所有宿主资源和设备注册成功后才装载 BIOS/kernel/FDT；macOS 非 none 网络被拒绝。

#pragma once

#include "rvemu/bus/bus.hpp"
#include "rvemu/core/cpu.hpp"
#include "rvemu/devices/clint.hpp"
#include "rvemu/devices/plic.hpp"
#include "rvemu/devices/uart16550.hpp"
#include "rvemu/devices/virtio_block.hpp"
#include "rvemu/devices/virtio_mmio.hpp"
#include "rvemu/memory/physical_memory.hpp"
#include "rvemu/runtime/boot.hpp"
#include "rvemu/runtime/cli.hpp"

#include <memory>
#include <optional>
#include <string>

namespace rvemu::runtime {

struct MachineConfig final {
    CliOptions cli{};
    BootLayout boot_layout{};
};

struct MachineBuildResult final {
    std::unique_ptr<struct Machine> machine{};
    ExitCode exit_code{ExitCode::Success};
    std::string error{};

    [[nodiscard]] bool ok() const noexcept {
        return machine != nullptr;
    }
};

struct Machine final {
    bus::Bus bus{};
    core::Cpu cpu{bus};
    std::shared_ptr<memory::PhysicalMemory> ram{};
    std::shared_ptr<devices::Clint> clint{};
    std::shared_ptr<devices::Plic> plic{};
    std::shared_ptr<devices::Uart16550> uart{};
    std::shared_ptr<devices::VirtioMmioTransport> block_transport{};
    platform::DiskBackend disk{};
    std::unique_ptr<devices::VirtioBlockDevice> block{};
    BootResult boot_result{};
};

/** 构造 macOS `--net none` 整机实例并装载启动镜像；失败时不进入运行循环。 */
[[nodiscard]] MachineBuildResult build_machine(const MachineConfig& config);

}  // namespace rvemu::runtime
