// 文件职责：运行 ACT4 生成的单个自校验 ELF，并从标准 tohost 通道报告真实 DUT 结果。
// 边界：该程序只编排现有 CPU/Bus/RAM，不包含参考模型、替代译码器或固定通过逻辑。

#include "rvemu/bus/access.hpp"
#include "rvemu/bus/address_range.hpp"
#include "rvemu/bus/bus.hpp"
#include "rvemu/conformance/elf_loader.hpp"
#include "rvemu/core/cpu.hpp"
#include "rvemu/memory/physical_memory.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

constexpr std::uint64_t kRamBase = 0x8000'0000ULL;
constexpr std::size_t kRamSize = 64U * 1024U * 1024U;
constexpr std::uint64_t kDefaultInstructionLimit = 20'000'000ULL;
constexpr std::uint64_t kHtifConsoleCommand = 0x0101'0000ULL;

struct Options final {
    std::filesystem::path elf{};
    std::uint64_t instruction_limit{kDefaultInstructionLimit};
};

[[nodiscard]] bool parse_positive(std::string_view text, std::uint64_t& value) noexcept {
    std::uint64_t parsed = 0U;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed == 0U) {
        return false;
    }
    value = parsed;
    return true;
}

[[nodiscard]] bool parse_options(int argc, char** argv, Options& options) {
    if (argc < 2) {
        return false;
    }
    options.elf = argv[1];
    for (int index = 2; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument != "--max-instructions" || index + 1 >= argc) {
            return false;
        }
        ++index;
        if (!parse_positive(argv[index], options.instruction_limit)) {
            return false;
        }
    }
    return true;
}

void print_usage(const char* executable) {
    std::cerr << "用法：" << executable
              << " <test.elf> [--max-instructions <正整数>]\n";
}

}  // namespace

int main(int argc, char** argv) {
    Options options{};
    if (!parse_options(argc, argv, options)) {
        print_usage(argv[0]);
        return 2;
    }

    rvemu::bus::Bus bus;
    auto ram = std::make_shared<rvemu::memory::PhysicalMemory>(
        rvemu::bus::PhysicalAddress{kRamBase}, kRamSize, "act4-ram");
    const auto registration = bus.register_region(ram);
    if (!registration.ok()) {
        std::cerr << "无法注册 ACT4 RAM：" << registration.error.detail << '\n';
        return 2;
    }
    const auto ram_range = rvemu::bus::AddressRange::create(
        rvemu::bus::PhysicalAddress{kRamBase}, kRamSize);
    if (!ram_range.has_value()) {
        std::cerr << "内部错误：ACT4 RAM 范围无效\n";
        return 2;
    }

    const auto loaded = rvemu::conformance::load_elf64_riscv(
        options.elf, bus, *ram_range);
    if (!loaded.ok()) {
        std::cerr << "ELF 装载失败：" << loaded.error << '\n';
        return 2;
    }

    rvemu::core::Cpu cpu{bus};
    cpu.state().reset(loaded.image->entry_point);
    std::uint64_t delivered_traps = 0U;

    for (std::uint64_t step_count = 0U; step_count < options.instruction_limit; ++step_count) {
        const auto step = cpu.step();
        if (step.trap.has_value()) {
            static_cast<void>(cpu.take_trap(*step.trap));
            ++delivered_traps;
        }

        const auto status = bus.read(
            rvemu::bus::PhysicalAddress{loaded.image->tohost_address},
            rvemu::bus::AccessWidth::DoubleWord,
            rvemu::bus::AccessType::Load);
        if (!status.ok()) {
            std::cerr << "读取 tohost 失败：" << status.error.detail << '\n';
            return 2;
        }
        if (status.value == 1U) {
            std::cout << "RVCP-SUMMARY: TEST PASSED - ELF \""
                      << options.elf.string() << "\"\n";
            return 0;
        }
        if (status.value == 3U) {
            std::cerr << "RVCP-SUMMARY: TEST FAILED - ELF \""
                      << options.elf.string() << "\"，PC=0x" << std::hex
                      << cpu.state().program_counter() << std::dec
                      << "，已送达 Trap=" << delivered_traps << '\n';
            return 1;
        }

        // ACT4 的 HTIF 字符命令由高 32 位设备/命令号和低字节字符组成。消费后清零，
        // 既允许连续输出相同字符，也避免把诊断输出误判成结束状态。
        if ((status.value >> 32U) == kHtifConsoleCommand) {
            std::cout.put(static_cast<char>(status.value & 0xFFU));
            std::cout.flush();
            const auto cleared = bus.write(
                rvemu::bus::PhysicalAddress{loaded.image->tohost_address},
                rvemu::bus::AccessWidth::DoubleWord,
                0U,
                rvemu::bus::AccessType::Initialization);
            if (!cleared.ok()) {
                std::cerr << "清除 HTIF 控制字失败：" << cleared.error.detail << '\n';
                return 2;
            }
        }
    }

    std::cerr << "RVCP-SUMMARY: TEST TIMEOUT - ELF \"" << options.elf.string()
              << "\"，指令上限=" << options.instruction_limit << "，PC=0x" << std::hex
              << cpu.state().program_counter() << std::dec
              << "，已送达 Trap=" << delivered_traps << '\n';
    return 2;
}
