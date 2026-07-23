// 文件职责：生成当前单 Hart virt 风格机器的 Flattened Device Tree 二进制。
// 边界：本文件不伪造启动日志、不反编译 DTB、不把生成结果写入宿主文件系统。

#include "rvemu/runtime/fdt.hpp"

#include "rvemu/bus/address_map.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rvemu::runtime {
namespace {

constexpr std::uint32_t kFdtMagic = 0xD00D'FEEDU;
constexpr std::uint32_t kFdtVersion = 17U;
constexpr std::uint32_t kFdtLastCompatibleVersion = 16U;
constexpr std::uint32_t kFdtBeginNode = 1U;
constexpr std::uint32_t kFdtEndNode = 2U;
constexpr std::uint32_t kFdtProp = 3U;
constexpr std::uint32_t kFdtEnd = 9U;
constexpr std::uint32_t kCpuIntcPhandle = 1U;
constexpr std::uint32_t kPlicPhandle = 2U;
constexpr std::uint32_t kUartInterrupt = 10U;
constexpr std::uint32_t kVirtioBlockInterrupt = 1U;
constexpr std::uint32_t kVirtioNetworkInterrupt = 2U;

void append_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24U));
    out.push_back(static_cast<std::uint8_t>(value >> 16U));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}

void append_be64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    append_be32(out, static_cast<std::uint32_t>(value >> 32U));
    append_be32(out, static_cast<std::uint32_t>(value));
}

void align4(std::vector<std::uint8_t>& out) {
    while ((out.size() & 3U) != 0U) {
        out.push_back(0U);
    }
}

class StringTable final {
   public:
    [[nodiscard]] std::uint32_t offset(const std::string& value) {
        const auto found = offsets_.find(value);
        if (found != offsets_.end()) {
            return found->second;
        }
        const auto offset = static_cast<std::uint32_t>(bytes_.size());
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        bytes_.push_back(0U);
        offsets_.emplace(value, offset);
        return offset;
    }

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
        return bytes_;
    }

   private:
    std::vector<std::uint8_t> bytes_{};
    std::unordered_map<std::string, std::uint32_t> offsets_{};
};

class FdtWriter final {
   public:
    explicit FdtWriter(StringTable& strings) noexcept : strings_(strings) {
    }

    void begin_node(const std::string& name) {
        append_be32(structure_, kFdtBeginNode);
        structure_.insert(structure_.end(), name.begin(), name.end());
        structure_.push_back(0U);
        align4(structure_);
    }

    void end_node() {
        append_be32(structure_, kFdtEndNode);
    }

    void prop_raw(const std::string& name, const std::vector<std::uint8_t>& value) {
        append_be32(structure_, kFdtProp);
        append_be32(structure_, static_cast<std::uint32_t>(value.size()));
        append_be32(structure_, strings_.offset(name));
        structure_.insert(structure_.end(), value.begin(), value.end());
        align4(structure_);
    }

    void prop_empty(const std::string& name) {
        prop_raw(name, {});
    }

    void prop_string(const std::string& name, const std::string& value) {
        std::vector<std::uint8_t> bytes{value.begin(), value.end()};
        bytes.push_back(0U);
        prop_raw(name, bytes);
    }

    void prop_u32(const std::string& name, std::uint32_t value) {
        std::vector<std::uint8_t> bytes;
        append_be32(bytes, value);
        prop_raw(name, bytes);
    }

    void prop_u64_pair(const std::string& name, std::uint64_t address, std::uint64_t size) {
        std::vector<std::uint8_t> bytes;
        append_be64(bytes, address);
        append_be64(bytes, size);
        prop_raw(name, bytes);
    }

    void prop_u32_list(const std::string& name, const std::vector<std::uint32_t>& values) {
        std::vector<std::uint8_t> bytes;
        for (const auto value : values) {
            append_be32(bytes, value);
        }
        prop_raw(name, bytes);
    }

    [[nodiscard]] std::vector<std::uint8_t> finish() && {
        append_be32(structure_, kFdtEnd);
        return std::move(structure_);
    }

   private:
    StringTable& strings_;
    std::vector<std::uint8_t> structure_{};
};

[[nodiscard]] bool range_overflows(std::uint64_t base, std::uint64_t size) noexcept {
    return size == 0U || base > std::numeric_limits<std::uint64_t>::max() - size;
}

[[nodiscard]] std::vector<std::uint8_t> join_blob(const std::vector<std::uint8_t>& reserve,
                                                  const std::vector<std::uint8_t>& structure,
                                                  const std::vector<std::uint8_t>& strings) {
    constexpr std::uint32_t kHeaderSize = 40U;
    const auto reserve_offset = kHeaderSize;
    const auto structure_offset = reserve_offset + static_cast<std::uint32_t>(reserve.size());
    const auto strings_offset = structure_offset + static_cast<std::uint32_t>(structure.size());
    const auto total_size = strings_offset + static_cast<std::uint32_t>(strings.size());

    std::vector<std::uint8_t> blob;
    append_be32(blob, kFdtMagic);
    append_be32(blob, total_size);
    append_be32(blob, structure_offset);
    append_be32(blob, strings_offset);
    append_be32(blob, reserve_offset);
    append_be32(blob, kFdtVersion);
    append_be32(blob, kFdtLastCompatibleVersion);
    append_be32(blob, 0U);
    append_be32(blob, static_cast<std::uint32_t>(strings.size()));
    append_be32(blob, static_cast<std::uint32_t>(structure.size()));
    blob.insert(blob.end(), reserve.begin(), reserve.end());
    blob.insert(blob.end(), structure.begin(), structure.end());
    blob.insert(blob.end(), strings.begin(), strings.end());
    return blob;
}

}  // namespace

FdtBuildResult build_machine_fdt(const FdtConfig& config) {
    if (range_overflows(config.ram_base, config.ram_size)
        || range_overflows(config.fdt_address, config.fdt_reserved_size)) {
        return FdtBuildResult{{}, "RAM 或 FDT 保留区范围无效"};
    }
    if (config.fdt_address < config.ram_base
        || config.fdt_address + config.fdt_reserved_size > config.ram_base + config.ram_size) {
        return FdtBuildResult{{}, "FDT 保留区必须完全位于 RAM 内"};
    }
    StringTable strings;
    FdtWriter writer{strings};
    writer.begin_node("");
    writer.prop_string("compatible", "riscv-virtio");
    writer.prop_string("model", "rvemu,riscv64-gcv-single-hart");
    writer.prop_u32("#address-cells", 2U);
    writer.prop_u32("#size-cells", 2U);

    writer.begin_node("chosen");
    writer.prop_string("stdout-path", "/soc/serial@10000000:115200n8");
    writer.prop_string("bootargs", config.bootargs);
    writer.end_node();

    writer.begin_node("memory@80000000");
    writer.prop_string("device_type", "memory");
    writer.prop_u64_pair("reg", config.ram_base, config.ram_size);
    writer.end_node();

    writer.begin_node("cpus");
    writer.prop_u32("#address-cells", 1U);
    writer.prop_u32("#size-cells", 0U);
    writer.prop_u32("timebase-frequency", config.timebase_frequency);
    writer.begin_node("cpu@0");
    writer.prop_string("device_type", "cpu");
    writer.prop_u32("reg", 0U);
    writer.prop_string("status", "okay");
    writer.prop_string("compatible", "riscv");
    writer.prop_string("riscv,isa", config.isa);
    writer.prop_string("mmu-type", "riscv,sv39");
    writer.begin_node("interrupt-controller");
    writer.prop_empty("interrupt-controller");
    writer.prop_u32("#interrupt-cells", 1U);
    writer.prop_string("compatible", "riscv,cpu-intc");
    writer.prop_u32("phandle", kCpuIntcPhandle);
    writer.end_node();
    writer.end_node();
    writer.end_node();

    writer.begin_node("soc");
    writer.prop_u32("#address-cells", 2U);
    writer.prop_u32("#size-cells", 2U);
    writer.prop_empty("ranges");
    writer.prop_string("compatible", "simple-bus");

    writer.begin_node("clint@2000000");
    writer.prop_string("compatible", "riscv,clint0");
    writer.prop_u64_pair("reg",
                         bus::address_map::kClint.base,
                         bus::address_map::kClint.size);
    writer.prop_u32_list("interrupts-extended", {kCpuIntcPhandle, 3U, kCpuIntcPhandle, 7U});
    writer.end_node();

    writer.begin_node("interrupt-controller@c000000");
    writer.prop_string("compatible", "sifive,plic-1.0.0");
    writer.prop_empty("interrupt-controller");
    writer.prop_u32("#interrupt-cells", 1U);
    writer.prop_u64_pair("reg", bus::address_map::kPlic.base, bus::address_map::kPlic.size);
    writer.prop_u32("riscv,ndev", 31U);
    writer.prop_u32("phandle", kPlicPhandle);
    writer.prop_u32_list("interrupts-extended", {kCpuIntcPhandle, 11U, kCpuIntcPhandle, 9U});
    writer.end_node();

    writer.begin_node("serial@10000000");
    writer.prop_string("compatible", "ns16550a");
    writer.prop_u64_pair("reg", bus::address_map::kUart.base, bus::address_map::kUart.size);
    writer.prop_u32("clock-frequency", 3'686'400U);
    writer.prop_u32("current-speed", 115'200U);
    writer.prop_u32("interrupt-parent", kPlicPhandle);
    writer.prop_u32("interrupts", kUartInterrupt);
    writer.end_node();

    writer.begin_node("virtio@10001000");
    writer.prop_string("compatible", "virtio,mmio");
    writer.prop_u64_pair(
        "reg", bus::address_map::kVirtioBlock.base, bus::address_map::kVirtioBlock.size);
    writer.prop_u32("interrupt-parent", kPlicPhandle);
    writer.prop_u32("interrupts", kVirtioBlockInterrupt);
    writer.end_node();

    writer.begin_node("virtio@10002000");
    writer.prop_string("compatible", "virtio,mmio");
    writer.prop_u64_pair("reg",
                         bus::address_map::kVirtioNetwork.base,
                         bus::address_map::kVirtioNetwork.size);
    writer.prop_u32("interrupt-parent", kPlicPhandle);
    writer.prop_u32("interrupts", kVirtioNetworkInterrupt);
    writer.end_node();
    writer.end_node();
    writer.end_node();

    std::vector<std::uint8_t> reserve;
    append_be64(reserve, config.fdt_address);
    append_be64(reserve, config.fdt_reserved_size);
    append_be64(reserve, 0U);
    append_be64(reserve, 0U);
    auto structure = std::move(writer).finish();
    return FdtBuildResult{join_blob(reserve, structure, strings.bytes()), {}};
}

}  // namespace rvemu::runtime
