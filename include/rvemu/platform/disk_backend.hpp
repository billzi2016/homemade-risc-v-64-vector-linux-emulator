// 文件职责：声明 VirtIO-Blk 使用的宿主磁盘镜像后端，提供固定容量、安全偏移和 pread/pwrite 访问。
// 边界：本模块不解析 VirtIO 描述符、不提交 used ring，也不自动创建或下载任何镜像文件。
// 主要依赖：POSIX open/fstat/pread/pwrite；路径由调用方显式传入。
// 关键不变量：容量必须是 512 字节整数倍；所有 I/O 都检查 sector 和长度溢出及 capacity 边界。

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rvemu::platform {

enum class DiskErrorCode : std::uint8_t {
    None,
    OpenFailure,
    StatFailure,
    InvalidFileType,
    InvalidSize,
    ReadOnly,
    OutOfRange,
    IoFailure,
};

struct DiskResult final {
    DiskErrorCode code{DiskErrorCode::None};
    int errno_value{0};
    std::string detail{};

    [[nodiscard]] bool ok() const noexcept {
        return code == DiskErrorCode::None;
    }
};

/** 打开模式决定是否允许 OUT 请求修改镜像；只读模式不得静默接受写入。 */
enum class DiskOpenMode : std::uint8_t {
    ReadOnly,
    ReadWrite,
};

class DiskBackend final {
   public:
    static constexpr std::uint64_t kSectorSize = 512U;

    DiskBackend() = default;
    ~DiskBackend();

    DiskBackend(const DiskBackend&) = delete;
    DiskBackend& operator=(const DiskBackend&) = delete;
    DiskBackend(DiskBackend&& other) noexcept;
    DiskBackend& operator=(DiskBackend&& other) noexcept;

    /** 打开一个已存在普通文件镜像，校验大小为 512 字节整数倍并冻结 capacity。 */
    [[nodiscard]] DiskResult open_existing(const std::string& path, DiskOpenMode mode) noexcept;

    /** 关闭 fd；重复调用安全。 */
    void close() noexcept;

    /** 从指定扇区读取完整 buffer；短读或越界均返回错误，不用未初始化数据补齐。 */
    [[nodiscard]] DiskResult read(std::uint64_t sector, std::vector<std::uint8_t>& buffer) noexcept;

    /** 向指定扇区写入完整 buffer；只读或短写均返回错误。 */
    [[nodiscard]] DiskResult write(std::uint64_t sector,
                                   const std::vector<std::uint8_t>& buffer) noexcept;

    [[nodiscard]] bool writable() const noexcept {
        return writable_;
    }

    [[nodiscard]] std::uint64_t capacity_sectors() const noexcept {
        return capacity_sectors_;
    }

   private:
    [[nodiscard]] DiskResult check_range(std::uint64_t sector, std::size_t byte_count) const noexcept;

    int fd_{-1};
    bool writable_{false};
    std::uint64_t capacity_sectors_{0U};
};

}  // namespace rvemu::platform
