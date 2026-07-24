// 文件职责：验证 CLI、FDT 和启动装载模块的真实生产路径与错误拒绝。
// 边界：测试只创建极小 raw 镜像，不生成 ext4、不运行 OpenSBI/Linux，也不冒充系统验收。

#include "rvemu/bus/address_map.hpp"
#include "rvemu/bus/bus.hpp"
#include "rvemu/core/cpu.hpp"
#include "rvemu/memory/physical_memory.hpp"
#include "rvemu/runtime/boot.hpp"
#include "rvemu/runtime/cli.hpp"
#include "rvemu/runtime/fdt.hpp"
#include "rvemu/runtime/host_signal.hpp"
#include "rvemu/runtime/machine.hpp"
#include "rvemu/runtime/runner.hpp"

#include <cstdint>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <termios.h>
#include <util.h>
#include <vector>
#include <unistd.h>

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
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error("无法写入测试镜像文件");
    }
}

void test_cli(int& failures) {
    const char* ok_args[] = {"rvemu",
                             "--bios",
                             "artifacts/firmware/fw_jump.bin",
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

    const char* duplicate[] = {
        "rvemu", "--bios", "b", "--bios", "b2", "--kernel", "k", "--disk", "d"};
    expect(!rvemu::runtime::parse_cli(9, const_cast<char**>(duplicate)).ok(),
           "重复参数必须报错",
           failures);

    const char* bad_format[] = {
        "rvemu", "--bios", "b", "--kernel", "k", "--disk", "d", "--kernel-format", "elf"};
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
    expect(blob_text.find("rootwait") != std::string::npos,
           "FDT chosen bootargs 必须等待 VirtIO-Blk root 出现",
           failures);
    expect(
        blob_text.find("riscv,sv39") != std::string::npos, "FDT CPU 节点必须声明 Sv39", failures);
    expect(blob_text.find("riscv,isa-base") != std::string::npos
               && blob_text.find("riscv,isa-extensions") != std::string::npos,
           "FDT CPU 节点必须提供新版 RISC-V ISA 属性",
           failures);
    expect(
        blob_text.find("cpu-map") != std::string::npos, "FDT 必须声明单 Hart CPU 拓扑", failures);
    expect(blob_text.find("sifive,clint0") != std::string::npos
               && blob_text.find("riscv,plic0") != std::string::npos,
           "FDT 中断控制器 compatible 必须兼容 OpenSBI/Linux virt 约定",
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

    config.cli.net = "none";
    config.cli.disk_path = (temp / "missing.ext4").string();
    const auto missing_disk = rvemu::runtime::build_machine(config);
    expect(!missing_disk.ok() && missing_disk.exit_code == rvemu::runtime::ExitCode::Resource,
           "缺失磁盘镜像必须映射为资源错误",
           failures);

    config.cli.disk_path = disk.string();
    config.cli.bios_path = (temp / "missing-opensbi.raw").string();
    const auto missing_bios = rvemu::runtime::build_machine(config);
    expect(!missing_bios.ok() && missing_bios.exit_code == rvemu::runtime::ExitCode::ImageFormat,
           "缺失 BIOS 必须映射为镜像错误且不进入运行循环",
           failures);
}

void test_host_signal_stop_flag(int& failures) {
    expect(rvemu::runtime::install_host_signal_handlers().ok(),
           "宿主信号 handler 必须安装成功",
           failures);
    expect(!rvemu::runtime::host_stop_requested(), "安装 handler 必须清除旧停止请求", failures);
    raise(SIGTERM);
    expect(rvemu::runtime::host_stop_requested(), "SIGTERM 必须只设置停止请求标志", failures);
    rvemu::runtime::clear_host_stop_request();
    expect(!rvemu::runtime::host_stop_requested(), "停止请求必须可由主线程清除", failures);
    expect(rvemu::runtime::restore_host_signal_handlers().ok(),
           "宿主信号 handler 必须可恢复",
           failures);
}

class PtyPair final {
   public:
    PtyPair() {
        if (openpty(&master_, &slave_, nullptr, nullptr, nullptr) != 0) {
            throw std::runtime_error("创建 runner 伪终端失败");
        }
    }

    ~PtyPair() {
        if (master_ >= 0) {
            static_cast<void>(close(master_));
        }
        if (slave_ >= 0) {
            static_cast<void>(close(slave_));
        }
    }

    [[nodiscard]] int slave() const noexcept {
        return slave_;
    }

   private:
    int master_{-1};
    int slave_{-1};
};

void test_runner_limited_loop(int& failures) {
    const auto temp = std::filesystem::path{"build"} / "runner-test";
    std::filesystem::create_directories(temp);
    const auto bios = temp / "opensbi.raw";
    const auto kernel = temp / "Image.raw";
    const auto disk = temp / "rootfs.ext4";
    write_file(bios, {0x13U, 0x00U, 0x00U, 0x00U});
    write_file(kernel, {0x13U, 0x00U, 0x00U, 0x00U});
    write_file(disk, std::vector<std::uint8_t>(512U, 0U));

    rvemu::runtime::MachineConfig config;
    config.cli.bios_path = bios.string();
    config.cli.kernel_path = kernel.string();
    config.cli.disk_path = disk.string();
    config.cli.net = "none";
    config.boot_layout.ram_size = 0x0040'0000U;
    config.boot_layout.fdt_address = config.boot_layout.ram_base + 0x0030'0000U;
    config.boot_layout.fdt_reserved_size = 0x0002'0000U;
    auto built = rvemu::runtime::build_machine(config);
    expect(built.ok(), "runner 测试整机必须组装成功", failures);
    if (!built.ok()) {
        return;
    }

    PtyPair pty;
    rvemu::platform::TerminalBackend terminal{pty.slave(), pty.slave()};
    expect(terminal.activate_raw().ok(), "runner 测试终端必须进入 Raw 模式", failures);
    rvemu::runtime::clear_host_stop_request();
    const auto result =
        rvemu::runtime::run_machine(*built.machine, terminal, rvemu::runtime::RunOptions{1U, 3U});
    expect(result.ok() && result.iterations == 3U,
           "runner 必须复用唯一事件循环并遵守测试迭代上限",
           failures);
    expect(terminal.restore().ok(), "runner 测试终端必须恢复", failures);
}

void test_runner_terminal_error_diagnostic(int& failures) {
    const auto temp = std::filesystem::path{"build"} / "runner-diagnostic-test";
    std::filesystem::create_directories(temp);
    const auto bios = temp / "opensbi.raw";
    const auto kernel = temp / "Image.raw";
    const auto disk = temp / "rootfs.ext4";
    write_file(bios, {0x13U, 0x00U, 0x00U, 0x00U});
    write_file(kernel, {0x13U, 0x00U, 0x00U, 0x00U});
    write_file(disk, std::vector<std::uint8_t>(512U, 0U));

    rvemu::runtime::MachineConfig config;
    config.cli.bios_path = bios.string();
    config.cli.kernel_path = kernel.string();
    config.cli.disk_path = disk.string();
    config.cli.net = "none";
    config.boot_layout.ram_size = 0x0040'0000U;
    config.boot_layout.fdt_address = config.boot_layout.ram_base + 0x0030'0000U;
    config.boot_layout.fdt_reserved_size = 0x0002'0000U;
    auto built = rvemu::runtime::build_machine(config);
    expect(built.ok(), "诊断测试整机必须组装成功", failures);
    if (!built.ok()) {
        return;
    }

    PtyPair pty;
    rvemu::platform::TerminalBackend terminal{pty.slave(), -1};
    expect(terminal.activate_raw().ok(), "诊断测试终端必须进入 Raw 模式", failures);
    expect(built.machine->uart
               ->write(rvemu::devices::Uart16550::kRbrThrDllOffset,
                       rvemu::bus::AccessWidth::Byte,
                       static_cast<std::uint8_t>('E'),
                       rvemu::bus::AccessType::Store)
               .ok(),
           "诊断测试必须预置 UART TX 字节",
           failures);

    rvemu::runtime::clear_host_stop_request();
    const auto result =
        rvemu::runtime::run_machine(*built.machine, terminal, rvemu::runtime::RunOptions{1U, 3U});
    expect(!result.ok() && result.exit_code == rvemu::runtime::ExitCode::RuntimeIo,
           "终端输出错误必须映射为运行期 I/O 错误",
           failures);
    expect(result.error.find("device=terminal-output") != std::string::npos,
           "诊断必须包含设备名",
           failures);
    expect(result.error.find("pc=0x") != std::string::npos, "诊断必须包含 PC", failures);
    expect(result.error.find("priv=M") != std::string::npos, "诊断必须包含特权级", failures);
    expect(result.error.find("trap_cause=") != std::string::npos,
           "诊断必须包含 trap cause 字段",
           failures);
    expect(result.error.find("errno=" + std::to_string(EBADF)) != std::string::npos,
           "诊断必须包含宿主 errno",
           failures);
    expect(terminal.restore().ok(), "诊断测试终端必须恢复", failures);
}

}  // namespace

int main() {
    int failures = 0;
    try {
        test_cli(failures);
        test_fdt(failures);
        test_boot_load(failures);
        test_machine_build(failures);
        test_host_signal_stop_flag(failures);
        test_runner_limited_loop(failures);
        test_runner_terminal_error_diagnostic(failures);
    } catch (const std::exception& exception) {
        std::cerr << "启动运行测试基础设施失败：" << exception.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
