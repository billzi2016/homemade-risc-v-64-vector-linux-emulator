// 文件职责：提供生产模拟器命令入口，按 CLI 规格先做无副作用参数校验。
// 边界：当前入口不伪造 Linux 启动；整机运行在后续 SYS 验证具备真实产物后接入。

#include "rvemu/runtime/cli.hpp"

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
    std::cerr << "启动运行链路尚未完成真实系统验收，拒绝伪造 OpenSBI/Linux 输出。\n";
    return static_cast<int>(rvemu::runtime::ExitCode::Internal);
}
