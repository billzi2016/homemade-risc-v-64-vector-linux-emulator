// 文件职责：实现生产整机资源校验、设备注册和 raw 启动镜像装载。
// 边界：本文件不伪造来宾输出、不创建 TAP、不切换终端 Raw，也不运行 CPU 主循环。

#include "rvemu/runtime/machine.hpp"

#include "rvemu/bus/address_map.hpp"

#include <cstdint>
#include <memory>
#include <utility>

namespace rvemu::runtime {
namespace {

[[nodiscard]] MachineBuildResult failure(ExitCode code, std::string message) {
    return MachineBuildResult{nullptr, code, std::move(message)};
}

[[nodiscard]] devices::VirtioMmioConfig block_transport_config() noexcept {
    return devices::VirtioMmioConfig{bus::address_map::kVirtioBlock.name,
                                     bus::address_map::kVirtioBlock.base,
                                     bus::address_map::kVirtioBlock.size,
                                     devices::VirtioDeviceId::Block,
                                     0x554D'4552U,
                                     0U,
                                     1U,
                                     1U,
                                     8U};
}

}  // namespace

MachineBuildResult build_machine(const MachineConfig& config) {
    if (config.cli.net != "none") {
        return failure(ExitCode::Resource,
                       "当前 macOS 收尾档位只支持 --net none，不创建 TAP 或伪网络后端");
    }

    auto machine = std::make_unique<Machine>();
    machine->ram = std::make_shared<memory::PhysicalMemory>(
        bus::PhysicalAddress{config.boot_layout.ram_base},
        static_cast<std::size_t>(config.boot_layout.ram_size),
        bus::address_map::kDefaultRam.name);
    machine->clint = std::make_shared<devices::Clint>();
    machine->plic = std::make_shared<devices::Plic>();
    machine->uart = std::make_shared<devices::Uart16550>();
    machine->block_transport =
        std::make_shared<devices::VirtioMmioTransport>(block_transport_config());

    for (const auto& region : {std::static_pointer_cast<bus::AddressRegion>(machine->ram),
                               std::static_pointer_cast<bus::AddressRegion>(machine->clint),
                               std::static_pointer_cast<bus::AddressRegion>(machine->plic),
                               std::static_pointer_cast<bus::AddressRegion>(machine->uart),
                               std::static_pointer_cast<bus::AddressRegion>(machine->block_transport)}) {
        const auto registered = machine->bus.register_region(region);
        if (!registered.ok()) {
            return failure(ExitCode::Internal, "整机 MMIO/RAM 区域注册失败：" + registered.error.detail);
        }
    }

    const auto disk = machine->disk.open_existing(config.cli.disk_path, platform::DiskOpenMode::ReadWrite);
    if (!disk.ok()) {
        return failure(ExitCode::Resource, "无法打开真实磁盘镜像：" + disk.detail);
    }
    machine->block =
        std::make_unique<devices::VirtioBlockDevice>(*machine->block_transport, machine->disk);

    FdtConfig fdt;
    fdt.include_network = false;
    machine->boot_result = load_boot_images(machine->bus,
                                            machine->cpu,
                                            BootImagePaths{config.cli.bios_path, config.cli.kernel_path},
                                            config.boot_layout,
                                            fdt);
    if (!machine->boot_result.ok()) {
        return failure(ExitCode::ImageFormat, machine->boot_result.error);
    }

    return MachineBuildResult{std::move(machine), ExitCode::Success, {}};
}

}  // namespace rvemu::runtime
