// 文件职责：实现宿主 SIGINT/SIGTERM 的最小安全处理和恢复。
// 边界：信号处理器不调用 C++ 分配、锁、I/O 或终端恢复；主线程负责实际清理。

#include "rvemu/runtime/host_signal.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>

namespace rvemu::runtime {
namespace {

volatile sig_atomic_t g_stop_requested = 0;
bool g_installed = false;
struct sigaction g_previous_int {};
struct sigaction g_previous_term {};

void handle_stop_signal(int signal_number) noexcept {
    static_cast<void>(signal_number);
    g_stop_requested = 1;
}

[[nodiscard]] HostSignalResult failure(int errno_value, const char* detail) {
    return HostSignalResult{false, errno_value, detail};
}

[[nodiscard]] HostSignalResult success() noexcept {
    return HostSignalResult{};
}

}  // namespace

HostSignalResult install_host_signal_handlers() noexcept {
    if (g_installed) {
        return success();
    }
    clear_host_stop_request();

    struct sigaction action {};
    action.sa_handler = handle_stop_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(SIGINT, &action, &g_previous_int) != 0) {
        return failure(errno, "安装 SIGINT handler 失败");
    }
    if (sigaction(SIGTERM, &action, &g_previous_term) != 0) {
        const auto saved = errno;
        static_cast<void>(sigaction(SIGINT, &g_previous_int, nullptr));
        return failure(saved, "安装 SIGTERM handler 失败");
    }
    g_installed = true;
    return success();
}

HostSignalResult restore_host_signal_handlers() noexcept {
    if (!g_installed) {
        return success();
    }
    HostSignalResult first = success();
    if (sigaction(SIGINT, &g_previous_int, nullptr) != 0 && first.ok()) {
        first = failure(errno, "恢复 SIGINT handler 失败");
    }
    if (sigaction(SIGTERM, &g_previous_term, nullptr) != 0 && first.ok()) {
        first = failure(errno, "恢复 SIGTERM handler 失败");
    }
    g_installed = false;
    clear_host_stop_request();
    return first;
}

bool host_stop_requested() noexcept {
    return g_stop_requested != 0;
}

void clear_host_stop_request() noexcept {
    g_stop_requested = 0;
}

}  // namespace rvemu::runtime
