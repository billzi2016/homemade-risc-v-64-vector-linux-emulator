// 文件职责：提供生产模拟器命令入口，按 CLI 规格先做无副作用参数校验。
// 边界：当前入口不伪造 Linux 启动；整机运行在后续 SYS 验证具备真实产物后接入。

#include "rvemu/runtime/cli.hpp"
#include "rvemu/runtime/host_signal.hpp"
#include "rvemu/runtime/machine.hpp"
#include "rvemu/runtime/runner.hpp"

#include <iostream>
#include <unistd.h>

int main(int argc, char** argv) {
    const auto parsed = rvemu::runtime::parse_cli(argc, argv);
    if (!parsed.ok()) {
        std::cerr << parsed.error << '\n' << rvemu::runtime::cli_usage(argv[0]);
        return static_cast<int>(rvemu::runtime::ExitCode::CliUsage);
    }
    if (parsed.options->help) {
        std::cout << rvemu::runtime::cli_usage(argv[0]);
        return static_cast<int>(rvemu::runtime::ExitCode::Success);
    }
    const auto machine = rvemu::runtime::build_machine(rvemu::runtime::MachineConfig{*parsed.options, {}});
    if (!machine.ok()) {
        std::cerr << machine.error << '\n';
        return static_cast<int>(machine.exit_code);
    }
    const auto signals = rvemu::runtime::install_host_signal_handlers();
    if (!signals.ok()) {
        std::cerr << signals.detail << '\n';
        return static_cast<int>(rvemu::runtime::ExitCode::Resource);
    }

    rvemu::platform::TerminalBackend terminal{STDIN_FILENO, STDOUT_FILENO};
    const auto raw = terminal.activate_raw();
    if (!raw.ok()) {
        static_cast<void>(rvemu::runtime::restore_host_signal_handlers());
        std::cerr << raw.detail << '\n';
        return static_cast<int>(rvemu::runtime::ExitCode::Resource);
    }

    const auto run = rvemu::runtime::run_machine(*machine.machine, terminal);
    const auto terminal_restore = terminal.restore();
    const auto signal_restore = rvemu::runtime::restore_host_signal_handlers();
    if (!terminal_restore.ok()) {
        std::cerr << terminal_restore.detail << '\n';
        return static_cast<int>(rvemu::runtime::ExitCode::Resource);
    }
    if (!signal_restore.ok()) {
        std::cerr << signal_restore.detail << '\n';
        return static_cast<int>(rvemu::runtime::ExitCode::Resource);
    }
    if (!run.ok()) {
        std::cerr << run.error << '\n';
    }
    return static_cast<int>(run.exit_code);
}
