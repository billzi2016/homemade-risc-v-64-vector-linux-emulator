// 文件职责：声明生产运行循环入口，连接 Machine、TerminalBackend、VirtIO-Blk 和 EventLoop。
// 边界：本模块不解析 CLI、不打开镜像、不生成 OpenSBI/Linux 日志，也不实现网络后端。
// 主要依赖：Machine 持有的真实设备和 host_signal 停止标志。
// 关键不变量：所有 CPU 步进都通过唯一 EventLoop；块设备完成只通过 VirtIO queue/PLIC 反馈。

#pragma once

#include "rvemu/platform/terminal.hpp"
#include "rvemu/runtime/cli.hpp"
#include "rvemu/runtime/machine.hpp"

#include <cstddef>
#include <cstdint>

namespace rvemu::runtime {

struct RunOptions final {
    std::uint64_t clint_ticks_per_iteration{1U};
    std::size_t max_iterations{0U};
};

struct RunResult final {
    ExitCode exit_code{ExitCode::Success};
    std::uint64_t iterations{0U};
    std::string error{};

    [[nodiscard]] bool ok() const noexcept {
        return exit_code == ExitCode::Success;
    }
};

/** 运行 macOS `--net none` 生产循环，直到宿主停止请求、运行期 I/O 错误或测试迭代上限。 */
[[nodiscard]] RunResult run_machine(Machine& machine,
                                    platform::TerminalBackend& terminal,
                                    const RunOptions& options = {});

}  // namespace rvemu::runtime
