// 文件职责：实现 POSIX 终端 Raw 模式切换、非阻塞输入输出和异常路径幂等恢复。
// 边界：本文件不打开真实 stdin/stdout、不处理 CLI 退出策略，也不直接连接 UART FIFO。
// 主要依赖：termios、fcntl、read/write；错误通过 TerminalResult/TerminalIoResult 返回。
// 关键不变量：任一设置步骤失败都会回滚已保存状态，析构不会抛出或终止进程。

#include "rvemu/platform/terminal.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace rvemu::platform {
namespace {

static_assert(sizeof(termios) <= 128U, "termios 保存缓冲区过小");
static_assert(alignof(termios) <= 8U, "termios 保存缓冲区对齐不足");

[[nodiscard]] termios& as_termios(unsigned char* storage) noexcept {
    return *reinterpret_cast<termios*>(storage);
}

/** 按 POSIX Raw 语义关闭宿主行缓冲、回显、信号字符解释和输出转换。 */
[[nodiscard]] termios make_raw(termios value) noexcept {
    value.c_iflag &= static_cast<tcflag_t>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
    value.c_oflag &= static_cast<tcflag_t>(~OPOST);
    value.c_cflag |= CS8;
    value.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON | IEXTEN | ISIG));
    value.c_cc[VMIN] = 0U;
    value.c_cc[VTIME] = 0U;
    return value;
}

[[nodiscard]] bool is_transient_io_error(int value) noexcept {
    return value == EINTR || value == EAGAIN || value == EWOULDBLOCK;
}

}  // namespace

TerminalBackend::TerminalBackend(int input_fd, int output_fd) noexcept
    : input_fd_(input_fd), output_fd_(output_fd) {
}

TerminalBackend::~TerminalBackend() {
    static_cast<void>(restore());
}

TerminalResult TerminalBackend::activate_raw() {
    if (active_) {
        return TerminalResult::success();
    }
    if (::isatty(input_fd_) != 1) {
        return TerminalResult::failure(
            TerminalErrorCode::NotTty, errno, "终端 Raw 模式要求 input fd 是 TTY");
    }

    termios current{};
    if (::tcgetattr(input_fd_, &current) != 0) {
        return TerminalResult::failure(
            TerminalErrorCode::TermiosFailure, errno, "读取原始 termios 失败");
    }
    as_termios(original_termios_) = current;
    termios_saved_ = true;

    const auto flags = ::fcntl(input_fd_, F_GETFL);
    if (flags < 0) {
        const auto failure_errno = errno;
        static_cast<void>(restore());
        return TerminalResult::failure(
            TerminalErrorCode::FcntlFailure, failure_errno, "读取 input fd flags 失败");
    }
    original_flags_ = flags;
    flags_saved_ = true;

    auto raw = make_raw(current);
    if (::tcsetattr(input_fd_, TCSAFLUSH, &raw) != 0) {
        const auto failure_errno = errno;
        static_cast<void>(restore());
        return TerminalResult::failure(
            TerminalErrorCode::TermiosFailure, failure_errno, "切换终端 Raw 模式失败");
    }
    if (::fcntl(input_fd_, F_SETFL, flags | O_NONBLOCK) != 0) {
        const auto failure_errno = errno;
        static_cast<void>(restore());
        return TerminalResult::failure(
            TerminalErrorCode::FcntlFailure, failure_errno, "设置 input fd 非阻塞失败");
    }

    active_ = true;
    return TerminalResult::success();
}

TerminalResult TerminalBackend::restore() noexcept {
    TerminalResult first_failure = TerminalResult::success();
    if (termios_saved_ && ::tcsetattr(input_fd_, TCSAFLUSH, &as_termios(original_termios_)) != 0
        && first_failure.ok()) {
        first_failure = TerminalResult::failure(
            TerminalErrorCode::TermiosFailure, errno, "恢复原始 termios 失败");
    }
    if (flags_saved_ && ::fcntl(input_fd_, F_SETFL, original_flags_) != 0 && first_failure.ok()) {
        first_failure = TerminalResult::failure(
            TerminalErrorCode::FcntlFailure, errno, "恢复 input fd flags 失败");
    }
    active_ = false;
    termios_saved_ = false;
    flags_saved_ = false;
    return first_failure;
}

TerminalIoResult TerminalBackend::read_byte(std::uint8_t& byte) noexcept {
    unsigned char buffer = 0U;
    for (;;) {
        const auto count = ::read(input_fd_, &buffer, 1U);
        if (count == 1) {
            byte = buffer;
            return TerminalIoResult::ready(1U);
        }
        if (count == 0) {
            // Raw TTY 在 VMIN=0/VTIME=0 时以 0 字节表示“当前无输入”，不是 EOF。
            return TerminalIoResult::would_block();
        }
        const auto failure_errno = errno;
        if (failure_errno == EINTR) {
            continue;
        }
        if (failure_errno == EAGAIN || failure_errno == EWOULDBLOCK) {
            return TerminalIoResult::would_block();
        }
        return TerminalIoResult::error(failure_errno);
    }
}

TerminalIoResult TerminalBackend::write_bytes(const std::uint8_t* data,
                                              std::size_t length) noexcept {
    if (length == 0U) {
        return TerminalIoResult::ready(0U);
    }
    if (data == nullptr) {
        return TerminalIoResult::error(EINVAL);
    }
    for (;;) {
        const auto count = ::write(output_fd_, data, length);
        if (count > 0) {
            return TerminalIoResult::ready(static_cast<std::size_t>(count));
        }
        if (count == 0) {
            return TerminalIoResult::would_block();
        }
        const auto failure_errno = errno;
        if (failure_errno == EINTR) {
            continue;
        }
        if (is_transient_io_error(failure_errno)) {
            return TerminalIoResult::would_block();
        }
        return TerminalIoResult::error(failure_errno);
    }
}

}  // namespace rvemu::platform
