// 文件职责：声明生产模拟器 CLI 参数、退出码和无副作用解析入口。
// 边界：本模块不打开文件、不切换终端、不创建 TAP，也不装载来宾镜像。
// 主要依赖：标准字符串容器；调用方负责把解析结果交给启动校验模块。
// 关键不变量：参数错误必须在任何宿主资源副作用前被发现并报告。

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rvemu::runtime {

enum class ExitCode : int {
    Success = 0,
    CliUsage = 2,
    Resource = 3,
    ImageFormat = 4,
    RuntimeIo = 5,
    Internal = 70,
};

enum class ImageFormat : std::uint8_t {
    Raw,
};

struct CliOptions final {
    std::string bios_path{};
    std::string kernel_path{};
    std::string disk_path{};
    std::string net{"none"};
    ImageFormat bios_format{ImageFormat::Raw};
    ImageFormat kernel_format{ImageFormat::Raw};
    bool help{false};
};

struct CliParseResult final {
    std::optional<CliOptions> options{};
    std::string error{};

    [[nodiscard]] bool ok() const noexcept {
        return options.has_value();
    }
};

/** 解析 `riscv_vector_emulator` 参数；成功和失败都不产生文件、终端或网络副作用。 */
[[nodiscard]] CliParseResult parse_cli(int argc, char** argv);

/** 返回稳定用法文本；不包含开发机器绝对路径。 */
[[nodiscard]] std::string cli_usage(const char* program_name);

/** 将格式字符串转为受支持格式；当前只接受 raw，拒绝按文件名猜测格式。 */
[[nodiscard]] std::optional<ImageFormat> parse_image_format(const std::string& value) noexcept;

}  // namespace rvemu::runtime
