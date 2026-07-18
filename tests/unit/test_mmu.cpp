// 文件职责：用生产 Bus、RAM 与 MMU 验证 Sv39 页表漫游、权限、A/D 原子更新和 TLB。
// 边界：测试只搭建真实物理页表，不实现第二套翻译器，也不伪造 CPU 或设备行为。

#include "rvemu/bus/bus.hpp"
#include "rvemu/memory/mmu.hpp"
#include "rvemu/memory/physical_memory.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

namespace {

constexpr std::uint64_t kRamBase = 0x8000'0000ULL;
constexpr std::uint64_t kRamSize = 16U * 1024U * 1024U;
constexpr std::uint64_t kRoot = kRamBase;
constexpr std::uint64_t kLevel1 = kRamBase + 0x1000U;
constexpr std::uint64_t kLevel0 = kRamBase + 0x2000U;
constexpr std::uint64_t kPage4K = kRamBase + 0x3000U;

constexpr std::uint64_t kSatpSv39 = 8ULL << 60U;
constexpr std::uint64_t kPteV = 1U << 0U;
constexpr std::uint64_t kPteR = 1U << 1U;
constexpr std::uint64_t kPteW = 1U << 2U;
constexpr std::uint64_t kPteX = 1U << 3U;
constexpr std::uint64_t kPteU = 1U << 4U;
constexpr std::uint64_t kPteA = 1U << 6U;
constexpr std::uint64_t kPteD = 1U << 7U;
constexpr std::uint64_t kMstatusSum = 1ULL << 18U;
constexpr std::uint64_t kMstatusMxr = 1ULL << 19U;

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

struct Fixture final {
    rvemu::bus::Bus bus{};
    std::shared_ptr<rvemu::memory::PhysicalMemory> ram{
        std::make_shared<rvemu::memory::PhysicalMemory>(
            rvemu::bus::PhysicalAddress{kRamBase},
            static_cast<std::size_t>(kRamSize),
            "ram")};
    rvemu::memory::Mmu mmu{bus};

    Fixture() {
        const auto registered = bus.register_region(ram);
        if (!registered.ok()) {
            throw std::runtime_error("注册 RAM 失败");
        }
    }

    void write64(std::uint64_t address, std::uint64_t value) {
        const auto written = bus.write(
            rvemu::bus::PhysicalAddress{address},
            rvemu::bus::AccessWidth::DoubleWord,
            value,
            rvemu::bus::AccessType::Initialization);
        if (!written.ok()) {
            throw std::runtime_error("写入测试页表失败");
        }
    }

    [[nodiscard]] std::uint64_t read64(std::uint64_t address) {
        const auto read = bus.read(
            rvemu::bus::PhysicalAddress{address},
            rvemu::bus::AccessWidth::DoubleWord,
            rvemu::bus::AccessType::Load);
        if (!read.ok()) {
            throw std::runtime_error("读取测试页表失败");
        }
        return read.value;
    }
};

[[nodiscard]] constexpr std::uint64_t ppn(std::uint64_t physical_address) noexcept {
    return physical_address >> 12U;
}

[[nodiscard]] constexpr std::uint64_t pte(
    std::uint64_t physical_address,
    std::uint64_t flags) noexcept {
    return (ppn(physical_address) << 10U) | flags;
}

[[nodiscard]] constexpr std::uint64_t satp(std::uint64_t root) noexcept {
    return kSatpSv39 | ppn(root);
}

[[nodiscard]] rvemu::memory::MmuContext supervisor_context(
    std::uint64_t status = 0U) noexcept {
    return rvemu::memory::MmuContext{
        rvemu::core::PrivilegeMode::Supervisor,
        satp(kRoot),
        status};
}

[[nodiscard]] rvemu::memory::MmuContext user_context(
    std::uint64_t status = 0U) noexcept {
    return rvemu::memory::MmuContext{
        rvemu::core::PrivilegeMode::User,
        satp(kRoot),
        status};
}

void install_three_level_mapping(
    Fixture& fixture,
    std::uint64_t leaf_flags,
    std::uint64_t physical_page = kPage4K) {
    fixture.write64(kRoot + 0U * 8U, pte(kLevel1, kPteV));
    fixture.write64(kLevel1 + 0U * 8U, pte(kLevel0, kPteV));
    fixture.write64(kLevel0 + 4U * 8U, pte(physical_page, leaf_flags));
}

void test_bare_and_canonical(TestContext& context) {
    Fixture fixture{};
    const auto bare = fixture.mmu.translate(
        0x1234U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Load,
        rvemu::memory::MmuContext{rvemu::core::PrivilegeMode::Supervisor, 0U, 0U});
    context.expect(
        bare.physical_address.has_value() &&
            bare.physical_address->value() == 0x1234U &&
            !bare.fault.has_value(),
        "satp Bare 必须直通物理地址");

    const auto bad = fixture.mmu.translate(
        1ULL << 39U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Load,
        supervisor_context());
    context.expect(
        !bad.physical_address.has_value() &&
            bad.fault.has_value() &&
            bad.fault->cause == rvemu::core::ExceptionCause::LoadPageFault &&
            bad.fault->value == (1ULL << 39U),
        "Sv39 非规范虚拟地址必须产生加载页错误");
}

void test_leaf_ad_and_permissions(TestContext& context) {
    Fixture fixture{};
    install_three_level_mapping(fixture, kPteV | kPteR | kPteW | kPteU);

    const auto loaded = fixture.mmu.translate(
        0x4008U,
        rvemu::bus::AccessWidth::DoubleWord,
        rvemu::memory::MmuAccessKind::Load,
        user_context());
    context.expect(
        loaded.physical_address.has_value() &&
            loaded.physical_address->value() == kPage4K + 8U,
        "4KiB 叶子必须合成 PPN 和页内偏移");
    const auto after_load = fixture.read64(kLevel0 + 4U * 8U);
    context.expect(
        (after_load & kPteA) != 0U && (after_load & kPteD) == 0U,
        "加载成功后必须只原子设置 A 位");

    const auto stored = fixture.mmu.translate(
        0x4010U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Store,
        user_context());
    context.expect(
        stored.physical_address.has_value() &&
            stored.physical_address->value() == kPage4K + 0x10U,
        "存储翻译必须复用同一 4KiB 映射");
    const auto after_store = fixture.read64(kLevel0 + 4U * 8U);
    context.expect(
        (after_store & kPteA) != 0U && (after_store & kPteD) != 0U,
        "存储成功后必须同时保证 A/D 位已设置");

    Fixture denied{};
    install_three_level_mapping(denied, kPteV | kPteR | kPteW);
    const auto user_denied = denied.mmu.translate(
        0x4000U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Load,
        user_context());
    context.expect(
        user_denied.fault.has_value() &&
            user_denied.fault->cause == rvemu::core::ExceptionCause::LoadPageFault,
        "U-mode 访问 U=0 内核页必须页错误");
    context.expect(
        (denied.read64(kLevel0 + 4U * 8U) & (kPteA | kPteD)) == 0U,
        "权限拒绝不得提前设置 A/D 位");

    Fixture atomic_denied{};
    install_three_level_mapping(atomic_denied, kPteV | kPteW | kPteU | kPteA | kPteD);
    const auto atomic = atomic_denied.mmu.translate(
        0x4000U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Atomic,
        user_context());
    context.expect(
        atomic.fault.has_value() &&
            atomic.fault->cause == rvemu::core::ExceptionCause::StorePageFault,
        "AMO 翻译必须同时要求 R/W 权限，失败时按 store page fault 报告");
}

void test_superpages_and_mxr_sum(TestContext& context) {
    Fixture fixture{};
    fixture.write64(kRoot + 0U * 8U, pte(kLevel1, kPteV));
    fixture.write64(kLevel1 + 1U * 8U, pte(kRamBase + 0x20'0000U, kPteV | kPteR | kPteW | kPteA | kPteD));
    const auto two_mib = fixture.mmu.translate(
        0x20'1234U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Load,
        supervisor_context());
    context.expect(
        two_mib.physical_address.has_value() &&
            two_mib.physical_address->value() == kRamBase + 0x20'1234U,
        "level 1 叶子必须支持 2MiB 超级页");

    fixture.write64(kRoot + 1U * 8U, pte(kRamBase, kPteV | kPteR | kPteA | kPteD));
    const auto one_gib = fixture.mmu.translate(
        0x4000'4567U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Load,
        supervisor_context());
    context.expect(
        one_gib.physical_address.has_value() &&
            one_gib.physical_address->value() == kRamBase + 0x4567U,
        "level 2 叶子必须支持 1GiB 超级页");

    Fixture misaligned{};
    misaligned.write64(kRoot + 0U * 8U, pte(kLevel1, kPteV));
    misaligned.write64(kLevel1 + 1U * 8U, pte(kRamBase + 0x201000U, kPteV | kPteR | kPteA));
    const auto bad_2m = misaligned.mmu.translate(
        0x20'0000U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Load,
        supervisor_context());
    context.expect(
        bad_2m.fault.has_value() &&
            bad_2m.fault->cause == rvemu::core::ExceptionCause::LoadPageFault &&
            bad_2m.fault->value == 0x20'0000U,
        "level 1 叶子的低级 PPN 非零时必须按错位超级页报页错误");

    misaligned.write64(kRoot + 1U * 8U, pte(kRamBase + 0x20'0000U, kPteV | kPteR | kPteA));
    const auto bad_1g = misaligned.mmu.translate(
        0x4000'0000U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Load,
        supervisor_context());
    context.expect(
        bad_1g.fault.has_value() &&
            bad_1g.fault->cause == rvemu::core::ExceptionCause::LoadPageFault &&
            bad_1g.fault->value == 0x4000'0000U,
        "level 2 叶子的低级 PPN 非零时必须按错位超级页报页错误");

    Fixture mxr{};
    install_three_level_mapping(mxr, kPteV | kPteX | kPteU | kPteA);
    const auto no_mxr = mxr.mmu.translate(
        0x4000U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Load,
        user_context());
    const auto with_mxr = mxr.mmu.translate(
        0x4000U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Load,
        user_context(kMstatusMxr));
    context.expect(no_mxr.fault.has_value() && with_mxr.physical_address.has_value(),
                   "MXR=1 必须允许加载仅 X 页");

    Fixture sum{};
    install_three_level_mapping(sum, kPteV | kPteR | kPteU | kPteA);
    const auto no_sum = sum.mmu.translate(
        0x4000U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Load,
        supervisor_context());
    const auto with_sum = sum.mmu.translate(
        0x4000U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Load,
        supervisor_context(kMstatusSum));
    context.expect(no_sum.fault.has_value() && with_sum.physical_address.has_value(),
                   "S-mode 数据访问 U 页必须受 SUM 控制");
}

void test_tlb_and_sfence(TestContext& context) {
    Fixture fixture{};
    install_three_level_mapping(fixture, kPteV | kPteR | kPteA);
    const auto first = fixture.mmu.translate(
        0x4000U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Load,
        supervisor_context());
    context.expect(first.physical_address.has_value(), "首次翻译必须成功填充 TLB");
    context.expect(fixture.mmu.valid_tlb_entries() == 1U, "成功翻译后必须存在一个 TLB 条目");

    fixture.write64(kLevel0 + 4U * 8U, pte(kRamBase + 0x4000U, kPteV | kPteR | kPteA));
    const auto cached = fixture.mmu.translate(
        0x4000U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Load,
        supervisor_context());
    context.expect(
        cached.physical_address.has_value() &&
            cached.physical_address->value() == kPage4K,
        "未执行 SFENCE.VMA 时允许命中旧 TLB 翻译");

    fixture.mmu.sfence_vma(0x4000U, std::nullopt);
    const auto refilled = fixture.mmu.translate(
        0x4000U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Load,
        supervisor_context());
    context.expect(
        refilled.physical_address.has_value() &&
            refilled.physical_address->value() == kRamBase + 0x4000U,
        "SFENCE.VMA 后必须重新页表漫游并观察新 PTE");
}

void test_page_fault_causes(TestContext& context) {
    Fixture fixture{};
    fixture.write64(kRoot + 0U * 8U, pte(kLevel1, kPteV));
    fixture.write64(kLevel1 + 0U * 8U, pte(kLevel0, kPteV));
    fixture.write64(kLevel0 + 4U * 8U, pte(kPage4K, kPteV | kPteR | kPteA));

    const auto instruction_fault = fixture.mmu.translate(
        0x4000U,
        rvemu::bus::AccessWidth::HalfWord,
        rvemu::memory::MmuAccessKind::InstructionFetch,
        supervisor_context());
    const auto load_fault = fixture.mmu.translate(
        0x5000U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Load,
        supervisor_context());
    const auto store_fault = fixture.mmu.translate(
        0x4000U,
        rvemu::bus::AccessWidth::Word,
        rvemu::memory::MmuAccessKind::Store,
        supervisor_context());

    context.expect(
        instruction_fault.fault.has_value() &&
            instruction_fault.fault->cause == rvemu::core::ExceptionCause::InstructionPageFault &&
            instruction_fault.fault->value == 0x4000U,
        "取指权限失败必须报告 instruction page fault 且 tval 为原始 VA");
    context.expect(
        load_fault.fault.has_value() &&
            load_fault.fault->cause == rvemu::core::ExceptionCause::LoadPageFault &&
            load_fault.fault->value == 0x5000U,
        "无效 PTE 加载必须报告 load page fault 且 tval 为原始 VA");
    context.expect(
        store_fault.fault.has_value() &&
            store_fault.fault->cause == rvemu::core::ExceptionCause::StorePageFault &&
            store_fault.fault->value == 0x4000U,
        "存储权限失败必须报告 store page fault 且 tval 为原始 VA");
}

}  // namespace

int main() {
    TestContext context{};
    try {
        test_bare_and_canonical(context);
        test_leaf_ad_and_permissions(context);
        test_superpages_and_mxr_sum(context);
        test_page_fault_causes(context);
        test_tlb_and_sfence(context);
    } catch (const std::exception& error) {
        std::cerr << "异常：" << error.what() << '\n';
        return 1;
    }

    if (context.failures() != 0) {
        return 1;
    }
    std::cout << "Sv39 MMU、A/D、权限、超级页和 TLB 测试通过。\n";
    return 0;
}
