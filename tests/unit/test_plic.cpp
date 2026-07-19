// 文件职责：经真实总线验证 PLIC priority、claim/complete、threshold 与 M/S 外部中断投影。
// 边界：测试不复制 PLIC 仲裁逻辑，只观察生产 MMIO 和 CSR 状态。

#include "rvemu/bus/address_map.hpp"
#include "rvemu/bus/bus.hpp"
#include "rvemu/core/csr.hpp"
#include "rvemu/devices/plic.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
constexpr std::uint64_t kBase = rvemu::bus::address_map::kPlic.base;
constexpr std::uint64_t kPriority = 4U;
constexpr std::uint64_t kPending = 0x1000U;
constexpr std::uint64_t kEnableM = 0x2000U;
constexpr std::uint64_t kEnableS = 0x2080U;
constexpr std::uint64_t kContextM = 0x200000U;
constexpr std::uint64_t kContextS = 0x201000U;

/** 注册真实生产 PLIC，并为测试提供统一的总线 MMIO 访问入口。 */
class Fixture final {
   public:
    Fixture() : plic(std::make_shared<rvemu::devices::Plic>()) {
        if (!bus.register_region(plic).ok()) {
            throw std::runtime_error("PLIC 注册失败");
        }
    }

    /** 以 32 位自然对齐事务写入 PLIC，返回生产总线的结构化结果。 */
    [[nodiscard]] rvemu::bus::AccessResult write(std::uint64_t offset, std::uint32_t value) {
        return bus.write(rvemu::bus::PhysicalAddress{kBase + offset},
                         rvemu::bus::AccessWidth::Word,
                         value,
                         rvemu::bus::AccessType::Store);
    }

    /** 读取 context claim 寄存器；读取本身会执行生产 PLIC 的 claim 状态转换。 */
    [[nodiscard]] std::uint64_t claim(std::uint64_t context) {
        return bus
            .read(rvemu::bus::PhysicalAddress{kBase + context + 4U},
                  rvemu::bus::AccessWidth::Word,
                  rvemu::bus::AccessType::Load)
            .value;
    }

    /** 读取无副作用的 32 位 PLIC MMIO 寄存器。 */
    [[nodiscard]] std::uint64_t read(std::uint64_t offset) {
        return bus
            .read(rvemu::bus::PhysicalAddress{kBase + offset},
                  rvemu::bus::AccessWidth::Word,
                  rvemu::bus::AccessType::Load)
            .value;
    }

    /** 在明确的模拟器同步点把 PLIC context 状态投影到 CSR pending 位。 */
    void sync() {
        plic->synchronize(csrs);
    }

    /** 读取指定外部中断在唯一 mip 状态中的当前电平。 */
    [[nodiscard]] bool pending(rvemu::core::InterruptCause cause) const {
        return (csrs.peek(rvemu::core::CsrAddress::Mip)
                & (1ULL << static_cast<std::uint64_t>(cause)))
               != 0U;
    }

    rvemu::bus::Bus bus{};
    std::shared_ptr<rvemu::devices::Plic> plic;
    rvemu::core::CsrFile csrs{};
};

/** 累积断言失败，使一次执行能报告同一状态机中的多个错误。 */
void expect(bool condition, const char* message, int& failures) {
    if (!condition) {
        ++failures;
        std::cerr << "失败：" << message << '\n';
    }
}

/** 验证优先级裁决、占用、完成、M/S context 和 threshold 的组合行为。 */
void test_plic(int& failures) {
    Fixture fixture;
    expect(fixture.write(kPriority * 2U, 2U).ok() && fixture.write(kPriority * 3U, 3U).ok(),
           "配置 source 优先级",
           failures);
    expect(
        fixture.write(kEnableM, (1U << 2U) | (1U << 3U)).ok(), "启用 M context sources", failures);

    fixture.plic->set_source_level(2U, true);
    fixture.plic->set_source_level(3U, true);
    fixture.sync();
    expect(fixture.pending(rvemu::core::InterruptCause::MachineExternal),
           "M context 必须投影 MEIP",
           failures);
    expect(fixture.claim(kContextM) == 3U && fixture.claim(kContextM) == 2U,
           "claim 必须先选择高优先级且跳过已占用 source",
           failures);
    expect((fixture.read(kPending) & ((1U << 2U) | (1U << 3U))) == 0U,
           "已 claim source 的 pending 位必须清除",
           failures);

    expect(fixture.write(kContextS + 4U, 2U).ok(),
           "错误 context complete 写入本身应安全返回",
           failures);
    expect((fixture.read(kPending) & (1U << 2U)) == 0U,
           "错误 context 不得释放已 claim source",
           failures);
    expect(fixture.write(kContextM + 4U, 2U).ok() && fixture.write(kEnableS, 1U << 2U).ok(),
           "complete source2 并启用 S context",
           failures);
    fixture.sync();
    expect(fixture.pending(rvemu::core::InterruptCause::SupervisorExternal),
           "S context 必须投影 SEIP",
           failures);
    expect(fixture.write(kContextS, 2U).ok(), "写入 S threshold", failures);
    fixture.sync();
    expect(!fixture.pending(rvemu::core::InterruptCause::SupervisorExternal),
           "threshold 必须抑制相等优先级",
           failures);
}

/** 验证同优先级按小 ID 裁决，complete 后高电平 source 会重新进入 pending。 */
void test_tie_break_and_level_retrigger(int& failures) {
    Fixture fixture;
    expect(fixture.write(kPriority * 4U, 5U).ok() && fixture.write(kPriority * 5U, 5U).ok()
               && fixture.write(kEnableM, (1U << 4U) | (1U << 5U)).ok(),
           "配置同优先级 sources",
           failures);
    fixture.plic->set_source_level(4U, true);
    fixture.plic->set_source_level(5U, true);
    expect(fixture.claim(kContextM) == 4U, "同优先级必须选择较小 source ID", failures);
    expect(fixture.write(kContextM + 4U, 4U).ok(), "正确 context complete 必须成功", failures);
    expect((fixture.read(kPending) & (1U << 4U)) != 0U,
           "仍为高电平的 source complete 后必须重新 pending",
           failures);
}

/** 验证 gateway 已接受的请求不会因设备在 claim 前撤销电平而丢失。 */
void test_pending_latch(int& failures) {
    Fixture fixture;
    expect(fixture.write(kPriority * 6U, 1U).ok() && fixture.write(kEnableM, 1U << 6U).ok(),
           "配置锁存测试 source",
           failures);
    fixture.plic->set_source_level(6U, true);
    fixture.plic->set_source_level(6U, false);
    expect((fixture.read(kPending) & (1U << 6U)) != 0U,
           "claim 前撤销电平不得丢失已锁存请求",
           failures);
    expect(fixture.claim(kContextM) == 6U, "锁存请求必须仍可被 claim", failures);
    expect(fixture.write(kContextM + 4U, 6U).ok(), "锁存请求 complete 必须成功", failures);
    expect((fixture.read(kPending) & (1U << 6U)) == 0U,
           "低电平 source complete 后不得重新 pending",
           failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_plic(failures);
    test_tie_break_and_level_retrigger(failures);
    test_pending_latch(failures);
    return failures == 0 ? 0 : 1;
}
