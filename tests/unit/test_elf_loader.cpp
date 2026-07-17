// 文件职责：用真实 ELF64 字节文件、生产 Bus 与 PhysicalMemory 验证一致性测试装载路径。
// 边界：测试只构造输入映像和断言结果，不实现替代 ELF 装载器或绕开生产总线。

#include "rvemu/bus/access.hpp"
#include "rvemu/bus/address_range.hpp"
#include "rvemu/bus/bus.hpp"
#include "rvemu/conformance/elf_loader.hpp"
#include "rvemu/memory/physical_memory.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kRamBase = 0x8000'0000ULL;
constexpr std::size_t kRamSize = 0x1000U;

class TestContext final {
public:
    void expect(bool condition, const std::string& message) {
        if (!condition) {
            ++failures_;
            std::cerr << "失败：" << message << '\n';
        }
    }

    [[nodiscard]] int failures() const noexcept { return failures_; }

private:
    int failures_{0};
};

template <typename Integer>
void put_little_endian(std::vector<std::uint8_t>& bytes, std::size_t offset, Integer value) {
    for (std::size_t index = 0U; index < sizeof(Integer); ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

[[nodiscard]] std::vector<std::uint8_t> make_elf(std::uint16_t machine, bool include_tohost) {
    std::vector<std::uint8_t> bytes(0x300U, 0U);
    bytes[0] = 0x7FU;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 2U;
    bytes[5] = 1U;
    bytes[6] = 1U;
    put_little_endian<std::uint16_t>(bytes, 16U, 2U);
    put_little_endian<std::uint16_t>(bytes, 18U, machine);
    put_little_endian<std::uint32_t>(bytes, 20U, 1U);
    put_little_endian<std::uint64_t>(bytes, 24U, kRamBase);
    put_little_endian<std::uint64_t>(bytes, 32U, 0x40U);
    put_little_endian<std::uint64_t>(bytes, 40U, 0x200U);
    put_little_endian<std::uint16_t>(bytes, 52U, 64U);
    put_little_endian<std::uint16_t>(bytes, 54U, 56U);
    put_little_endian<std::uint16_t>(bytes, 56U, 1U);
    put_little_endian<std::uint16_t>(bytes, 58U, 64U);
    put_little_endian<std::uint16_t>(bytes, 60U, 3U);

    // 单个 PT_LOAD 段含 8 字节文件数据和 8 字节必须由装载器清零的 BSS。
    put_little_endian<std::uint32_t>(bytes, 0x40U, 1U);
    put_little_endian<std::uint64_t>(bytes, 0x48U, 0x100U);
    put_little_endian<std::uint64_t>(bytes, 0x50U, kRamBase);
    put_little_endian<std::uint64_t>(bytes, 0x58U, kRamBase);
    put_little_endian<std::uint64_t>(bytes, 0x60U, 8U);
    put_little_endian<std::uint64_t>(bytes, 0x68U, 16U);
    put_little_endian<std::uint32_t>(bytes, 0x100U, 0x0000'0013U);
    put_little_endian<std::uint32_t>(bytes, 0x104U, 0xA5A5'5A5AU);

    // 节 1 是含空符号和 tohost 的 SYMTAB，节 2 是其字符串表。
    put_little_endian<std::uint32_t>(bytes, 0x200U + 64U + 4U, 2U);
    put_little_endian<std::uint64_t>(bytes, 0x200U + 64U + 24U, 0x180U);
    put_little_endian<std::uint64_t>(bytes, 0x200U + 64U + 32U, 48U);
    put_little_endian<std::uint32_t>(bytes, 0x200U + 64U + 40U, 2U);
    put_little_endian<std::uint64_t>(bytes, 0x200U + 64U + 56U, 24U);
    put_little_endian<std::uint32_t>(bytes, 0x198U, include_tohost ? 1U : 0U);
    put_little_endian<std::uint64_t>(bytes, 0x1A0U, kRamBase + 8U);
    put_little_endian<std::uint32_t>(bytes, 0x200U + 128U + 4U, 3U);
    put_little_endian<std::uint64_t>(bytes, 0x200U + 128U + 24U, 0x1C0U);
    put_little_endian<std::uint64_t>(bytes, 0x200U + 128U + 32U, 8U);
    const std::string symbol_name = include_tohost ? std::string{"\0tohost\0", 8U}
                                                   : std::string{"\0absent", 7U};
    for (std::size_t index = 0U; index < symbol_name.size(); ++index) {
        bytes[0x1C0U + index] = static_cast<std::uint8_t>(symbol_name[index]);
    }
    return bytes;
}

[[nodiscard]] bool write_file(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return stream.good();
}

void test_valid_load(TestContext& context, const std::filesystem::path& path) {
    context.expect(write_file(path, make_elf(243U, true)), "应成功写入真实 ELF 测试输入");
    rvemu::bus::Bus bus;
    auto ram = std::make_shared<rvemu::memory::PhysicalMemory>(
        rvemu::bus::PhysicalAddress{kRamBase}, kRamSize);
    context.expect(bus.register_region(ram).ok(), "测试 RAM 应注册成功");
    const auto range = rvemu::bus::AddressRange::create(
        rvemu::bus::PhysicalAddress{kRamBase}, kRamSize);
    context.expect(range.has_value(), "测试 RAM 范围应合法");
    if (!range.has_value()) {
        return;
    }

    // 预置非零值，证明 p_memsz 尾部由生产装载器显式清零，而不是依赖 RAM 初态。
    context.expect(
        bus.write(
               rvemu::bus::PhysicalAddress{kRamBase + 8U},
               rvemu::bus::AccessWidth::DoubleWord,
               0xFFFF'FFFF'FFFF'FFFFULL,
               rvemu::bus::AccessType::Store)
            .ok(),
        "BSS 预置值应写入成功");
    const auto result = rvemu::conformance::load_elf64_riscv(path, bus, *range);
    context.expect(result.ok(), "合法 RV64 ELF 应装载成功");
    if (!result.ok()) {
        return;
    }
    context.expect(result.image->entry_point == kRamBase, "应保存 ELF 入口地址");
    context.expect(result.image->tohost_address == kRamBase + 8U, "应解析 tohost 符号");
    const auto instruction = bus.read(
        rvemu::bus::PhysicalAddress{kRamBase},
        rvemu::bus::AccessWidth::Word,
        rvemu::bus::AccessType::InstructionFetch);
    const auto bss = bus.read(
        rvemu::bus::PhysicalAddress{kRamBase + 8U},
        rvemu::bus::AccessWidth::DoubleWord,
        rvemu::bus::AccessType::Load);
    context.expect(instruction.ok() && instruction.value == 0x13U, "文件段应按小端序进入 RAM");
    context.expect(bss.ok() && bss.value == 0U, "PT_LOAD 的 BSS 尾部必须清零");
}

void test_rejections(TestContext& context, const std::filesystem::path& path) {
    rvemu::bus::Bus bus;
    auto ram = std::make_shared<rvemu::memory::PhysicalMemory>(
        rvemu::bus::PhysicalAddress{kRamBase}, kRamSize);
    context.expect(bus.register_region(ram).ok(), "拒绝测试 RAM 应注册成功");
    const auto range = rvemu::bus::AddressRange::create(
        rvemu::bus::PhysicalAddress{kRamBase}, kRamSize);
    if (!range.has_value()) {
        context.expect(false, "拒绝测试 RAM 范围应合法");
        return;
    }

    context.expect(write_file(path, make_elf(62U, true)), "应写入错误架构 ELF");
    context.expect(
        !rvemu::conformance::load_elf64_riscv(path, bus, *range).ok(),
        "x86-64 ELF 必须被拒绝");
    context.expect(write_file(path, make_elf(243U, false)), "应写入缺少 tohost 的 ELF");
    context.expect(
        !rvemu::conformance::load_elf64_riscv(path, bus, *range).ok(),
        "缺少 tohost 的 ELF 必须被拒绝");
}

}  // namespace

int main() {
    TestContext context;
    const std::filesystem::path path{"rvemu-elf-loader-test.elf"};
    test_valid_load(context, path);
    test_rejections(context, path);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    if (context.failures() != 0) {
        std::cerr << "ELF 装载器测试失败数：" << context.failures() << '\n';
        return 1;
    }
    std::cout << "ELF 装载器测试全部通过\n";
    return 0;
}
