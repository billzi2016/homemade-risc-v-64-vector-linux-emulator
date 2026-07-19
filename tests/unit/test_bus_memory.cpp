// 文件职责：使用真实 Bus、PhysicalMemory 与 BootRom 验证物理访问、边界、只读和原子事务。
// 边界：本文件不定义假设备、Mock 后端或复制生产地址分发逻辑。

#include "rvemu/bus/address_map.hpp"
#include "rvemu/bus/address_range.hpp"
#include "rvemu/bus/bus.hpp"
#include "rvemu/memory/boot_rom.hpp"
#include "rvemu/memory/physical_memory.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

class TestContext final {
   public:
    void expect(bool condition, const std::string& message) {
        if (condition) {
            return;
        }
        ++failures_;
        std::cerr << "失败：" << message << '\n';
    }

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

   private:
    int failures_{0};
};

using rvemu::bus::AccessType;
using rvemu::bus::AccessWidth;
using rvemu::bus::BusErrorCode;
using rvemu::bus::PhysicalAddress;

void test_address_ranges(TestContext& context) {
    const auto valid = rvemu::bus::AddressRange::create(PhysicalAddress{0x1000U}, 0x100U);
    context.expect(valid.has_value(), "普通地址区间应创建成功");
    if (valid.has_value()) {
        context.expect(valid->contains(PhysicalAddress{0x1000U}), "区间应包含首地址");
        context.expect(valid->contains(PhysicalAddress{0x10FFU}), "区间应包含末地址");
        context.expect(!valid->contains(PhysicalAddress{0x1100U}), "半开区间不应包含结束地址");
        context.expect(!valid->contains(PhysicalAddress{0x10FFU}, 2U),
                       "跨越区间末端的多字节访问应被拒绝");
    }

    context.expect(!rvemu::bus::AddressRange::create(PhysicalAddress{0x1000U}, 0U).has_value(),
                   "零长度区域应被拒绝");
    context.expect(!rvemu::bus::AddressRange::create(
                        PhysicalAddress{std::numeric_limits<std::uint64_t>::max() - 3U}, 8U)
                        .has_value(),
                   "结束地址溢出的区域应被拒绝");
}

void test_fixed_address_map(TestContext& context) {
    const auto& entries = rvemu::bus::address_map::kEntries;
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        const auto current = rvemu::bus::AddressRange::create(PhysicalAddress{entries[index].base},
                                                              entries[index].size);
        context.expect(current.has_value(),
                       std::string{entries[index].name} + " 固定地址范围必须合法");
        if (!current.has_value()) {
            continue;
        }
        for (std::size_t other_index = index + 1U; other_index < entries.size(); ++other_index) {
            const auto other = rvemu::bus::AddressRange::create(
                PhysicalAddress{entries[other_index].base}, entries[other_index].size);
            context.expect(other.has_value(), "所有固定地址范围必须可构造");
            if (other.has_value()) {
                context.expect(!current->overlaps(*other),
                               std::string{entries[index].name} + " 不得与 "
                                   + entries[other_index].name + " 重叠");
            }
        }
    }
}

void test_bus_registration(TestContext& context) {
    rvemu::bus::Bus bus;
    const auto null_registration = bus.register_region(nullptr);
    context.expect(!null_registration.ok(), "空物理区域必须注册失败");
    context.expect(null_registration.error.code == BusErrorCode::InvalidRegion,
                   "空区域注册必须返回 InvalidRegion");

    auto ram = std::make_shared<rvemu::memory::PhysicalMemory>(
        PhysicalAddress{0x8000'0000U}, 0x1000U, "test-ram");
    auto rom =
        std::make_shared<rvemu::memory::BootRom>(PhysicalAddress{0x0000'1000U}, 0x100U, "test-rom");

    context.expect(bus.register_region(ram).ok(), "RAM 应成功注册到物理总线");
    context.expect(bus.register_region(rom).ok(), "ROM 应成功注册到物理总线");
    context.expect(bus.region_count() == 2U, "总线应保存两个生产地址区域");

    auto overlapping_ram = std::make_shared<rvemu::memory::PhysicalMemory>(
        PhysicalAddress{0x8000'0080U}, 0x100U, "overlap-ram");
    const auto overlap = bus.register_region(overlapping_ram);
    context.expect(!overlap.ok(), "重叠物理区域必须注册失败");
    context.expect(overlap.error.code == BusErrorCode::RegionOverlap,
                   "重叠注册必须返回 RegionOverlap");
    context.expect(bus.region_count() == 2U, "失败注册不得改变区域数量");
}

void test_ram_endianness_and_widths(TestContext& context) {
    constexpr std::uint64_t base = 0x8000'0000U;
    rvemu::bus::Bus bus;
    auto ram = std::make_shared<rvemu::memory::PhysicalMemory>(PhysicalAddress{base}, 0x1000U);
    context.expect(bus.register_region(ram).ok(), "RAM 注册必须成功");

    const auto write64 = bus.write(PhysicalAddress{base + 3U},
                                   AccessWidth::DoubleWord,
                                   0x8877'6655'4433'2211ULL,
                                   AccessType::Store);
    context.expect(write64.ok(), "非对齐 64 位 RAM 写入应成功");

    const auto byte0 = bus.read(PhysicalAddress{base + 3U}, AccessWidth::Byte, AccessType::Load);
    const auto half = bus.read(PhysicalAddress{base + 4U}, AccessWidth::HalfWord, AccessType::Load);
    const auto word = bus.read(PhysicalAddress{base + 6U}, AccessWidth::Word, AccessType::Load);
    const auto full =
        bus.read(PhysicalAddress{base + 3U}, AccessWidth::DoubleWord, AccessType::Load);
    context.expect(byte0.ok() && byte0.value == 0x11U, "最低地址必须保存最低有效字节");
    context.expect(half.ok() && half.value == 0x3322U, "16 位读取必须遵循小端序");
    context.expect(word.ok() && word.value == 0x7766'5544U, "32 位读取必须遵循小端序");
    context.expect(full.ok() && full.value == 0x8877'6655'4433'2211ULL, "64 位读取必须往返一致");

    // 每种写宽度单独验证，且超出访问宽度的高位必须像真实总线一样被截断。
    context.expect(
        bus.write(PhysicalAddress{base + 0x20U}, AccessWidth::Byte, 0x1AAU, AccessType::Store).ok(),
        "8 位写入应成功");
    context.expect(
        bus.write(
               PhysicalAddress{base + 0x22U}, AccessWidth::HalfWord, 0x1'BEEFU, AccessType::Store)
            .ok(),
        "16 位写入应成功");
    context.expect(bus.write(PhysicalAddress{base + 0x24U},
                             AccessWidth::Word,
                             0x1'89AB'CDEFULL,
                             AccessType::Store)
                       .ok(),
                   "32 位写入应成功");
    context.expect(bus.write(PhysicalAddress{base + 0x28U},
                             AccessWidth::DoubleWord,
                             0xFEDC'BA98'7654'3210ULL,
                             AccessType::Store)
                       .ok(),
                   "64 位写入应成功");

    const auto byte_written =
        bus.read(PhysicalAddress{base + 0x20U}, AccessWidth::Byte, AccessType::Load);
    const auto half_written =
        bus.read(PhysicalAddress{base + 0x22U}, AccessWidth::HalfWord, AccessType::Load);
    const auto word_written =
        bus.read(PhysicalAddress{base + 0x24U}, AccessWidth::Word, AccessType::Load);
    const auto double_written =
        bus.read(PhysicalAddress{base + 0x28U}, AccessWidth::DoubleWord, AccessType::Load);
    context.expect(byte_written.ok() && byte_written.value == 0xAAU, "8 位写入必须截断高位");
    context.expect(half_written.ok() && half_written.value == 0xBEEFU, "16 位写入必须截断高位");
    context.expect(word_written.ok() && word_written.value == 0x89AB'CDEFU,
                   "32 位写入必须截断高位");
    context.expect(double_written.ok() && double_written.value == 0xFEDC'BA98'7654'3210ULL,
                   "64 位写入必须完整保留");
}

void test_bus_failures_have_no_partial_effect(TestContext& context) {
    constexpr std::uint64_t base = 0x8000'0000U;
    constexpr std::uint64_t size = 0x100U;
    rvemu::bus::Bus bus;
    auto ram = std::make_shared<rvemu::memory::PhysicalMemory>(PhysicalAddress{base}, size);
    context.expect(bus.register_region(ram).ok(), "边界测试 RAM 注册必须成功");

    const auto sentinel =
        bus.write(PhysicalAddress{base + size - 1U}, AccessWidth::Byte, 0xA5U, AccessType::Store);
    context.expect(sentinel.ok(), "末字节哨兵写入应成功");

    const auto crossing = bus.write(
        PhysicalAddress{base + size - 1U}, AccessWidth::HalfWord, 0xFFFFU, AccessType::Store);
    context.expect(!crossing.ok(), "跨越 RAM 末端的写入必须失败");
    context.expect(crossing.error.code == BusErrorCode::OutOfBounds, "跨界应返回 OutOfBounds");

    const auto unchanged =
        bus.read(PhysicalAddress{base + size - 1U}, AccessWidth::Byte, AccessType::Load);
    context.expect(unchanged.ok() && unchanged.value == 0xA5U, "跨界失败不得修改首字节");

    const auto unmapped =
        bus.read(PhysicalAddress{0x7000'0000U}, AccessWidth::Word, AccessType::Load);
    context.expect(!unmapped.ok() && unmapped.error.code == BusErrorCode::Unmapped,
                   "未映射读取必须结构化失败");

    const auto overflow = bus.read(PhysicalAddress{std::numeric_limits<std::uint64_t>::max()},
                                   AccessWidth::HalfWord,
                                   AccessType::Load);
    context.expect(!overflow.ok() && overflow.error.code == BusErrorCode::AddressOverflow,
                   "物理末地址加访问宽度溢出必须在分发前失败");
}

void test_boot_rom_lifecycle(TestContext& context) {
    constexpr std::uint64_t base = 0x1000U;
    rvemu::bus::Bus bus;
    auto rom = std::make_shared<rvemu::memory::BootRom>(PhysicalAddress{base}, 0x100U);
    const std::vector<std::uint8_t> image{0x13U, 0x00U, 0x00U, 0x00U};

    const auto oversized_load = rom->load(0xFEU, image);
    context.expect(!oversized_load.ok() && oversized_load.error.code == BusErrorCode::OutOfBounds,
                   "Boot ROM 初始化数据跨界必须失败");
    context.expect(rom->load(0U, image).ok(), "Boot ROM 密封前应允许受控装载");
    rom->seal();
    context.expect(rom->sealed(), "Boot ROM 应报告已密封");
    context.expect(bus.register_region(rom).ok(), "Boot ROM 应成功注册");

    const auto instruction =
        bus.read(PhysicalAddress{base}, AccessWidth::Word, AccessType::InstructionFetch);
    context.expect(instruction.ok() && instruction.value == 0x13U, "Boot ROM 指令读取必须为小端值");

    const auto runtime_write =
        bus.write(PhysicalAddress{base}, AccessWidth::Word, 0xFFFF'FFFFU, AccessType::Store);
    context.expect(!runtime_write.ok() && runtime_write.error.code == BusErrorCode::ReadOnly,
                   "运行期总线写入 Boot ROM 必须失败");

    const auto late_load = rom->load(0U, image);
    context.expect(!late_load.ok() && late_load.error.code == BusErrorCode::ReadOnly,
                   "密封后初始化接口也不得修改 Boot ROM");
}

void test_atomic_compare_exchange(TestContext& context) {
    constexpr std::uint64_t base = 0x8000'0000U;
    rvemu::bus::Bus bus;
    auto ram = std::make_shared<rvemu::memory::PhysicalMemory>(PhysicalAddress{base}, 0x100U);
    context.expect(bus.register_region(ram).ok(), "原子测试 RAM 注册必须成功");
    context.expect(
        bus.write(PhysicalAddress{base + 8U}, AccessWidth::DoubleWord, 0x10U, AccessType::Store)
            .ok(),
        "原子测试初值写入必须成功");

    const auto success =
        bus.compare_exchange(PhysicalAddress{base + 8U}, AccessWidth::DoubleWord, 0x10U, 0x20U);
    context.expect(success.ok() && success.exchanged, "匹配旧值时 compare-exchange 必须提交");
    context.expect(success.value == 0x10U, "成功原子事务必须返回观察到的旧值");

    const auto failure =
        bus.compare_exchange(PhysicalAddress{base + 8U}, AccessWidth::DoubleWord, 0x10U, 0x30U);
    context.expect(failure.ok() && !failure.exchanged, "旧值不匹配时不得提交写入");
    context.expect(failure.value == 0x20U, "失败原子事务必须返回当前观察值");

    const auto current =
        bus.read(PhysicalAddress{base + 8U}, AccessWidth::DoubleWord, AccessType::Load);
    context.expect(current.ok() && current.value == 0x20U, "失败原子事务不得改变 RAM");

    context.expect(
        bus.write(PhysicalAddress{base + 0x20U}, AccessWidth::Word, 0xFFFF'FF80U, AccessType::Store)
            .ok(),
        "32 位原子测试初值写入必须成功");
    const auto narrow = bus.compare_exchange(PhysicalAddress{base + 0x20U},
                                             AccessWidth::Word,
                                             0xCAFE'BABE'FFFF'FF80ULL,
                                             0x1234'5678'89AB'CDEFULL);
    context.expect(narrow.ok() && narrow.exchanged, "窄原子事务只比较访问宽度内的低位");
    const auto narrow_value =
        bus.read(PhysicalAddress{base + 0x20U}, AccessWidth::Word, AccessType::Load);
    context.expect(narrow_value.ok() && narrow_value.value == 0x89AB'CDEFU,
                   "窄原子事务只提交访问宽度内的低位");

    const auto crossing =
        bus.compare_exchange(PhysicalAddress{base + 0xFEU}, AccessWidth::Word, 0U, 1U);
    context.expect(!crossing.ok() && crossing.error.code == BusErrorCode::OutOfBounds,
                   "跨 RAM 末端的原子事务必须在提交前失败");
}

// 使用生产总线验证 LR/SC 保留监视器；普通存储、DMA 与 CAS 都必须共享同一失效入口。
void test_load_reserved_and_store_conditional(TestContext& context) {
    constexpr std::uint64_t base = 0x8000'0000U;
    const auto reserved_address = PhysicalAddress{base + 0x40U};
    rvemu::bus::Bus bus;
    auto ram = std::make_shared<rvemu::memory::PhysicalMemory>(PhysicalAddress{base}, 0x100U);
    context.expect(bus.register_region(ram).ok(), "保留监视测试 RAM 注册必须成功");
    context.expect(
        bus.write(reserved_address, AccessWidth::DoubleWord, 0x10U, AccessType::Store).ok(),
        "保留监视测试初值写入必须成功");

    auto loaded = bus.load_reserved(reserved_address, AccessWidth::DoubleWord);
    context.expect(loaded.access.ok() && loaded.access.value == 0x10U, "LR 总线事务必须读取旧值");
    context.expect(loaded.token.valid(), "成功 LR 必须返回非零不透明 token");

    context.expect(
        bus.write(PhysicalAddress{base + 0x60U}, AccessWidth::Word, 1U, AccessType::DmaWrite).ok(),
        "不重叠 DMA 写必须成功");
    auto conditional =
        bus.store_conditional(loaded.token, reserved_address, AccessWidth::DoubleWord, 0x20U);
    context.expect(conditional.ok() && conditional.exchanged, "不重叠写入不得破坏保留，SC 应成功");

    conditional =
        bus.store_conditional(loaded.token, reserved_address, AccessWidth::DoubleWord, 0x30U);
    context.expect(conditional.ok() && !conditional.exchanged, "同一个 token 只能被一次 SC 消费");

    loaded = bus.load_reserved(reserved_address, AccessWidth::DoubleWord);
    context.expect(
        bus.write(PhysicalAddress{base + 0x43U}, AccessWidth::Byte, 0xAAU, AccessType::DmaWrite)
            .ok(),
        "重叠 DMA 字节写必须成功");
    conditional =
        bus.store_conditional(loaded.token, reserved_address, AccessWidth::DoubleWord, 0x40U);
    context.expect(conditional.ok() && !conditional.exchanged, "重叠 DMA 写必须使 SC 条件失败");

    loaded = bus.load_reserved(reserved_address, AccessWidth::DoubleWord);
    const auto observed = loaded.access.value;
    const auto failed_compare =
        bus.compare_exchange(reserved_address, AccessWidth::DoubleWord, observed ^ 1U, 0x50U);
    context.expect(failed_compare.ok() && !failed_compare.exchanged,
                   "未提交的 CAS 必须明确报告比较失败");
    conditional =
        bus.store_conditional(loaded.token, reserved_address, AccessWidth::DoubleWord, 0x60U);
    context.expect(conditional.ok() && conditional.exchanged,
                   "未改变内存的 CAS 失败不得使保留失效");

    loaded = bus.load_reserved(reserved_address, AccessWidth::DoubleWord);
    const auto successful_compare =
        bus.compare_exchange(reserved_address, AccessWidth::DoubleWord, 0x60U, 0x70U);
    context.expect(successful_compare.ok() && successful_compare.exchanged,
                   "重叠 CAS 必须成功提交");
    conditional =
        bus.store_conditional(loaded.token, reserved_address, AccessWidth::DoubleWord, 0x80U);
    context.expect(conditional.ok() && !conditional.exchanged, "成功原子更新必须使旧保留失效");

    loaded = bus.load_reserved(reserved_address, AccessWidth::DoubleWord);
    conditional = bus.store_conditional(
        loaded.token, PhysicalAddress{base + 0x48U}, AccessWidth::DoubleWord, 0x90U);
    context.expect(conditional.ok() && !conditional.exchanged,
                   "地址不匹配的 SC 必须失败并消费 token");
    conditional =
        bus.store_conditional(loaded.token, reserved_address, AccessWidth::DoubleWord, 0xA0U);
    context.expect(conditional.ok() && !conditional.exchanged,
                   "地址不匹配后不得再次使用已消费 token");
}

}  // namespace

int main() {
    TestContext context;
    try {
        test_address_ranges(context);
        test_fixed_address_map(context);
        test_bus_registration(context);
        test_ram_endianness_and_widths(context);
        test_bus_failures_have_no_partial_effect(context);
        test_boot_rom_lifecycle(context);
        test_atomic_compare_exchange(context);
        test_load_reserved_and_store_conditional(context);
    } catch (const std::exception& error) {
        std::cerr << "测试运行出现未处理异常：" << error.what() << '\n';
        return 2;
    }

    if (context.failures() != 0) {
        std::cerr << "共有 " << context.failures() << " 项断言失败。\n";
        return 1;
    }

    std::cout << "物理总线、RAM 与 Boot ROM 的全部真实组件测试通过。\n";
    return 0;
}
