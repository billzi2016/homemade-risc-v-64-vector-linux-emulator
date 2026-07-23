// 文件职责：实现生产运行循环的设备请求服务、事件循环调用和运行期错误映射。
// 边界：本文件不绕过 CPU/设备公开接口，不伪造来宾输出，也不处理 Linux TAP 网络。

#include "rvemu/runtime/runner.hpp"

#include "rvemu/runtime/event_loop.hpp"
#include "rvemu/runtime/host_signal.hpp"

namespace rvemu::runtime {
namespace {

[[nodiscard]] bool terminal_failed(const EventLoopIterationResult& result) noexcept {
    return result.terminal_input_status == platform::TerminalIoStatus::Error
           || result.terminal_output_status == platform::TerminalIoStatus::Error
           || result.terminal_output_status == platform::TerminalIoStatus::Closed;
}

}  // namespace

RunResult run_machine(Machine& machine,
                      platform::TerminalBackend& terminal,
                      const RunOptions& options) {
    EventLoop loop{machine.cpu,
                   *machine.clint,
                   *machine.plic,
                   *machine.uart,
                   terminal,
                   EventLoopOptions{options.clint_ticks_per_iteration, 64U, 64U}};
    std::uint64_t iterations = 0U;
    while (!host_stop_requested()) {
        const auto block_status = machine.block->process_one(machine.bus);
        if (block_status == devices::VirtioBlockProcessStatus::QueueError) {
            return RunResult{ExitCode::RuntimeIo, iterations, "VirtIO-Blk 队列处理失败"};
        }

        const auto iteration = loop.run_once();
        ++iterations;
        if (terminal_failed(iteration)) {
            return RunResult{ExitCode::RuntimeIo, iterations, "宿主终端 I/O 失败或输出端关闭"};
        }
        if (options.max_iterations != 0U && iterations >= options.max_iterations) {
            return RunResult{ExitCode::Success, iterations, {}};
        }
    }
    return RunResult{ExitCode::Success, iterations, {}};
}

}  // namespace rvemu::runtime
