// 文件职责：通过真实总线、CLINT 设备和 CSR 文件验证单 Hart 计时器及软件中断语义。
// 边界：测试不伪造 mip 状态；所有可见结果均由生产 CLINT 的 MMIO 和 synchronize 路径产生。

#include "rvemu/bus/address_map.hpp"
#include "rvemu/bus/bus.hpp"
#include "rvemu/core/csr.hpp"
#include "rvemu/devices/clint.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

constexpr std::uint64_t kClintBase = rvemu::bus::address_map::kClint.base;

class TestContext final {
   public:
    /** 累积断言错误，使单次测试运行报告全部 CLINT 状态边界。 */
    void expect(bool condition, const std::string& message) {
        if (!condition) {
            ++failures_;
            std::cerr << "失败：" << message << '\n';
        }
    }
    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

   private:
    int failures_{0};
};

class ClintFixture final {
   public:
    /** 创建并注册真实生产 CLINT；失败表示总线地址图或设备构造已违反不变量。 */
    ClintFixture() : clint_(std::make_shared<rvemu::devices::Clint>()) {
        if (!bus_.register_region(clint_).ok()) {
            throw std::runtime_error("CLINT 测试设备注册失败");
        }
    }

    /** 通过生产总线写 CLINT MMIO，而非直接修改设备内部状态。 */
    [[nodiscard]] rvemu::bus::AccessResult write(std::uint64_t offset,
                                                 rvemu::bus::AccessWidth width,
                                                 std::uint64_t value) {
        return bus_.write(rvemu::bus::PhysicalAddress{kClintBase + offset},
                          width,
                          value,
                          rvemu::bus::AccessType::Store);
    }
    /** 通过生产总线读 CLINT MMIO，以验证小端半字寄存器视图。 */
    [[nodiscard]] rvemu::bus::AccessResult read(std::uint64_t offset,
                                                rvemu::bus::AccessWidth width) {
        return bus_.read(
            rvemu::bus::PhysicalAddress{kClintBase + offset}, width, rvemu::bus::AccessType::Load);
    }
    /** 推进离散时钟后在同一边界投影 CSR 中断电平。 */
    void advance_and_synchronize(std::uint64_t ticks) {
        clint_->advance(ticks);
        clint_->synchronize(csrs_);
    }
    /** 在没有时间推进时刷新 MMIO 写入已改变的中断电平。 */
    void synchronize() {
        clint_->synchronize(csrs_);
    }
    [[nodiscard]] rvemu::core::CsrFile& csrs() noexcept {
        return csrs_;
    }

   private:
    rvemu::bus::Bus bus_{};
    std::shared_ptr<rvemu::devices::Clint> clint_;
    rvemu::core::CsrFile csrs_{};
};

[[nodiscard]] bool interrupt_pending(const rvemu::core::CsrFile& csrs,
                                     rvemu::core::InterruptCause cause) noexcept {
    const auto bit = 1ULL << static_cast<std::uint8_t>(cause);
    return (csrs.peek(rvemu::core::CsrAddress::Mip) & bit) != 0U;
}

/** 验证 mtime/time 投影，以及 mtimecmp 跨越和撤销 MTIP。 */
void test_timer_pending_and_time_projection(TestContext& context) {
    ClintFixture fixture;
    context.expect(
        fixture.write(rvemu::devices::Clint::kMtimeOffset, rvemu::bus::AccessWidth::DoubleWord, 10U)
            .ok(),
        "写 mtime 必须成功");
    context.expect(
        fixture
            .write(rvemu::devices::Clint::kMtimecmpOffset, rvemu::bus::AccessWidth::DoubleWord, 12U)
            .ok(),
        "写 mtimecmp 必须成功");
    fixture.synchronize();
    context.expect(fixture.csrs().peek(rvemu::core::CsrAddress::Time) == 10U,
                   "同步必须把 mtime 投影为 time CSR");
    context.expect(!interrupt_pending(fixture.csrs(), rvemu::core::InterruptCause::MachineTimer),
                   "mtime 小于 mtimecmp 时 MTIP 必须清除");
    fixture.advance_and_synchronize(2U);
    context.expect(interrupt_pending(fixture.csrs(), rvemu::core::InterruptCause::MachineTimer),
                   "mtime 到达 mtimecmp 时 MTIP 必须置位");
    context.expect(
        fixture
            .write(rvemu::devices::Clint::kMtimecmpOffset, rvemu::bus::AccessWidth::DoubleWord, 13U)
            .ok(),
        "更新 mtimecmp 必须成功");
    fixture.synchronize();
    context.expect(!interrupt_pending(fixture.csrs(), rvemu::core::InterruptCause::MachineTimer),
                   "写入更大的 mtimecmp 后 MTIP 必须撤销");
}

/** 验证 msip 的 bit 0 语义及 CLINT 同步不伤害外部中断源。 */
void test_machine_software_interrupt(TestContext& context) {
    ClintFixture fixture;
    fixture.csrs().set_interrupt_pending(rvemu::core::InterruptCause::MachineExternal, true);
    context.expect(
        fixture
            .write(rvemu::devices::Clint::kMsipOffset, rvemu::bus::AccessWidth::Word, 0xFFFF'FFFEU)
            .ok(),
        "写 msip 保留位必须成功");
    fixture.synchronize();
    context.expect(!interrupt_pending(fixture.csrs(), rvemu::core::InterruptCause::MachineSoftware),
                   "msip 高位写入不得置位 MSIP");
    context.expect(interrupt_pending(fixture.csrs(), rvemu::core::InterruptCause::MachineExternal),
                   "CLINT 同步不得清除其他中断源");
    context.expect(
        fixture.write(rvemu::devices::Clint::kMsipOffset, rvemu::bus::AccessWidth::Word, 1U).ok(),
        "写 msip bit 0 必须成功");
    fixture.synchronize();
    context.expect(interrupt_pending(fixture.csrs(), rvemu::core::InterruptCause::MachineSoftware),
                   "msip bit 0 必须置位 MSIP");
    context.expect(
        fixture.write(rvemu::devices::Clint::kMsipOffset, rvemu::bus::AccessWidth::Word, 0U).ok(),
        "清除 msip 必须成功");
    fixture.synchronize();
    context.expect(!interrupt_pending(fixture.csrs(), rvemu::core::InterruptCause::MachineSoftware),
                   "清除 msip 后 MSIP 必须撤销");
}

/** 验证 Linux 安全的高、低半字 mtimecmp 更新序列不会产生中间 MTIP。 */
void test_split_mtimecmp_update(TestContext& context) {
    ClintFixture fixture;
    context.expect(fixture
                       .write(rvemu::devices::Clint::kMtimeOffset,
                              rvemu::bus::AccessWidth::DoubleWord,
                              0x0000'0001'0000'0010ULL)
                       .ok(),
                   "写入测试 mtime 必须成功");
    context.expect(
        fixture
            .write(rvemu::devices::Clint::kMtimecmpOffset, rvemu::bus::AccessWidth::DoubleWord, 0U)
            .ok(),
        "设置已过期 mtimecmp 必须成功");
    fixture.synchronize();
    context.expect(interrupt_pending(fixture.csrs(), rvemu::core::InterruptCause::MachineTimer),
                   "已过期比较值必须使 MTIP 置位");
    context.expect(
        fixture
            .write(rvemu::devices::Clint::kMtimecmpOffset + 4U, rvemu::bus::AccessWidth::Word, 2U)
            .ok(),
        "安全序列先写 mtimecmp 高半字必须成功");
    fixture.synchronize();
    context.expect(!interrupt_pending(fixture.csrs(), rvemu::core::InterruptCause::MachineTimer),
                   "高半字先更新为未来值后 MTIP 必须撤销");
    context.expect(
        fixture.write(rvemu::devices::Clint::kMtimecmpOffset, rvemu::bus::AccessWidth::Word, 0x20U)
            .ok(),
        "安全序列再写 mtimecmp 低半字必须成功");
    fixture.synchronize();
    context.expect(!interrupt_pending(fixture.csrs(), rvemu::core::InterruptCause::MachineTimer),
                   "完整安全序列不得产生瞬时 MTIP");
    const auto comparison =
        fixture.read(rvemu::devices::Clint::kMtimecmpOffset, rvemu::bus::AccessWidth::DoubleWord);
    context.expect(comparison.ok() && comparison.value == 0x0000'0002'0000'0020ULL,
                   "分段写入必须按低/高半字组合为完整 mtimecmp");
}

/** 验证 CLINT 拒绝未声明宽度和非自然对齐访问。 */
void test_invalid_mmio_accesses(TestContext& context) {
    ClintFixture fixture;
    const auto byte_read =
        fixture.read(rvemu::devices::Clint::kMsipOffset, rvemu::bus::AccessWidth::Byte);
    context.expect(
        !byte_read.ok() && byte_read.error.code == rvemu::bus::BusErrorCode::InvalidWidth,
        "CLINT 必须拒绝字节宽度访问");
    const auto unaligned_read =
        fixture.read(rvemu::devices::Clint::kMtimeOffset + 2U, rvemu::bus::AccessWidth::Word);
    context.expect(
        !unaligned_read.ok() && unaligned_read.error.code == rvemu::bus::BusErrorCode::InvalidWidth,
        "CLINT 必须拒绝非自然对齐访问");
}

}  // namespace

int main() {
    TestContext context;
    test_timer_pending_and_time_projection(context);
    test_machine_software_interrupt(context);
    test_split_mtimecmp_update(context);
    test_invalid_mmio_accesses(context);
    if (context.failures() != 0) {
        std::cerr << "CLINT 测试共有 " << context.failures() << " 项断言失败。\n";
        return 1;
    }
    std::cout << "CLINT MMIO、定时器和软件中断测试全部通过。\n";
    return 0;
}
