// 文件职责：严格解析 ELF64 RISC-V 文件，预验证所有可装载段并解析 ACT4 的 tohost 符号。
// 边界：文件内容只通过 Bus 初始化事务进入来宾 RAM；本文件不绕过总线访问内部存储。

#include "rvemu/conformance/elf_loader.hpp"

#include "rvemu/bus/access.hpp"
#include "rvemu/bus/physical_address.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace rvemu::conformance {
namespace {

constexpr std::size_t kElfHeaderSize = 64U;
constexpr std::size_t kProgramHeaderSize = 56U;
constexpr std::size_t kSectionHeaderSize = 64U;
constexpr std::size_t kSymbolSize = 24U;
constexpr std::uint16_t kRiscvMachine = 243U;
constexpr std::uint32_t kLoadSegment = 1U;
constexpr std::uint32_t kSymbolTable = 2U;
constexpr std::uint32_t kDynamicSymbolTable = 11U;

struct Segment final {
    std::uint64_t file_offset{0U};
    std::uint64_t physical_address{0U};
    std::uint64_t file_size{0U};
    std::uint64_t memory_size{0U};
};

[[nodiscard]] bool range_fits(
    std::uint64_t offset,
    std::uint64_t length,
    std::uint64_t total) noexcept {
    return offset <= total && length <= total - offset;
}

template <typename Integer>
[[nodiscard]] std::optional<Integer> read_little_endian(
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t offset) noexcept {
    const auto width = static_cast<std::uint64_t>(sizeof(Integer));
    if (!range_fits(offset, width, static_cast<std::uint64_t>(bytes.size()))) {
        return std::nullopt;
    }

    Integer value = 0U;
    for (std::uint64_t index = 0U; index < width; ++index) {
        value |= static_cast<Integer>(bytes[static_cast<std::size_t>(offset + index)])
                 << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> read_file(
    const std::filesystem::path& path,
    std::string& error) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        error = "无法打开 ELF 文件：" + path.string();
        return std::nullopt;
    }

    std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};
    if (!stream.eof() && stream.fail()) {
        error = "读取 ELF 文件失败：" + path.string();
        return std::nullopt;
    }
    return bytes;
}

[[nodiscard]] std::optional<std::uint64_t> find_tohost_symbol(
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t section_offset,
    std::uint16_t section_entry_size,
    std::uint16_t section_count) {
    const auto file_size = static_cast<std::uint64_t>(bytes.size());
    for (std::uint16_t section_index = 0U; section_index < section_count; ++section_index) {
        const auto header = section_offset +
                            static_cast<std::uint64_t>(section_index) * section_entry_size;
        const auto type = read_little_endian<std::uint32_t>(bytes, header + 4U);
        if (!type.has_value() || (*type != kSymbolTable && *type != kDynamicSymbolTable)) {
            continue;
        }

        const auto symbols_offset = read_little_endian<std::uint64_t>(bytes, header + 24U);
        const auto symbols_size = read_little_endian<std::uint64_t>(bytes, header + 32U);
        const auto strings_index = read_little_endian<std::uint32_t>(bytes, header + 40U);
        const auto symbol_entry_size = read_little_endian<std::uint64_t>(bytes, header + 56U);
        if (!symbols_offset.has_value() || !symbols_size.has_value() ||
            !strings_index.has_value() || !symbol_entry_size.has_value() ||
            *symbol_entry_size < kSymbolSize || *strings_index >= section_count ||
            !range_fits(*symbols_offset, *symbols_size, file_size)) {
            continue;
        }

        const auto strings_header = section_offset +
                                    static_cast<std::uint64_t>(*strings_index) * section_entry_size;
        const auto strings_offset = read_little_endian<std::uint64_t>(bytes, strings_header + 24U);
        const auto strings_size = read_little_endian<std::uint64_t>(bytes, strings_header + 32U);
        if (!strings_offset.has_value() || !strings_size.has_value() ||
            !range_fits(*strings_offset, *strings_size, file_size)) {
            continue;
        }

        const auto symbol_count = *symbols_size / *symbol_entry_size;
        for (std::uint64_t symbol_index = 0U; symbol_index < symbol_count; ++symbol_index) {
            const auto symbol = *symbols_offset + symbol_index * *symbol_entry_size;
            const auto name_offset = read_little_endian<std::uint32_t>(bytes, symbol);
            const auto value = read_little_endian<std::uint64_t>(bytes, symbol + 8U);
            if (!name_offset.has_value() || !value.has_value() || *name_offset >= *strings_size) {
                continue;
            }

            const auto name_begin = *strings_offset + *name_offset;
            const auto name_limit = *strings_offset + *strings_size;
            std::string name;
            for (auto position = name_begin; position < name_limit; ++position) {
                const auto character = bytes[static_cast<std::size_t>(position)];
                if (character == 0U) {
                    break;
                }
                name.push_back(static_cast<char>(character));
            }
            if (name == "tohost") {
                return value;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] bus::AccessWidth best_width(std::uint64_t remaining) noexcept {
    if (remaining >= 8U) {
        return bus::AccessWidth::DoubleWord;
    }
    if (remaining >= 4U) {
        return bus::AccessWidth::Word;
    }
    if (remaining >= 2U) {
        return bus::AccessWidth::HalfWord;
    }
    return bus::AccessWidth::Byte;
}

[[nodiscard]] std::optional<std::string> write_segment(
    const std::vector<std::uint8_t>& bytes,
    const Segment& segment,
    bus::Bus& bus) {
    std::uint64_t completed = 0U;
    while (completed < segment.memory_size) {
        const auto width = best_width(segment.memory_size - completed);
        const auto width_bytes = bus::width_in_bytes(width);
        std::uint64_t value = 0U;

        // 文件数据结束后的 p_memsz 尾部必须显式清零，不能依赖 RAM 构造时碰巧为零。
        for (std::uint64_t index = 0U; index < width_bytes; ++index) {
            const auto segment_offset = completed + index;
            if (segment_offset < segment.file_size) {
                const auto source = segment.file_offset + segment_offset;
                value |= static_cast<std::uint64_t>(bytes[static_cast<std::size_t>(source)])
                         << (index * 8U);
            }
        }

        const auto address = segment.physical_address + completed;
        const auto written = bus.write(
            bus::PhysicalAddress{address}, width, value, bus::AccessType::Initialization);
        if (!written.ok()) {
            return "ELF 段写入物理总线失败，地址=" + std::to_string(address) +
                   "，原因=" + written.error.detail;
        }
        completed += width_bytes;
    }
    return std::nullopt;
}

}  // namespace

ElfLoadResult load_elf64_riscv(
    const std::filesystem::path& path,
    bus::Bus& bus,
    const bus::AddressRange& allowed_memory) {
    std::string file_error;
    const auto file = read_file(path, file_error);
    if (!file.has_value()) {
        return ElfLoadResult::failure(std::move(file_error));
    }
    const auto& bytes = *file;
    if (bytes.size() < kElfHeaderSize || bytes[0] != 0x7FU || bytes[1] != 'E' ||
        bytes[2] != 'L' || bytes[3] != 'F') {
        return ElfLoadResult::failure("输入不是完整的 ELF 文件");
    }
    if (bytes[4] != 2U || bytes[5] != 1U || bytes[6] != 1U) {
        return ElfLoadResult::failure("只支持 ELFCLASS64、小端且版本为 1 的映像");
    }

    const auto machine = read_little_endian<std::uint16_t>(bytes, 18U);
    const auto elf_version = read_little_endian<std::uint32_t>(bytes, 20U);
    const auto entry = read_little_endian<std::uint64_t>(bytes, 24U);
    const auto program_offset = read_little_endian<std::uint64_t>(bytes, 32U);
    const auto section_offset = read_little_endian<std::uint64_t>(bytes, 40U);
    const auto elf_header_size = read_little_endian<std::uint16_t>(bytes, 52U);
    const auto program_entry_size = read_little_endian<std::uint16_t>(bytes, 54U);
    const auto program_count = read_little_endian<std::uint16_t>(bytes, 56U);
    const auto section_entry_size = read_little_endian<std::uint16_t>(bytes, 58U);
    const auto section_count = read_little_endian<std::uint16_t>(bytes, 60U);
    if (!machine.has_value() || !elf_version.has_value() || !entry.has_value() ||
        !program_offset.has_value() || !section_offset.has_value() ||
        !elf_header_size.has_value() || !program_entry_size.has_value() ||
        !program_count.has_value() || !section_entry_size.has_value() ||
        !section_count.has_value()) {
        return ElfLoadResult::failure("ELF 头字段不完整");
    }
    if (*machine != kRiscvMachine || *elf_version != 1U ||
        *elf_header_size < kElfHeaderSize || *program_entry_size < kProgramHeaderSize ||
        *section_entry_size < kSectionHeaderSize || *program_count == 0U ||
        *section_count == 0U) {
        return ElfLoadResult::failure("ELF 架构、版本或头表尺寸不符合 RV64 测试要求");
    }

    const auto file_size = static_cast<std::uint64_t>(bytes.size());
    if (*program_count > (file_size / *program_entry_size) ||
        !range_fits(*program_offset, static_cast<std::uint64_t>(*program_count) *
                                         *program_entry_size, file_size) ||
        *section_count > (file_size / *section_entry_size) ||
        !range_fits(*section_offset, static_cast<std::uint64_t>(*section_count) *
                                         *section_entry_size, file_size)) {
        return ElfLoadResult::failure("ELF 程序头表或节头表越过文件边界");
    }

    std::vector<Segment> segments;
    std::uint64_t lowest = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t highest = 0U;
    for (std::uint16_t index = 0U; index < *program_count; ++index) {
        const auto header = *program_offset + static_cast<std::uint64_t>(index) *
                                                  *program_entry_size;
        const auto type = read_little_endian<std::uint32_t>(bytes, header);
        if (!type.has_value() || *type != kLoadSegment) {
            continue;
        }
        const auto file_offset = read_little_endian<std::uint64_t>(bytes, header + 8U);
        const auto virtual_address = read_little_endian<std::uint64_t>(bytes, header + 16U);
        const auto physical_address = read_little_endian<std::uint64_t>(bytes, header + 24U);
        const auto file_bytes = read_little_endian<std::uint64_t>(bytes, header + 32U);
        const auto memory_bytes = read_little_endian<std::uint64_t>(bytes, header + 40U);
        if (!file_offset.has_value() || !virtual_address.has_value() ||
            !physical_address.has_value() || !file_bytes.has_value() ||
            !memory_bytes.has_value() || *file_bytes > *memory_bytes ||
            !range_fits(*file_offset, *file_bytes, file_size)) {
            return ElfLoadResult::failure("ELF PT_LOAD 段字段无效或文件数据越界");
        }
        if (*memory_bytes == 0U) {
            continue;
        }

        const auto destination = *physical_address == 0U ? *virtual_address : *physical_address;
        if (!allowed_memory.contains(bus::PhysicalAddress{destination}, *memory_bytes)) {
            return ElfLoadResult::failure("ELF PT_LOAD 段超出一致性测试 RAM 范围");
        }
        segments.push_back(Segment{*file_offset, destination, *file_bytes, *memory_bytes});
        lowest = std::min(lowest, destination);
        highest = std::max(highest, destination + *memory_bytes);
    }
    if (segments.empty() || !allowed_memory.contains(bus::PhysicalAddress{*entry}, 2U)) {
        return ElfLoadResult::failure("ELF 没有可装载段或入口不在测试 RAM 中");
    }

    const auto tohost = find_tohost_symbol(
        bytes, *section_offset, *section_entry_size, *section_count);
    if (!tohost.has_value() ||
        !allowed_memory.contains(bus::PhysicalAddress{*tohost}, sizeof(std::uint64_t))) {
        return ElfLoadResult::failure("ELF 缺少位于测试 RAM 内的 tohost 符号");
    }

    // 全部结构和边界先验证，再开始任何总线写入，保证坏 ELF 不留下半装载状态。
    for (const auto& segment : segments) {
        const auto error = write_segment(bytes, segment, bus);
        if (error.has_value()) {
            return ElfLoadResult::failure(*error);
        }
    }
    return ElfLoadResult::success(LoadedElf{*entry, *tohost, lowest, highest});
}

}  // namespace rvemu::conformance
