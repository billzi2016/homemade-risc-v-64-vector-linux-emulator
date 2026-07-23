// 文件职责：声明宿主 POSIX 终端 Raw 模式、非阻塞 I/O 和幂等恢复的受控后端。
// 边界：本模块只适配宿主终端文件描述符，不解析 CLI、不调度 CPU，也不实现 UART 寄存器语义。
// 主要依赖：POSIX termios、fcntl 与 read/write 系统调用。
// 关键不变量：成功切换 Raw 模式前必须保存原状态；析构和显式 restore 必须可重复且尽力恢复。

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rvemu::platform {

enum class TerminalErrorCode : std::uint8_t {
    None,
    NotTty,
    TermiosFailure,
    FcntlFailure,
    IoFailure,
};

/** 描述终端控制操作的结构化结果；失败时 errno_value 保存宿主 errno 快照。 */
struct TerminalResult final {
    TerminalErrorCode code{TerminalErrorCode::None};
    int errno_value{0};
    std::string detail{};

    [[nodiscard]] bool ok() const noexcept {
        return code == TerminalErrorCode::None;
    }

    [[nodiscard]] static TerminalResult success() noexcept {
        return {};
    }

    [[nodiscard]] static TerminalResult failure(TerminalErrorCode failure_code,
                                                int failure_errno,
                                                std::string failure_detail) {
        return TerminalResult{failure_code, failure_errno, std::move(failure_detail)};
    }
};

enum class TerminalIoStatus : std::uint8_t {
    Ready,
    WouldBlock,
    Closed,
    Error,
};

/** 描述一次非阻塞输入或输出尝试的结果；Ready 时 byte_count 表示已处理字节数。 */
struct TerminalIoResult final {
    TerminalIoStatus status{TerminalIoStatus::Ready};
    std::size_t byte_count{0U};
    int errno_value{0};

    [[nodiscard]] static TerminalIoResult ready(std::size_t count) noexcept {
        return TerminalIoResult{TerminalIoStatus::Ready, count, 0};
    }

    [[nodiscard]] static TerminalIoResult would_block() noexcept {
        return TerminalIoResult{TerminalIoStatus::WouldBlock, 0U, 0};
    }

    [[nodiscard]] static TerminalIoResult closed() noexcept {
        return TerminalIoResult{TerminalIoStatus::Closed, 0U, 0};
    }

    [[nodiscard]] static TerminalIoResult error(int failure_errno) noexcept {
        return TerminalIoResult{TerminalIoStatus::Error, 0U, failure_errno};
    }
};

/**
 * 终端 Raw 模式守卫，服务 UART 后端和运行循环。
 *
 * 参数中的文件描述符由调用方持有，本类不关闭 fd；它只保存和恢复输入 TTY 的 termios
 * 与输入 fd flags，并提供非阻塞读写包装。所有方法预期在单线程事件循环中调用。
 */
class TerminalBackend final {
   public:
    TerminalBackend(int input_fd, int output_fd) noexcept;
    ~TerminalBackend();

    TerminalBackend(const TerminalBackend&) = delete;
    TerminalBackend& operator=(const TerminalBackend&) = delete;
    TerminalBackend(TerminalBackend&&) = delete;
    TerminalBackend& operator=(TerminalBackend&&) = delete;

    /**
     * 校验 input fd 是 TTY，保存原 termios/flags，并切换到 Raw + O_NONBLOCK。
     *
     * 失败时不留下部分 Raw 状态；重复调用在已经激活时直接成功。
     */
    [[nodiscard]] TerminalResult activate_raw();

    /** 尽力恢复 activate_raw 保存的 termios 与 fd flags；重复调用安全。 */
    [[nodiscard]] TerminalResult restore() noexcept;

    /** 从非阻塞 input fd 读取一个字节；无输入时返回 WouldBlock，不阻塞 CPU 主循环。 */
    [[nodiscard]] TerminalIoResult read_byte(std::uint8_t& byte) noexcept;

    /** 向 output fd 尝试写入一段字节；短写和 WouldBlock 由调用方在事件循环中续写。 */
    [[nodiscard]] TerminalIoResult write_bytes(const std::uint8_t* data,
                                               std::size_t length) noexcept;

    [[nodiscard]] bool active() const noexcept {
        return active_;
    }

   private:
    int input_fd_{-1};
    int output_fd_{-1};
    bool termios_saved_{false};
    bool flags_saved_{false};
    bool active_{false};
    int original_flags_{0};
    // 使用不透明字节存储 termios，避免在头文件暴露系统头给所有调用方。
    alignas(8) unsigned char original_termios_[128]{};
};

}  // namespace rvemu::platform
