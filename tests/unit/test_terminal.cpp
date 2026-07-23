// 文件职责：使用 POSIX 伪终端验证生产终端后端的 Raw 模式、非阻塞输入输出和幂等恢复。
// 边界：测试不修改当前 shell 终端，不启动 CPU 主循环，也不把伪终端验收冒充真实 Linux Shell 验收。

#include "rvemu/platform/terminal.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <termios.h>
#include <unistd.h>

namespace {

/** 管理测试专用 master/slave pty，保证测试失败时也关闭 fd。 */
class PseudoTerminal final {
   public:
    PseudoTerminal() {
        master_ = ::posix_openpt(O_RDWR | O_NOCTTY);
        if (master_ < 0 || ::grantpt(master_) != 0 || ::unlockpt(master_) != 0) {
            throw std::runtime_error("创建伪终端 master 失败");
        }
        char* slave_name = ::ptsname(master_);
        if (slave_name == nullptr) {
            throw std::runtime_error("读取伪终端 slave 名称失败");
        }
        slave_ = ::open(slave_name, O_RDWR | O_NOCTTY);
        if (slave_ < 0) {
            throw std::runtime_error("打开伪终端 slave 失败");
        }
    }

    ~PseudoTerminal() {
        if (slave_ >= 0) {
            static_cast<void>(::close(slave_));
        }
        if (master_ >= 0) {
            static_cast<void>(::close(master_));
        }
    }

    PseudoTerminal(const PseudoTerminal&) = delete;
    PseudoTerminal& operator=(const PseudoTerminal&) = delete;
    PseudoTerminal(PseudoTerminal&&) = delete;
    PseudoTerminal& operator=(PseudoTerminal&&) = delete;

    [[nodiscard]] int master() const noexcept {
        return master_;
    }

    [[nodiscard]] int slave() const noexcept {
        return slave_;
    }

   private:
    int master_{-1};
    int slave_{-1};
};

/** 累积断言失败，使一次测试可以报告同一状态机中的多个问题。 */
void expect(bool condition, const char* message, int& failures) {
    if (!condition) {
        ++failures;
        std::cerr << "失败：" << message << '\n';
    }
}

[[nodiscard]] termios get_termios(int fd) {
    termios value{};
    if (::tcgetattr(fd, &value) != 0) {
        throw std::runtime_error("读取 termios 失败");
    }
    return value;
}

[[nodiscard]] int get_flags(int fd) {
    const auto flags = ::fcntl(fd, F_GETFL);
    if (flags < 0) {
        throw std::runtime_error("读取 fd flags 失败");
    }
    return flags;
}

[[nodiscard]] bool same_terminal_mode(const termios& lhs, const termios& rhs) noexcept {
    return lhs.c_iflag == rhs.c_iflag && lhs.c_oflag == rhs.c_oflag && lhs.c_cflag == rhs.c_cflag
           && lhs.c_lflag == rhs.c_lflag && lhs.c_cc[VMIN] == rhs.c_cc[VMIN]
           && lhs.c_cc[VTIME] == rhs.c_cc[VTIME];
}

/** 验证 Raw 模式禁用宿主行缓冲/回显/信号字符，并且 restore 恢复原 flags 与 termios。 */
void test_raw_mode_and_restore(int& failures) {
    PseudoTerminal pty;
    const auto original_mode = get_termios(pty.slave());
    const auto original_flags = get_flags(pty.slave());
    rvemu::platform::TerminalBackend terminal{pty.slave(), pty.slave()};

    expect(terminal.activate_raw().ok() && terminal.active(), "Raw 模式激活必须成功", failures);
    const auto raw_mode = get_termios(pty.slave());
    expect((raw_mode.c_lflag & (ECHO | ICANON | ISIG | IEXTEN)) == 0U,
           "Raw 模式必须关闭回显、规范行缓冲和宿主信号字符解释",
           failures);
    expect(raw_mode.c_cc[VMIN] == 0U && raw_mode.c_cc[VTIME] == 0U,
           "Raw 模式必须允许非阻塞 read 立即返回",
           failures);
    expect((get_flags(pty.slave()) & O_NONBLOCK) != 0, "input fd 必须设置 O_NONBLOCK", failures);

    expect(terminal.restore().ok() && terminal.restore().ok(),
           "restore 必须成功且幂等",
           failures);
    expect(same_terminal_mode(get_termios(pty.slave()), original_mode),
           "restore 必须恢复原 termios",
           failures);
    expect(get_flags(pty.slave()) == original_flags, "restore 必须恢复原 fd flags", failures);
}

/** 验证非 TTY 输入会被拒绝，且不会伪装成可用 Raw 后端。 */
void test_non_tty_rejected(int& failures) {
    int pipe_fds[2]{-1, -1};
    if (::pipe(pipe_fds) != 0) {
        throw std::runtime_error("创建 pipe 失败");
    }
    rvemu::platform::TerminalBackend terminal{pipe_fds[0], pipe_fds[1]};
    const auto result = terminal.activate_raw();
    expect(!result.ok() && result.code == rvemu::platform::TerminalErrorCode::NotTty,
           "非 TTY input fd 必须被明确拒绝",
           failures);
    expect(!terminal.active(), "拒绝非 TTY 后不得进入 active 状态", failures);
    static_cast<void>(::close(pipe_fds[0]));
    static_cast<void>(::close(pipe_fds[1]));
}

/** 验证 master 写入的字节可以被 Raw slave 非阻塞逐字节读取，空队列返回 WouldBlock。 */
void test_nonblocking_read(int& failures) {
    PseudoTerminal pty;
    rvemu::platform::TerminalBackend terminal{pty.slave(), pty.slave()};
    expect(terminal.activate_raw().ok(), "激活 Raw 模式用于读取测试", failures);
    std::uint8_t byte = 0U;
    expect(terminal.read_byte(byte).status == rvemu::platform::TerminalIoStatus::WouldBlock,
           "无输入时 read_byte 必须 WouldBlock",
           failures);
    const std::uint8_t input[] = {'a', '\003'};
    expect(::write(pty.master(), input, sizeof(input)) == static_cast<ssize_t>(sizeof(input)),
           "向 pty master 写入输入字节",
           failures);
    expect(terminal.read_byte(byte).status == rvemu::platform::TerminalIoStatus::Ready
               && byte == 'a',
           "read_byte 必须读取普通字节",
           failures);
    expect(terminal.read_byte(byte).status == rvemu::platform::TerminalIoStatus::Ready
               && byte == '\003',
           "Raw 模式必须把 Ctrl+C 作为字节交给来宾",
           failures);
}

/** 验证后端写入 slave 的 UART 输出可以从 master 读取，并正确报告实际短写长度。 */
void test_output_write(int& failures) {
    PseudoTerminal pty;
    rvemu::platform::TerminalBackend terminal{pty.slave(), pty.slave()};
    expect(terminal.activate_raw().ok(), "激活 Raw 模式用于输出测试", failures);
    const std::uint8_t output[] = {'O', 'K', '\n'};
    const auto written = terminal.write_bytes(output, sizeof(output));
    expect(written.status == rvemu::platform::TerminalIoStatus::Ready
               && written.byte_count == sizeof(output),
           "write_bytes 必须报告写入字节数",
           failures);

    char buffer[8]{};
    const auto count = ::read(pty.master(), buffer, sizeof(buffer));
    expect(count == 3 && std::string(buffer, buffer + count) == "OK\n",
           "pty master 必须收到 Raw 输出字节",
           failures);
    expect(terminal.write_bytes(nullptr, 1U).status == rvemu::platform::TerminalIoStatus::Error,
           "非零长度空指针写入必须返回错误",
           failures);
}

}  // namespace

int main() {
    int failures = 0;
    try {
        test_raw_mode_and_restore(failures);
        test_non_tty_rejected(failures);
        test_nonblocking_read(failures);
        test_output_write(failures);
    } catch (const std::exception& exception) {
        std::cerr << "终端测试基础设施失败：" << exception.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
