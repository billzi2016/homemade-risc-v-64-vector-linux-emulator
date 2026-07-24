// 文件职责：声明单 Hart 运行循环的最小生产编排入口，串联 CPU、CLINT、PLIC、UART 和终端后端。
// 边界：本模块不解析 CLI、不装载固件/内核/磁盘，也不实现具体设备寄存器或 CPU 指令语义。
// 主要依赖：core::Cpu、devices::{Clint,Plic,Uart16550} 与 platform::TerminalBackend。
// 关键不变量：每轮只通过设备公开同步入口投影中断，CPU 仍由唯一 step/trap 路径推进。

#pragma once

#include "rvemu/core/cpu.hpp"
#include "rvemu/devices/clint.hpp"
#include "rvemu/devices/plic.hpp"
#include "rvemu/devices/uart16550.hpp"
#include "rvemu/platform/terminal.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace rvemu::runtime {

struct EventLoopOptions final {
    std::uint64_t clint_ticks_per_iteration{1U};
    std::size_t max_terminal_rx_bytes{64U};
    std::size_t max_uart_tx_bytes{64U};
};

struct EventLoopIterationResult final {
    bool cpu_retired{false};
    bool cpu_stalled{false};
    bool interrupt_taken{false};
    bool trap_taken{false};
    std::size_t terminal_rx_bytes{0U};
    std::size_t terminal_tx_bytes{0U};
    std::optional<core::TrapDelivery> delivery{};
    std::optional<core::Trap> synchronous_trap{};
    platform::TerminalIoStatus terminal_input_status{platform::TerminalIoStatus::Ready};
    platform::TerminalIoStatus terminal_output_status{platform::TerminalIoStatus::Ready};
    int terminal_input_errno{0};
    int terminal_output_errno{0};
};

/**
 * 最小单 Hart 事件循环。
 *
 * 调用方持有所有硬件和宿主后端对象；本类不关闭资源、不改变终端模式，只在指令边界
 * 编排设备事件和 CPU 步进。测试和未来 CLI 主循环必须复用此入口，避免多套 tick/中断逻辑。
 */
class EventLoop final {
   public:
    EventLoop(core::Cpu& cpu,
              devices::Clint& clint,
              devices::Plic& plic,
              devices::Uart16550& uart,
              platform::TerminalBackend& terminal,
              EventLoopOptions options = {}) noexcept;

    /** 执行一轮有界设备服务和最多一次 CPU step/trap；不会阻塞等待宿主输入。 */
    [[nodiscard]] EventLoopIterationResult run_once();

   private:
    /** 将终端中当前可读字节送入 UART RX FIFO；满 FIFO 或 WouldBlock 会停止本轮读取。 */
    void service_terminal_input(EventLoopIterationResult& result) noexcept;
    /** 将 UART TX FIFO 中的字节按有界预算写到终端，短写时保留待发送字节。 */
    void service_uart_output(EventLoopIterationResult& result) noexcept;
    /** 在同一指令边界推进 CLINT 并把 CLINT/PLIC/UART 电平投影到唯一 CSR pending。 */
    void synchronize_devices() noexcept;
    /** 先处理 pending interrupt，再执行一步 CPU；同步异常由唯一 take_trap 入口提交。 */
    void step_cpu(EventLoopIterationResult& result);

    core::Cpu& cpu_;
    devices::Clint& clint_;
    devices::Plic& plic_;
    devices::Uart16550& uart_;
    platform::TerminalBackend& terminal_;
    EventLoopOptions options_;
    std::deque<std::uint8_t> pending_tx_{};
};

}  // namespace rvemu::runtime
