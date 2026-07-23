// 文件职责：验证 CLI、FDT 和启动装载模块的真实生产路径与错误拒绝。
// 边界：测试只创建极小 raw 镜像，不生成 ext4、不运行 OpenSBI/Linux，也不冒充系统验收。

#include "rvemu/bus/address_map.hpp"
#include "rvemu/bus/bus.hpp"
#include "rvemu/core/cpu.hpp"
#include "rvemu/memory/physical_memory.hpp"
#include "rvemu/runtime/boot.hpp"
#include "rvemu/runtime/cli.hpp"
#include "rvemu/runtime/fdt.hpp"
#include "rvemu/runtime/machine.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message, int& failures) {
    if (!condition) {
        ++failures;
        std::cerr << "失败：" << message << '\n';
    }
}

[[nodiscard]] std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U)
           | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U)
           | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U)
           | static_cast<std::uint32_t>(bytes[offset + 3U]);
}

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error("无法创建测试镜像文件");
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error("无法写入测试镜像文件");
    }
}

void test_cli(int& failures) {
    const char* ok_args[] = {"rvemu",
                             "--bios",
                             "artifacts/firmware/opensbi.bin",
                             "--kernel",
                             "artifacts/kernel/Image",
                             "--disk",
                             "artifacts/disk/rootfs.ext4",
                             "--net",
                             "none"};
    const auto ok = rvemu::runtime::parse_cli(9, const_cast<char**>(ok_args));
    expect(ok.ok() && ok.options->net == "none", "合法 CLI 必须解析成功", failures);

    const char* missing[] = {"rvemu", "--bios", "b", "--kernel", "k"};
    expect(!rvemu::runtime::parse_cli(5, const_cast<char**>(missing)).ok(),
           "缺少 disk 必须报错",
           failures);

    const char* duplicate[] = {"rvemu", "--bios", "b", "--bios", "b2", "--kernel", "k", "--disk", "d"};
    expect(!rvemu::runtime::parse_cli(9, const_cast<char**>(duplicate)).ok(),
           "重复参数必须报错",
           failures);

    const char* bad_format[] = {"rvemu", "--bios", "b", "--kernel", "k", "--disk", "d", "--kernel-format", "elf"};
    expect(!rvemu::runtime::parse_cli(9, const_cast<char**>(bad_format)).ok(),
           "不支持格式必须报错，不能按文件名猜测",
           failures);
}

void test_fdt(int& failures) {
    const auto fdt = rvemu::runtime::build_machine_fdt({});
    expect(fdt.ok() && fdt.blob.size() > 256U, "FDT 生成必须成功且非空", failures);
    expect(read_be32(fdt.blob, 0U) == 0xD00D'FEEDU, "FDT magic 必须正确", failures);
    const std::string blob_text{fdt.blob.begin(), fdt.blob.end()};
    expect(blob_text.find("virtio,mmio") != std::string::npos,
           "FDT 必须包含 VirtIO MMIO compatible 字符串",
           failures);
    expect(blob_text.find("root=/dev/vda") != std::string::npos,
           "FDT chosen bootargs 必须指向 VirtIO-Blk root",
           failures);
    expect(blob_text.find("riscv,sv39") != std::string::npos,
           "FDT CPU 节点必须声明 Sv39",
           failures);
    expect(blob_text.find("10002000") == std::string::npos,
           "macOS --net none 默认 FDT 不得暴露 VirtIO-Net 节点",
           failures);
}

void test_boot_load(int& failures) {
    const auto temp = std::filesystem::path{"build"} / "boot-runtime-test";
    std::filesystem::create_directories(temp);
    const auto bios = temp / "opensbi.raw";
    const auto kernel = temp / "Image.raw";
    write_file(bios, {0x13U, 0x00U, 0x00U, 0x00U});
    write_file(kernel, {0x6FU, 0x00U, 0x00U, 0x00U});

    rvemu::bus::Bus bus;
    auto ram = std::make_shared<rvemu::memory::PhysicalMemory>(
        rvemu::bus::PhysicalAddress{rvemu::bus::address_map::kDefaultRam.base},
        0x0040'0000U,
        "boot-test-ram");
    if (!bus.register_region(ram).ok()) {
        throw std::runtime_error("RAM 注册失败");
    }
    rvemu::core::Cpu cpu{bus};
    rvemu::runtime::BootLayout layout;
    layout.ram_size = 0x0040'0000U;
    layout.fdt_address = layout.ram_base + 0x0030'0000U;
    layout.fdt_reserved_size = 0x0002'0000U;
    const auto loaded = rvemu::runtime::load_boot_images(
        bus, cpu, rvemu::runtime::BootImagePaths{bios, kernel}, layout, {});
    expect(loaded.ok() && loaded.bios_size == 4U && loaded.kernel_size == 4U,
           "raw BIOS/kernel/FDT 必须装载成功",
           failures);
    expect(cpu.state().program_counter() == layout.bios_load_address,
           "CPU PC 必须指向 BIOS 入口",
           failures);
    expect(cpu.state().integer(10U) == 0U && cpu.state().integer(11U) == layout.fdt_address,
           "CPU a0/a1 必须符合 RISC-V boot ABI",
           failures);

    layout.kernel_load_address = layout.bios_load_address + 2U;
    const auto rejected = rvemu::runtime::load_boot_images(
        bus, cpu, rvemu::runtime::BootImagePaths{bios, kernel}, layout, {});
    expect(!rejected.ok(), "BIOS/kernel 范围重叠必须拒绝", failures);
}

void test_machine_build(int& failures) {
    const auto temp = std::filesystem::path{"build"} / "machine-build-test";
    std::filesystem::create_directories(temp);
    const auto bios = temp / "opensbi.raw";
    const auto kernel = temp / "Image.raw";
    const auto disk = temp / "rootfs.ext4";
    write_file(bios, {0x13U, 0x00U, 0x00U, 0x00U});
    write_file(kernel, {0x6FU, 0x00U, 0x00U, 0x00U});
    write_file(disk, std::vector<std::uint8_t>(512U, 0x5AU));

    rvemu::runtime::MachineConfig config;
    config.cli.bios_path = bios.string();
    config.cli.kernel_path = kernel.string();
    config.cli.disk_path = disk.string();
    config.cli.net = "none";
    config.boot_layout.ram_size = 0x0040'0000U;
    config.boot_layout.fdt_address = config.boot_layout.ram_base + 0x0030'0000U;
    config.boot_layout.fdt_reserved_size = 0x0002'0000U;
    const auto built = rvemu::runtime::build_machine(config);
    expect(built.ok(), "macOS --net none 整机资源必须组装成功", failures);
    if (built.ok()) {
        expect(built.machine->bus.region_count() == 5U,
               "macOS --net none 只注册 RAM/CLINT/PLIC/UART/VirtIO-Blk",
               failures);
        expect(built.machine->cpu.state().program_counter() == config.boot_layout.bios_load_address,
               "整机组装后 CPU PC 必须指向 BIOS",
               failures);
        expect(built.machine->disk.capacity_sectors() == 1U,
               "整机组装必须打开并校验真实磁盘镜像",
               failures);
    }

    config.cli.net = "tap0";
    const auto rejected = rvemu::runtime::build_machine(config);
    expect(!rejected.ok() && rejected.exit_code == rvemu::runtime::ExitCode::Resource,
           "macOS 档位必须拒绝非 none 网络",
           failures);
}

}  // namespace

int main() {
    int failures = 0;
    try {
        test_cli(failures);
        test_fdt(failures);
        test_boot_load(failures);
        test_machine_build(failures);
    } catch (const std::exception& exception) {
        std::cerr << "启动运行测试基础设施失败：" << exception.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
