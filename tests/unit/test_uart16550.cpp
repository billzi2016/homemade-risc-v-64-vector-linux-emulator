// 文件职责：通过真实总线验证 UART 16550A 寄存器复用、FIFO、LSR/IIR 和 PLIC 中断电平。
// 边界：测试不使用宿主终端后端，也不把单元级 UART 行为冒充 Linux 控制台验收。

#include "rvemu/bus/address_map.hpp"
#include "rvemu/bus/bus.hpp"
#include "rvemu/core/csr.hpp"
#include "rvemu/devices/plic.hpp"
#include "rvemu/devices/uart16550.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

constexpr std::uint64_t kUartBase = rvemu::bus::address_map::kUart.base;
constexpr std::uint64_t kPlicBase = rvemu::bus::address_map::kPlic.base;
constexpr std::uint32_t kUartSource = rvemu::devices::Uart16550::kDefaultInterruptSource;
constexpr std::uint64_t kPlicPriority = 4U * kUartSource;
constexpr std::uint64_t kPlicEnableM = 0x2000U;
constexpr std::uint64_t kPlicContextM = 0x200000U;

/** 注册真实 UART 与 PLIC，并强制所有寄存器访问走生产总线分发路径。 */
class Fixture final {
   public:
    Fixture()
        : uart(std::make_shared<rvemu::devices::Uart16550>()),
          plic(std::make_shared<rvemu::devices::Plic>()) {
        if (!bus.register_region(uart).ok() || !bus.register_region(plic).ok()) {
            throw std::runtime_error("UART/PLIC 注册失败");
        }
    }

    /** 对 UART 执行 8 位 MMIO 读，返回来宾可见字节。 */
    [[nodiscard]] std::uint8_t read8(std::uint64_t offset) {
        return static_cast<std::uint8_t>(
            bus.read(rvemu::bus::PhysicalAddress{kUartBase + offset},
                     rvemu::bus::AccessWidth::Byte,
                     rvemu::bus::AccessType::Load)
                .value);
    }

    /** 对 UART 执行 8 位 MMIO 写，返回结构化总线结果以验证拒绝路径。 */
    [[nodiscard]] rvemu::bus::AccessResult write8(std::uint64_t offset, std::uint8_t value) {
        return bus.write(rvemu::bus::PhysicalAddress{kUartBase + offset},
                         rvemu::bus::AccessWidth::Byte,
                         value,
                         rvemu::bus::AccessType::Store);
    }

    /** 对 PLIC 执行 32 位 MMIO 写，复用生产 PLIC 裁决状态。 */
    [[nodiscard]] rvemu::bus::AccessResult write_plic(std::uint64_t offset, std::uint32_t value) {
        return bus.write(rvemu::bus::PhysicalAddress{kPlicBase + offset},
                         rvemu::bus::AccessWidth::Word,
                         value,
                         rvemu::bus::AccessType::Store);
    }

    /** 同步 UART 到 PLIC 再同步 PLIC 到 CSR，用于观察 MEIP 电平。 */
    void sync() {
        uart->synchronize(*plic);
        plic->synchronize(csrs);
    }

    /** 读取 PLIC claim 寄存器，验证 UART source 是否真的进入外部中断仲裁。 */
    [[nodiscard]] std::uint64_t claim_machine() {
        return bus
            .read(rvemu::bus::PhysicalAddress{kPlicBase + kPlicContextM + 4U},
                  rvemu::bus::AccessWidth::Word,
                  rvemu::bus::AccessType::Load)
            .value;
    }

    /** 读取唯一 CSR pending 状态中的 MEIP 位。 */
    [[nodiscard]] bool meip() const {
        return (csrs.peek(rvemu::core::CsrAddress::Mip)
                & (1ULL << static_cast<std::uint64_t>(
                       rvemu::core::InterruptCause::MachineExternal)))
               != 0U;
    }

    rvemu::bus::Bus bus{};
    std::shared_ptr<rvemu::devices::Uart16550> uart;
    std::shared_ptr<rvemu::devices::Plic> plic;
    rvemu::core::CsrFile csrs{};
};

/** 累积断言失败，使同一执行能报告多个寄存器状态问题。 */
void expect(bool condition, const char* message, int& failures) {
    if (!condition) {
        ++failures;
        std::cerr << "失败：" << message << '\n';
    }
}

/** 验证复位值、DLAB 复用和 scratch/line control 等 Linux 8250 探测常用寄存器。 */
void test_registers_and_dlab(int& failures) {
    Fixture fixture;
    expect((fixture.read8(rvemu::devices::Uart16550::kLsrOffset) & 0x60U) == 0x60U,
           "复位后 THRE/TEMT 必须为 1",
           failures);
    expect(fixture.read8(rvemu::devices::Uart16550::kLcrOffset) == 0x03U,
           "复位 LCR 必须为 8N1",
           failures);
    expect(fixture.write8(rvemu::devices::Uart16550::kScrOffset, 0xA5U).ok()
               && fixture.read8(rvemu::devices::Uart16550::kScrOffset) == 0xA5U,
           "SCR 必须可读写",
           failures);

    expect(fixture.write8(rvemu::devices::Uart16550::kLcrOffset, 0x83U).ok(),
           "打开 DLAB",
           failures);
    expect(fixture.write8(rvemu::devices::Uart16550::kRbrThrDllOffset, 0x34U).ok()
               && fixture.write8(rvemu::devices::Uart16550::kIerDlmOffset, 0x12U).ok(),
           "写入 DLL/DLM",
           failures);
    expect(fixture.read8(rvemu::devices::Uart16550::kRbrThrDllOffset) == 0x34U
               && fixture.read8(rvemu::devices::Uart16550::kIerDlmOffset) == 0x12U,
           "DLAB 打开时偏移 0/1 必须访问 divisor latch",
           failures);
    expect(fixture.write8(rvemu::devices::Uart16550::kLcrOffset, 0x03U).ok()
               && fixture.write8(rvemu::devices::Uart16550::kIerDlmOffset, 0x0FU).ok()
               && fixture.read8(rvemu::devices::Uart16550::kIerDlmOffset) == 0x0FU,
           "DLAB 关闭后偏移 1 必须访问 IER",
           failures);
}

/** 验证接收 FIFO、RBR 消耗、overrun 错误与 LSR 读取清除错误状态。 */
void test_rx_lsr_and_overrun(int& failures) {
    Fixture fixture;
    expect(fixture.uart->push_rx('A') && fixture.uart->push_rx('B'),
           "接收 FIFO 应接受输入字节",
           failures);
    expect((fixture.read8(rvemu::devices::Uart16550::kLsrOffset) & 0x01U) != 0U,
           "FIFO 非空时 LSR.DR 必须置位",
           failures);
    expect(fixture.read8(rvemu::devices::Uart16550::kRbrThrDllOffset) == 'A'
               && fixture.read8(rvemu::devices::Uart16550::kRbrThrDllOffset) == 'B',
           "RBR 必须按 FIFO 顺序消耗字节",
           failures);
    expect((fixture.read8(rvemu::devices::Uart16550::kLsrOffset) & 0x01U) == 0U,
           "FIFO 清空后 LSR.DR 必须撤销",
           failures);

    for (std::uint8_t value = 0U; value < 16U; ++value) {
        expect(fixture.uart->push_rx(value), "填充接收 FIFO", failures);
    }
    expect(!fixture.uart->push_rx(0xFFU), "FIFO 满时必须拒绝覆盖旧字节", failures);
    expect((fixture.read8(rvemu::devices::Uart16550::kLsrOffset) & 0x02U) != 0U,
           "FIFO overrun 必须反映到 LSR.OE",
           failures);
    expect((fixture.read8(rvemu::devices::Uart16550::kLsrOffset) & 0x02U) == 0U,
           "读取 LSR 后 overrun 错误必须清除",
           failures);
}

/** 验证 THR 写入进入发送 FIFO，并且 FCR 清除发送 FIFO 后 THRE/TEMT 保持可见。 */
void test_tx_and_fifo_control(int& failures) {
    Fixture fixture;
    std::uint8_t byte = 0U;
    expect(fixture.write8(rvemu::devices::Uart16550::kRbrThrDllOffset, 'Z').ok(),
           "写 THR 必须成功",
           failures);
    expect(fixture.uart->pop_tx(byte) && byte == 'Z', "THR 字节必须进入发送 FIFO", failures);
    expect(!fixture.uart->pop_tx(byte), "发送 FIFO 消费后应为空", failures);
    expect(fixture.write8(rvemu::devices::Uart16550::kRbrThrDllOffset, 'Q').ok()
               && fixture.write8(rvemu::devices::Uart16550::kIirFcrOffset, 0x07U).ok(),
           "FCR 必须支持清空 FIFO",
           failures);
    expect(!fixture.uart->pop_tx(byte), "FCR clear TX 必须丢弃待发送字节", failures);
    expect((fixture.read8(rvemu::devices::Uart16550::kLsrOffset) & 0x60U) == 0x60U,
           "清空后 THRE/TEMT 必须保持置位",
           failures);
}

/** 验证 IER/IIR 中断原因与 PLIC source 电平，不复制 UART 或 PLIC 的内部选择逻辑。 */
void test_interrupt_projection(int& failures) {
    Fixture fixture;
    expect(fixture.write_plic(kPlicPriority, 1U).ok()
               && fixture.write_plic(kPlicEnableM, 1U << kUartSource).ok(),
           "配置 PLIC UART source",
           failures);
    fixture.sync();
    expect(!fixture.meip(), "IER 全关时 UART 不得投影外部中断", failures);

    expect(fixture.write8(rvemu::devices::Uart16550::kIerDlmOffset, 0x01U).ok()
               && fixture.uart->push_rx('x'),
           "启用接收中断并注入字节",
           failures);
    fixture.sync();
    expect(fixture.meip() && fixture.claim_machine() == kUartSource,
           "接收可用必须经 PLIC 投影为 MEIP",
           failures);
    expect((fixture.read8(rvemu::devices::Uart16550::kIirFcrOffset) & 0x0FU) == 0x04U,
           "IIR 必须报告接收可用原因",
           failures);
    expect(fixture.read8(rvemu::devices::Uart16550::kRbrThrDllOffset) == 'x',
           "读 RBR 必须清除接收条件",
           failures);

    Fixture tx_fixture;
    expect(tx_fixture.write_plic(kPlicPriority, 1U).ok()
               && tx_fixture.write_plic(kPlicEnableM, 1U << kUartSource).ok()
               && tx_fixture.write8(rvemu::devices::Uart16550::kIerDlmOffset, 0x02U).ok(),
           "启用发送空中断",
           failures);
    tx_fixture.sync();
    expect(tx_fixture.meip()
               && (tx_fixture.read8(rvemu::devices::Uart16550::kIirFcrOffset) & 0x0FU) == 0x02U,
           "THR 空时必须报告发送空中断",
           failures);
}

/** 验证非法宽度、越界寄存器和原子 MMIO 都返回结构化错误。 */
void test_invalid_access(int& failures) {
    Fixture fixture;
    expect(!fixture.bus
                .read(rvemu::bus::PhysicalAddress{kUartBase},
                      rvemu::bus::AccessWidth::Word,
                      rvemu::bus::AccessType::Load)
                .ok(),
           "UART 必须拒绝 32 位访问",
           failures);
    expect(!fixture.bus
                .write(rvemu::bus::PhysicalAddress{kUartBase + 8U},
                       rvemu::bus::AccessWidth::Byte,
                       0U,
                       rvemu::bus::AccessType::Store)
                .ok(),
           "UART 必须拒绝未实现偏移",
           failures);
    expect(!fixture.bus
                .compare_exchange(rvemu::bus::PhysicalAddress{kUartBase},
                                  rvemu::bus::AccessWidth::Byte,
                                  0U,
                                  1U)
                .ok(),
           "UART 必须拒绝原子 MMIO",
           failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_registers_and_dlab(failures);
    test_rx_lsr_and_overrun(failures);
    test_tx_and_fifo_control(failures);
    test_interrupt_projection(failures);
    test_invalid_access(failures);
    return failures == 0 ? 0 : 1;
}
