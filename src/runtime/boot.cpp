// 文件职责：实现 raw OpenSBI/Linux 镜像和 FDT 的安全装载，以及启动寄存器初始化。
// 边界：本文件不解析 ext4、不创建磁盘镜像、不运行事件循环，也不伪造任何来宾输出。

#include "rvemu/runtime/boot.hpp"

#include "rvemu/bus/access.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace rvemu::runtime {
namespace {

struct ImageBytes final {
    std::vector<std::uint8_t> bytes{};
    std::string error{};
};

struct Range final {
    std::uint64_t begin{0U};
    std::uint64_t end{0U};
};

[[nodiscard]] ImageBytes read_binary(const std::filesystem::path& path, const char* label) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return ImageBytes{{}, std::string{"无法打开 "} + label + " 文件：" + path.string()};
    }
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{stream},
                                    std::istreambuf_iterator<char>{}};
    if (!stream.eof() && stream.fail()) {
        return ImageBytes{{}, std::string{"读取 "} + label + " 文件失败：" + path.string()};
    }
    if (bytes.empty()) {
        return ImageBytes{{}, std::string{label} + " 文件为空"};
    }
    return ImageBytes{std::move(bytes), {}};
}

[[nodiscard]] bool make_range(std::uint64_t begin, std::uint64_t size, Range& range) noexcept {
    if (size == 0U || begin > std::numeric_limits<std::uint64_t>::max() - size) {
        return false;
    }
    range = Range{begin, begin + size};
    return true;
}

[[nodiscard]] bool contains(const Range& outer, const Range& inner) noexcept {
    return inner.begin >= outer.begin && inner.end <= outer.end;
}

[[nodiscard]] bool overlaps(const Range& lhs, const Range& rhs) noexcept {
    return lhs.begin < rhs.end && rhs.begin < lhs.end;
}

[[nodiscard]] std::optional<std::string> write_bytes(bus::Bus& bus,
                                                     std::uint64_t address,
                                                     const std::vector<std::uint8_t>& bytes,
                                                     const char* label) {
    for (std::uint64_t offset = 0U; offset < bytes.size(); ++offset) {
        const auto written = bus.write(bus::PhysicalAddress{address + offset},
                                       bus::AccessWidth::Byte,
                                       bytes[static_cast<std::size_t>(offset)],
                                       bus::AccessType::Initialization);
        if (!written.ok()) {
            return std::string{label} + " 写入来宾内存失败：" + written.error.detail;
        }
    }
    return std::nullopt;
}

}  // namespace

BootResult load_boot_images(bus::Bus& bus,
                            core::Cpu& cpu,
                            const BootImagePaths& paths,
                            const BootLayout& layout,
                            const FdtConfig& fdt_config) {
    Range ram{};
    if (!make_range(layout.ram_base, layout.ram_size, ram)) {
        return BootResult{false, 0U, 0U, 0U, "RAM 范围无效"};
    }
    const auto bios = read_binary(paths.bios, "BIOS");
    if (!bios.error.empty()) {
        return BootResult{false, 0U, 0U, 0U, bios.error};
    }
    const auto kernel = read_binary(paths.kernel, "kernel");
    if (!kernel.error.empty()) {
        return BootResult{false, 0U, 0U, 0U, kernel.error};
    }

    auto fdt_config_effective = fdt_config;
    fdt_config_effective.ram_base = layout.ram_base;
    fdt_config_effective.ram_size = layout.ram_size;
    fdt_config_effective.fdt_address = layout.fdt_address;
    fdt_config_effective.fdt_reserved_size = layout.fdt_reserved_size;
    const auto fdt = build_machine_fdt(fdt_config_effective);
    if (!fdt.ok()) {
        return BootResult{false, 0U, 0U, 0U, fdt.error};
    }
    if (fdt.blob.size() > layout.fdt_reserved_size) {
        return BootResult{false, 0U, 0U, 0U, "生成的 FDT 超过保留区"};
    }

    Range bios_range{};
    Range kernel_range{};
    Range fdt_range{};
    if (!make_range(layout.bios_load_address, bios.bytes.size(), bios_range)
        || !make_range(layout.kernel_load_address, kernel.bytes.size(), kernel_range)
        || !make_range(layout.fdt_address, fdt.blob.size(), fdt_range)) {
        return BootResult{false, 0U, 0U, 0U, "镜像地址加长度发生溢出"};
    }
    if (!contains(ram, bios_range) || !contains(ram, kernel_range) || !contains(ram, fdt_range)) {
        return BootResult{false, 0U, 0U, 0U, "BIOS、kernel 或 FDT 不完全位于 RAM 内"};
    }
    if (overlaps(bios_range, kernel_range) || overlaps(bios_range, fdt_range)
        || overlaps(kernel_range, fdt_range)) {
        return BootResult{false, 0U, 0U, 0U, "BIOS、kernel 和 FDT 装载范围发生重叠"};
    }

    if (const auto error = write_bytes(bus, layout.bios_load_address, bios.bytes, "BIOS");
        error.has_value()) {
        return BootResult{false, 0U, 0U, 0U, *error};
    }
    if (const auto error = write_bytes(bus, layout.kernel_load_address, kernel.bytes, "kernel");
        error.has_value()) {
        return BootResult{false, 0U, 0U, 0U, *error};
    }
    if (const auto error = write_bytes(bus, layout.fdt_address, fdt.blob, "FDT"); error.has_value()) {
        return BootResult{false, 0U, 0U, 0U, *error};
    }

    cpu.state().reset(layout.bios_load_address);
    cpu.state().set_integer(10U, 0U);
    cpu.state().set_integer(11U, layout.fdt_address);
    return BootResult{true,
                      static_cast<std::uint64_t>(bios.bytes.size()),
                      static_cast<std::uint64_t>(kernel.bytes.size()),
                      static_cast<std::uint64_t>(fdt.blob.size()),
                      {}};
}

}  // namespace rvemu::runtime
