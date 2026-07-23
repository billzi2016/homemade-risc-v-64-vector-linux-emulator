// 文件职责：声明 BIOS、kernel、FDT 的安全装载和 OpenSBI 入口状态设置。
// 边界：本模块不运行 CPU、不打开终端或 TAP、不把磁盘整体载入 RAM。
// 主要依赖：统一物理总线、CPU 状态和 FDT 生成器。
// 关键不变量：所有镜像范围和重叠关系预验证成功后，才开始向来宾内存写入。

#pragma once

#include "rvemu/bus/address_range.hpp"
#include "rvemu/bus/bus.hpp"
#include "rvemu/core/cpu.hpp"
#include "rvemu/runtime/fdt.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace rvemu::runtime {

struct BootLayout final {
    std::uint64_t ram_base{0x8000'0000ULL};
    std::uint64_t ram_size{0x4000'0000ULL};
    std::uint64_t bios_load_address{0x8000'0000ULL};
    std::uint64_t kernel_load_address{0x8020'0000ULL};
    std::uint64_t fdt_address{0xBFE0'0000ULL};
    std::uint64_t fdt_reserved_size{0x0002'0000ULL};
};

struct BootImagePaths final {
    std::filesystem::path bios{};
    std::filesystem::path kernel{};
};

struct BootResult final {
    bool loaded{false};
    std::uint64_t bios_size{0U};
    std::uint64_t kernel_size{0U};
    std::uint64_t fdt_size{0U};
    std::string error{};

    [[nodiscard]] bool ok() const noexcept {
        return loaded;
    }
};

/** 预验证并装载 raw BIOS、raw kernel 和 DTB，然后设置 PC/a0/a1；失败不改变 CPU 入口状态。 */
[[nodiscard]] BootResult load_boot_images(bus::Bus& bus,
                                          core::Cpu& cpu,
                                          const BootImagePaths& paths,
                                          const BootLayout& layout = {},
                                          const FdtConfig& fdt_config = {});

}  // namespace rvemu::runtime
