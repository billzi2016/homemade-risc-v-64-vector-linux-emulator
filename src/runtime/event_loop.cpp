// 文件职责：实现单 Hart 运行循环的设备服务、中断同步和 CPU 步进编排。
// 边界：本文件不拥有文件描述符、不创建 TAP/磁盘后端，也不绕过 CPU、CLINT、PLIC 或 UART 的生产接口。
// 主要依赖：TerminalBackend 非阻塞 I/O、设备 synchronize 入口和 Cpu step/trap 入口。
// 关键不变量：同步异常和中断都必须由 CPU 的唯一 Trap 入口提交，设备只改变自身电平。

#include "rvemu/runtime/event_loop.hpp"

namespace rvemu::runtime {

EventLoop::EventLoop(core::Cpu& cpu,
                     devices::Clint& clint,
                     devices::Plic& plic,
                     devices::Uart16550& uart,
                     platform::TerminalBackend& terminal,
                     EventLoopOptions options) noexcept
    : cpu_(cpu), clint_(clint), plic_(plic), uart_(uart), terminal_(terminal), options_(options) {
}

EventLoopIterationResult EventLoop::run_once() {
    EventLoopIterationResult result{};
    service_terminal_input(result);
    service_uart_output(result);
    synchronize_devices();
    step_cpu(result);
    return result;
}

void EventLoop::service_terminal_input(EventLoopIterationResult& result) noexcept {
    for (std::size_t count = 0U; count < options_.max_terminal_rx_bytes; ++count) {
        std::uint8_t byte = 0U;
        const auto input = terminal_.read_byte(byte);
        result.terminal_input_status = input.status;
        result.terminal_input_errno = input.errno_value;
        if (input.status != platform::TerminalIoStatus::Ready) {
            return;
        }
        if (!uart_.push_rx(byte)) {
            return;
        }
        ++result.terminal_rx_bytes;
    }
}

void EventLoop::service_uart_output(EventLoopIterationResult& result) noexcept {
    for (std::size_t count = 0U; count < options_.max_uart_tx_bytes; ++count) {
        if (pending_tx_.empty()) {
            std::uint8_t byte = 0U;
            if (!uart_.pop_tx(byte)) {
                return;
            }
            pending_tx_.push_back(byte);
        }

        const auto byte = pending_tx_.front();
        const auto output = terminal_.write_bytes(&byte, 1U);
        result.terminal_output_status = output.status;
        result.terminal_output_errno = output.errno_value;
        if (output.status != platform::TerminalIoStatus::Ready || output.byte_count == 0U) {
            return;
        }
        pending_tx_.pop_front();
        ++result.terminal_tx_bytes;
    }
}

void EventLoop::synchronize_devices() noexcept {
    clint_.advance(options_.clint_ticks_per_iteration);
    clint_.synchronize(cpu_.state().csrs());
    uart_.synchronize(plic_);
    plic_.synchronize(cpu_.state().csrs());
}

void EventLoop::step_cpu(EventLoopIterationResult& result) {
    if (auto delivery = cpu_.take_pending_interrupt(); delivery.has_value()) {
        result.interrupt_taken = true;
        result.delivery = *delivery;
        return;
    }

    const auto step = cpu_.step();
    result.cpu_retired = step.retired;
    result.cpu_stalled = step.stalled;
    if (step.trap.has_value()) {
        result.trap_taken = true;
        result.synchronous_trap = *step.trap;
        result.delivery = cpu_.take_trap(*step.trap);
    }
}

}  // namespace rvemu::runtime
