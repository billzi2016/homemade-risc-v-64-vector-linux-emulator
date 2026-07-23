// 文件职责：声明宿主信号到运行循环停止请求的最小桥接层。
// 边界：本模块不关闭文件、不恢复终端、不做日志 I/O；清理仍由主线程资源守卫完成。
// 主要依赖：POSIX sigaction；信号处理器只写 sig_atomic_t 标志。
// 关键不变量：handler 内只执行 async-signal-safe 的标志写入，避免破坏终端和磁盘状态。

#pragma once

#include <string>

namespace rvemu::runtime {

struct HostSignalResult final {
    bool success{true};
    int errno_value{0};
    std::string detail{};

    [[nodiscard]] bool ok() const noexcept {
        return success;
    }
};

/** 安装 SIGINT/SIGTERM 停止标志处理器；失败时不进入运行循环。 */
[[nodiscard]] HostSignalResult install_host_signal_handlers() noexcept;

/** 恢复安装前的 SIGINT/SIGTERM 行为；重复调用安全。 */
[[nodiscard]] HostSignalResult restore_host_signal_handlers() noexcept;

/** 主循环轮询该标志决定是否执行有序退出清理。 */
[[nodiscard]] bool host_stop_requested() noexcept;

/** 测试和新一次运行前清除停止标志；不修改已安装 handler。 */
void clear_host_stop_request() noexcept;

}  // namespace rvemu::runtime
