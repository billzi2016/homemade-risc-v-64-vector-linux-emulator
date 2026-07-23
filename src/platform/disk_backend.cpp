// 文件职责：实现宿主磁盘镜像的安全打开、容量冻结和完整 pread/pwrite 访问。
// 边界：本文件不创建镜像、不解释 ext4，也不把宿主 I/O 结果伪装成 VirtIO 完成。
// 主要依赖：POSIX 文件 API；所有错误通过 DiskResult 返回，深层代码不 exit。
// 关键不变量：任何成功 I/O 都完整覆盖请求长度；失败不会改变已冻结 capacity。

#include "rvemu/platform/disk_backend.hpp"

#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace rvemu::platform {
namespace {

[[nodiscard]] DiskResult success() noexcept {
    return {};
}

[[nodiscard]] DiskResult failure(DiskErrorCode code, int errno_value, std::string detail) {
    return DiskResult{code, errno_value, std::move(detail)};
}

}  // namespace

DiskBackend::~DiskBackend() {
    close();
}

DiskBackend::DiskBackend(DiskBackend&& other) noexcept
    : fd_(other.fd_), writable_(other.writable_), capacity_sectors_(other.capacity_sectors_) {
    other.fd_ = -1;
    other.writable_ = false;
    other.capacity_sectors_ = 0U;
}

DiskBackend& DiskBackend::operator=(DiskBackend&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        writable_ = other.writable_;
        capacity_sectors_ = other.capacity_sectors_;
        other.fd_ = -1;
        other.writable_ = false;
        other.capacity_sectors_ = 0U;
    }
    return *this;
}

DiskResult DiskBackend::open_existing(const std::string& path, DiskOpenMode mode) noexcept {
    close();
    const auto flags = mode == DiskOpenMode::ReadWrite ? O_RDWR : O_RDONLY;
    fd_ = ::open(path.c_str(), flags | O_CLOEXEC);
    if (fd_ < 0) {
        return failure(DiskErrorCode::OpenFailure, errno, "打开磁盘镜像失败");
    }
    struct stat st {};
    if (::fstat(fd_, &st) != 0) {
        const auto saved = errno;
        close();
        return failure(DiskErrorCode::StatFailure, saved, "读取磁盘镜像 stat 失败");
    }
    if (!S_ISREG(st.st_mode)) {
        close();
        return failure(DiskErrorCode::InvalidFileType, 0, "磁盘镜像必须是普通文件");
    }
    if (st.st_size <= 0 || (static_cast<std::uint64_t>(st.st_size) % kSectorSize) != 0U) {
        close();
        return failure(DiskErrorCode::InvalidSize, 0, "磁盘镜像大小必须是 512 字节正整数倍");
    }
    capacity_sectors_ = static_cast<std::uint64_t>(st.st_size) / kSectorSize;
    writable_ = mode == DiskOpenMode::ReadWrite;
    return success();
}

void DiskBackend::close() noexcept {
    if (fd_ >= 0) {
        static_cast<void>(::close(fd_));
    }
    fd_ = -1;
    writable_ = false;
    capacity_sectors_ = 0U;
}

DiskResult DiskBackend::check_range(std::uint64_t sector, std::size_t byte_count) const noexcept {
    if (fd_ < 0) {
        return failure(DiskErrorCode::IoFailure, EBADF, "磁盘镜像尚未打开");
    }
    if (byte_count == 0U) {
        return success();
    }
    const auto sector_offset = sector * kSectorSize;
    if (sector != 0U && sector_offset / kSectorSize != sector) {
        return failure(DiskErrorCode::OutOfRange, 0, "sector * 512 发生溢出");
    }
    const auto capacity_bytes = capacity_sectors_ * kSectorSize;
    if (sector_offset > capacity_bytes || byte_count > capacity_bytes - sector_offset) {
        return failure(DiskErrorCode::OutOfRange, 0, "磁盘 I/O 超出冻结 capacity");
    }
    if (sector_offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())
        || byte_count
               > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) - sector_offset) {
        return failure(DiskErrorCode::OutOfRange, 0, "宿主 off_t 无法表示该磁盘偏移");
    }
    return success();
}

DiskResult DiskBackend::read(std::uint64_t sector, std::vector<std::uint8_t>& buffer) noexcept {
    const auto range = check_range(sector, buffer.size());
    if (!range.ok()) {
        return range;
    }
    std::size_t done = 0U;
    const auto offset = sector * kSectorSize;
    while (done < buffer.size()) {
        const auto position = static_cast<off_t>(offset + done);
        const auto count = ::pread(fd_, buffer.data() + done, buffer.size() - done, position);
        if (count > 0) {
            done += static_cast<std::size_t>(count);
            continue;
        }
        return failure(DiskErrorCode::IoFailure, count < 0 ? errno : EIO, "磁盘镜像短读或读取失败");
    }
    return success();
}

DiskResult DiskBackend::write(std::uint64_t sector, const std::vector<std::uint8_t>& buffer) noexcept {
    if (!writable_) {
        return failure(DiskErrorCode::ReadOnly, 0, "只读磁盘镜像拒绝写入");
    }
    const auto range = check_range(sector, buffer.size());
    if (!range.ok()) {
        return range;
    }
    std::size_t done = 0U;
    const auto offset = sector * kSectorSize;
    while (done < buffer.size()) {
        const auto position = static_cast<off_t>(offset + done);
        const auto count = ::pwrite(fd_, buffer.data() + done, buffer.size() - done, position);
        if (count > 0) {
            done += static_cast<std::size_t>(count);
            continue;
        }
        return failure(DiskErrorCode::IoFailure, count < 0 ? errno : EIO, "磁盘镜像短写或写入失败");
    }
    return success();
}

}  // namespace rvemu::platform
