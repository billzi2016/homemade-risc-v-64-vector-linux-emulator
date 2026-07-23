// 文件职责：集成验证单 Hart 事件循环中的 CLINT、PLIC、UART、终端后端与 CPU 中断入口。
// 边界：测试使用伪终端和短机器码，不启动 OpenSBI/Linux，也不把单元集成测试冒充系统验收。

#include "rvemu/bus/address_map.hpp"
#include "rvemu/bus/bus.hpp"
#include "rvemu/core/cpu.hpp"
#include "rvemu/core/csr.hpp"
#include "rvemu/devices/clint.hpp"
#include "rvemu/devices/plic.hpp"
#include "rvemu/devices/uart16550.hpp"
#include "rvemu/memory/physical_memory.hpp"
#include "rvemu/platform/terminal.hpp"
#include "rvemu/runtime/event_loop.hpp"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {

constexpr std::uint64_t kRamBase = rvemu::bus::address_map::kDefaultRam.base;
constexpr std::uint64_t kTrapBase = kRamBase + 0x400U;
constexpr std::uint64_t kInterruptFlag = 1ULL << 63U;
constexpr std::uint32_t kUartSource = rvemu::devices::Uart16550::kDefaultInterruptSource;

[[nodiscard]] constexpr std::uint64_t bit(std::uint8_t index) noexcept {
    return 1ULL << index;
}

/** 管理事件循环测试专用 pty，确保不会修改调用者当前 shell 终端。 */
class PseudoTerminal final {
   public:
    PseudoTerminal() {
        master_ = ::posix_openpt(O_RDWR | O_NOCTTY);
        if (master_ < 0 || ::grantpt(master_) != 0 || ::unlockpt(master_) != 0) {
            throw std::runtime_error("创建事件循环伪终端失败");
        }
        char* slave_name = ::ptsname(master_);
        if (slave_name == nullptr) {
            throw std::runtime_error("读取事件循环伪终端 slave 名称失败");
        }
        slave_ = ::open(slave_name, O_RDWR | O_NOCTTY);
        if (slave_ < 0) {
            throw std::runtime_error("打开事件循环伪终端 slave 失败");
        }
    }

    ~PseudoTerminal() {
        if (slave_ >= 0) {
            static_cast<void>(::close(slave_));
        }
        if (master_ >= 0) {
            static_cast<void>(::close(master_));
        }
    }

    PseudoTerminal(const PseudoTerminal&) = delete;
    PseudoTerminal& operator=(const PseudoTerminal&) = delete;
    PseudoTerminal(PseudoTerminal&&) = delete;
    PseudoTerminal& operator=(PseudoTerminal&&) = delete;

    [[nodiscard]] int master() const noexcept {
        return master_;
    }
    [[nodiscard]] int slave() const noexcept {
        return slave_;
    }

   private:
    int master_{-1};
    int slave_{-1};
};

/** 累积断言失败，使同一次集成运行报告多个设备同步问题。 */
void expect(bool condition, const std::string& message, int& failures) {
    if (!condition) {
        ++failures;
        std::cerr << "失败：" << message << '\n';
    }
}

void write_csr(rvemu::core::CsrFile& csrs,
               rvemu::core::CsrAddress address,
               std::uint64_t value) {
    const auto result = csrs.access(rvemu::core::CsrAccessRequest{
        address,
        rvemu::core::PrivilegeMode::Machine,
        false,
        true,
        rvemu::core::CsrModifyOperation::Replace,
        value,
    });
    if (!result.success) {
        throw std::runtime_error("事件循环测试写 CSR 失败");
    }
}

/** 注册真实 RAM、CLINT、PLIC、UART，并提供 MMIO 配置辅助。 */
class Fixture final {
   public:
    Fixture()
        : ram(std::make_shared<rvemu::memory::PhysicalMemory>(
              rvemu::bus::PhysicalAddress{kRamBase}, 0x4000U, "event-loop-ram")),
          clint(std::make_shared<rvemu::devices::Clint>()),
          plic(std::make_shared<rvemu::devices::Plic>()),
          uart(std::make_shared<rvemu::devices::Uart16550>()),
          cpu(bus),
          terminal(pty.slave(), pty.slave()),
          loop(cpu, *clint, *plic, *uart, terminal, rvemu::runtime::EventLoopOptions{1U, 16U, 16U}) {
        if (!bus.register_region(ram).ok() || !bus.register_region(clint).ok()
            || !bus.register_region(plic).ok() || !bus.register_region(uart).ok()) {
            throw std::runtime_error("事件循环测试设备注册失败");
        }
        cpu.state().reset(kRamBase);
        const auto raw = terminal.activate_raw();
        if (!raw.ok()) {
            throw std::runtime_error("事件循环测试终端 Raw 模式失败");
        }
    }

    void configure_trap(std::uint64_t mie_mask) {
        write_csr(cpu.state().csrs(), rvemu::core::CsrAddress::Mtvec, kTrapBase);
        write_csr(cpu.state().csrs(), rvemu::core::CsrAddress::Mie, mie_mask);
        write_csr(cpu.state().csrs(), rvemu::core::CsrAddress::Mstatus, bit(3U));
        cpu.state().set_program_counter(kRamBase + 0x100U);
    }

    void write32(std::uint64_t address, std::uint32_t value) {
        const auto result = bus.write(rvemu::bus::PhysicalAddress{address},
                                      rvemu::bus::AccessWidth::Word,
                                      value,
                                      rvemu::bus::AccessType::Initialization);
        if (!result.ok()) {
            throw std::runtime_error("事件循环测试写内存失败");
        }
    }

    [[nodiscard]] bool mmio_write(std::uint64_t address,
                                  rvemu::bus::AccessWidth width,
                                  std::uint64_t value) {
        return bus.write(rvemu::bus::PhysicalAddress{address},
                         width,
                         value,
                         rvemu::bus::AccessType::Store)
            .ok();
    }

    rvemu::bus::Bus bus{};
    std::shared_ptr<rvemu::memory::PhysicalMemory> ram;
    std::shared_ptr<rvemu::devices::Clint> clint;
    std::shared_ptr<rvemu::devices::Plic> plic;
    std::shared_ptr<rvemu::devices::Uart16550> uart;
    rvemu::core::Cpu cpu;
    PseudoTerminal pty;
    rvemu::platform::TerminalBackend terminal;
    rvemu::runtime::EventLoop loop;
};

/** 验证终端输入进入 UART RX FIFO，并经 UART->PLIC->CSR 触发机器外部中断。 */
void test_uart_input_external_interrupt(int& failures) {
    Fixture fixture;
    fixture.configure_trap(bit(11U));
    expect(fixture.mmio_write(rvemu::bus::address_map::kUart.base + 1U,
                              rvemu::bus::AccessWidth::Byte,
                              1U),
           "启用 UART 接收中断",
           failures);
    expect(fixture.mmio_write(rvemu::bus::address_map::kPlic.base + 4U * kUartSource,
                              rvemu::bus::AccessWidth::Word,
                              1U),
           "配置 UART PLIC priority",
           failures);
    expect(fixture.mmio_write(rvemu::bus::address_map::kPlic.base + 0x2000U,
                              rvemu::bus::AccessWidth::Word,
                              1U << kUartSource),
           "启用 UART PLIC M context",
           failures);

    const char input = 'A';
    expect(::write(fixture.pty.master(), &input, 1U) == 1, "向终端注入一个输入字节", failures);
    const auto result = fixture.loop.run_once();
    expect(result.terminal_rx_bytes == 1U, "事件循环必须读取一个终端输入字节", failures);
    expect(result.interrupt_taken && result.delivery.has_value(), "UART 输入必须触发外部中断", failures);
    expect(fixture.cpu.state().program_counter() == kTrapBase,
           "机器外部中断必须进入 mtvec",
           failures);
    expect(fixture.cpu.state().csrs().peek(rvemu::core::CsrAddress::Mcause)
               == (kInterruptFlag | 11U),
           "mcause 必须记录机器外部中断",
           failures);
    const auto rbr = fixture.bus
                         .read(rvemu::bus::PhysicalAddress{rvemu::bus::address_map::kUart.base},
                               rvemu::bus::AccessWidth::Byte,
                               rvemu::bus::AccessType::Load)
                         .value;
    expect(rbr == static_cast<std::uint8_t>('A'), "UART RBR 必须保存终端输入字节", failures);
}

/** 验证来宾写 THR 后，事件循环把 UART TX 字节转发到宿主终端输出。 */
void test_uart_output_to_terminal(int& failures) {
    Fixture fixture;
    expect(fixture.mmio_write(rvemu::bus::address_map::kUart.base,
                              rvemu::bus::AccessWidth::Byte,
                              static_cast<std::uint8_t>('Z')),
           "来宾写 UART THR",
           failures);
    const auto result = fixture.loop.run_once();
    expect(result.terminal_tx_bytes == 1U, "事件循环必须转发一个 UART 输出字节", failures);
    char output = 0;
    expect(::read(fixture.pty.master(), &output, 1U) == 1 && output == 'Z',
           "伪终端 master 必须收到 UART 输出字节",
           failures);
}

/** 验证事件循环推进 CLINT tick 并通过 CSR/CPU 入口提交机器定时器中断。 */
void test_clint_timer_interrupt(int& failures) {
    Fixture fixture;
    fixture.configure_trap(bit(7U));
    expect(fixture.mmio_write(rvemu::bus::address_map::kClint.base + 0x4000U,
                              rvemu::bus::AccessWidth::DoubleWord,
                              1U),
           "设置 mtimecmp=1",
           failures);
    const auto result = fixture.loop.run_once();
    expect(result.interrupt_taken && result.delivery.has_value(),
           "CLINT tick 必须触发机器定时器中断",
           failures);
    expect(fixture.cpu.state().program_counter() == kTrapBase,
           "机器定时器中断必须进入 mtvec",
           failures);
    expect(fixture.cpu.state().csrs().peek(rvemu::core::CsrAddress::Mcause)
               == (kInterruptFlag | 7U),
           "mcause 必须记录机器定时器中断",
           failures);
}

}  // namespace

int main() {
    int failures = 0;
    try {
        test_uart_input_external_interrupt(failures);
        test_uart_output_to_terminal(failures);
        test_clint_timer_interrupt(failures);
    } catch (const std::exception& exception) {
        std::cerr << "事件循环测试基础设施失败：" << exception.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
