// 文件职责：实现生产运行循环的设备请求服务、事件循环调用和运行期错误映射。
// 边界：本文件不绕过 CPU/设备公开接口，不伪造来宾输出，也不处理 Linux TAP 网络。

#include "rvemu/runtime/runner.hpp"

#include "rvemu/runtime/event_loop.hpp"
#include "rvemu/runtime/host_signal.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace rvemu::runtime {
namespace {

[[nodiscard]] bool boot_trace_enabled() noexcept {
    const char* value = std::getenv("RVEMU_BOOT_TRACE");
    return value != nullptr && value[0] == '1' && value[1] == '\0';
}

void maybe_trace_boot_pc(const Machine& machine, std::uint64_t iterations) {
    static bool enabled = boot_trace_enabled();
    if (!enabled) {
        return;
    }
    static std::uint64_t last_s3 = machine.cpu.state().integer(19U);
    static std::uint32_t s3_change_count = 0U;
    static bool trace_s3_changes = false;
    struct TracePoint final {
        std::uint64_t pc;
        const char* label;
        bool printed;
    };
    static std::array<TracePoint, 20U> points{{
        {0x8000'0C04ULL, "opensbi_read_mhartid", false},
        {0x8000'0C12ULL, "opensbi_hartid_saved", false},
        {0x8000'0C98ULL, "sbi_domain_init_before", false},
        {0x8000'0CA0ULL, "sbi_domain_init_after", false},
        {0x8000'0CFEULL, "init_before_domain_context", false},
        {0x8000'0D00ULL, "init_before_domain_context_arg", false},
        {0x8000'0D02ULL, "init_domain_context_call", false},
        {0x8000'0D06ULL, "init_after_boot_print", false},
        {0x8000'0D10ULL, "init_after_timer_init", false},
        {0x8000'0EAEULL, "init_after_irqchip", false},
        {0x8000'0ED6ULL, "init_after_ipi", false},
        {0x8000'0F04ULL, "init_after_tlb", false},
        {0x8000'0F34ULL, "init_after_timer", false},
        {0x8000'0F5CULL, "init_after_fwft", false},
        {0x8000'0F84ULL, "init_after_mpxy", false},
        {0x8000'0FCAULL, "init_after_pmu", false},
        {0x8000'0FF2ULL, "sbi_domain_finalize_before", false},
        {0x8000'CE2EULL, "sbi_domain_finalize_entry", false},
        {0x8000'CF32ULL, "sbi_domain_boot_hart_compare", false},
        {0x8000'CF7EULL, "sbi_hsm_hart_start_call", false},
    }};

    const auto pc = machine.cpu.state().program_counter();
    if (iterations != 0U && iterations % 5'000'000U == 0U) {
        const auto& state = machine.cpu.state();
        const auto& csrs = state.csrs();
        std::cerr << "RVEMU_BOOT_TRACE sample iter=" << std::dec << iterations
                  << " pc=0x" << std::hex << pc
                  << " ra=0x" << state.integer(1U)
                  << " sp=0x" << state.integer(2U)
                  << " a0=0x" << state.integer(10U)
                  << " a1=0x" << state.integer(11U)
                  << " satp=0x" << csrs.peek(core::CsrAddress::Satp)
                  << " mie=0x" << csrs.peek(core::CsrAddress::Mie)
                  << " mip=0x" << csrs.peek(core::CsrAddress::Mip)
                  << " mcounteren=0x" << csrs.peek(core::CsrAddress::Mcounteren)
                  << " priv=" << std::dec << static_cast<unsigned>(state.privilege())
                  << " wfi=" << (state.waiting_for_interrupt() ? 1 : 0) << '\n';
    }

    for (auto& point : points) {
        if (point.printed || point.pc != pc) {
            continue;
        }
        point.printed = true;
        const auto& state = machine.cpu.state();
        std::cerr << "RVEMU_BOOT_TRACE " << point.label << " pc=0x" << std::hex << pc
                  << " a0=0x" << state.integer(10U)
                  << " a1=0x" << state.integer(11U)
                  << " a2=0x" << state.integer(12U)
                  << " a3=0x" << state.integer(13U)
                  << " a4=0x" << state.integer(14U)
                  << " s3=0x" << state.integer(19U)
                  << " s5=0x" << state.integer(21U)
                  << " priv=" << static_cast<unsigned>(state.privilege()) << std::dec << '\n';
        if (point.pc == 0x8000'0CA0ULL) {
            trace_s3_changes = true;
            last_s3 = state.integer(19U);
        }
    }

    const auto current_s3 = machine.cpu.state().integer(19U);
    const auto s3_in_scratch_area = current_s3 >= 0x8004'7000ULL && current_s3 < 0x8004'8000ULL;
    if (trace_s3_changes && current_s3 != last_s3 && pc >= 0x8000'0C00ULL
        && pc < 0x8001'0000ULL && (s3_in_scratch_area || s3_change_count < 8U)) {
        ++s3_change_count;
        const auto& state = machine.cpu.state();
        std::cerr << "RVEMU_BOOT_TRACE s3_change pc=0x" << std::hex << pc
                  << " old=0x" << last_s3
                  << " new=0x" << current_s3
                  << " a0=0x" << state.integer(10U)
                  << " ra=0x" << state.integer(1U)
                  << " sp=0x" << state.integer(2U)
                  << " s0=0x" << state.integer(8U)
                  << " s1=0x" << state.integer(9U)
                  << " s2=0x" << state.integer(18U)
                  << " s5=0x" << state.integer(21U) << std::dec << '\n';
    }
    last_s3 = current_s3;
}

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
        maybe_trace_boot_pc(machine, iterations);
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
