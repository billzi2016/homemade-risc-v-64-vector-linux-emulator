// 文件职责：声明面向正式架构测试的 ELF64 RISC-V 装载结果与严格校验入口。
// 边界：本模块只解析并装载可执行映像，不解释指令、不运行 CPU，也不生成期望结果。

#pragma once

#include "rvemu/bus/address_range.hpp"
#include "rvemu/bus/bus.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace rvemu::conformance {

struct LoadedElf final {
    std::uint64_t entry_point{0U};
    std::uint64_t tohost_address{0U};
    std::uint64_t lowest_loaded_address{0U};
    std::uint64_t highest_loaded_address_exclusive{0U};
};

struct ElfLoadResult final {
    std::optional<LoadedElf> image{};
    std::string error{};

    [[nodiscard]] bool ok() const noexcept { return image.has_value(); }

    [[nodiscard]] static ElfLoadResult success(LoadedElf loaded) {
        return ElfLoadResult{loaded, {}};
    }

    [[nodiscard]] static ElfLoadResult failure(std::string message) {
        return ElfLoadResult{std::nullopt, std::move(message)};
    }
};

// 读取真实宿主文件、校验 ELF64/小端/RISC-V 元数据，并经唯一物理总线装载全部 PT_LOAD 段。
// allowed_memory 是测试平台明确授权的 RAM 范围，任何越界段都会在产生部分写入前被拒绝。
[[nodiscard]] ElfLoadResult load_elf64_riscv(
    const std::filesystem::path& path,
    bus::Bus& bus,
    const bus::AddressRange& allowed_memory);

}  // namespace rvemu::conformance
