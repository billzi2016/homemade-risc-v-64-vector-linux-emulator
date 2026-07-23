// 文件职责：提供生产模拟器命令入口，按 CLI 规格先做无副作用参数校验。
// 边界：当前入口不伪造 Linux 启动；整机运行在后续 SYS 验证具备真实产物后接入。

#include "rvemu/runtime/cli.hpp"
#include "rvemu/runtime/machine.hpp"

#include <iostream>

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
    std::cerr << "整机资源和启动镜像校验已通过，但运行循环尚未接入生产入口；拒绝伪造 OpenSBI/Linux 输出。\n";
    return static_cast<int>(rvemu::runtime::ExitCode::Internal);
}
