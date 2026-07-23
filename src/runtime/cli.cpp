// 文件职责：实现生产模拟器 CLI 的无副作用解析与稳定错误报告。
// 边界：本文件不访问文件系统、不打开设备、不修改终端状态，也不推断镜像格式。

#include "rvemu/runtime/cli.hpp"

#include <string>
#include <string_view>
#include <unordered_set>

namespace rvemu::runtime {
namespace {

[[nodiscard]] bool empty_value(const char* value) noexcept {
    return value == nullptr || std::string_view{value}.empty();
}

[[nodiscard]] CliParseResult failure(std::string message) {
    return CliParseResult{std::nullopt, std::move(message)};
}

}  // namespace

std::optional<ImageFormat> parse_image_format(const std::string& value) noexcept {
    if (value == "raw") {
        return ImageFormat::Raw;
    }
    return std::nullopt;
}

std::string cli_usage(const char* program_name) {
    const auto name =
        program_name == nullptr || std::string_view{program_name}.empty()
            ? std::string{"riscv_vector_emulator"}
            : std::string{program_name};
    return "用法：\n"
           "  " + name
           + " --bios artifacts/firmware/opensbi.bin --kernel artifacts/kernel/Image "
             "--disk artifacts/disk/rootfs.ext4 [--net none|tap0]\n"
             "可选：--bios-format raw --kernel-format raw\n";
}

CliParseResult parse_cli(int argc, char** argv) {
    CliOptions options;
    std::unordered_set<std::string> seen;
    for (int index = 1; index < argc; ++index) {
        const std::string argument{argv[index] == nullptr ? "" : argv[index]};
        if (argument == "--help" || argument == "-h") {
            options.help = true;
            return CliParseResult{options, {}};
        }
        if (argument.rfind("--", 0U) != 0U) {
            return failure("未知位置参数：" + argument);
        }
        if (!seen.insert(argument).second) {
            return failure("重复参数：" + argument);
        }
        if (index + 1 >= argc || empty_value(argv[index + 1])) {
            return failure("参数缺少非空值：" + argument);
        }
        const std::string value{argv[++index]};
        if (argument == "--bios") {
            options.bios_path = value;
        } else if (argument == "--kernel") {
            options.kernel_path = value;
        } else if (argument == "--disk") {
            options.disk_path = value;
        } else if (argument == "--net") {
            options.net = value;
        } else if (argument == "--bios-format") {
            const auto format = parse_image_format(value);
            if (!format.has_value()) {
                return failure("不支持的 BIOS 格式：" + value);
            }
            options.bios_format = *format;
        } else if (argument == "--kernel-format") {
            const auto format = parse_image_format(value);
            if (!format.has_value()) {
                return failure("不支持的 kernel 格式：" + value);
            }
            options.kernel_format = *format;
        } else {
            return failure("未知参数：" + argument);
        }
    }
    if (options.bios_path.empty()) {
        return failure("缺少必需参数：--bios");
    }
    if (options.kernel_path.empty()) {
        return failure("缺少必需参数：--kernel");
    }
    if (options.disk_path.empty()) {
        return failure("缺少必需参数：--disk");
    }
    if (options.net.empty()) {
        return failure("--net 不允许为空；省略该参数表示 none");
    }
    return CliParseResult{options, {}};
}

}  // namespace rvemu::runtime
