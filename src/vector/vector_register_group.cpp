// 文件职责：实现 RVV 1.0 对 SEW、整数/分数 LMUL、寄存器组对齐和小端元素映射的共同规则。
// 边界：本文件不修改 VS/CSR；直接使用 VectorState 仅服务于纯布局层，CPU 提交必须经 CpuState 入口标记 Dirty。

#include "rvemu/vector/vector_register_group.hpp"

namespace rvemu::vector {
namespace {

constexpr std::uint64_t kBitsPerByte = 8U;

// 整数 LMUL 才会占用多个物理寄存器；分数 LMUL 只使用基寄存器低部的有效容量。
[[nodiscard]] std::uint64_t physical_register_count(
    const VectorConfiguration& configuration) noexcept {
    return configuration.lmul_numerator >= configuration.lmul_denominator ?
               configuration.lmul_numerator / configuration.lmul_denominator :
               1U;
}

}  // namespace

std::optional<VectorRegisterGroup> VectorRegisterGroup::create(
    const VectorConfiguration& configuration,
    std::uint8_t base_register) noexcept {
    if (!configuration.valid || base_register >= VectorState::kRegisterCount) {
        return std::nullopt;
    }
    const auto register_count = physical_register_count(configuration);
    // 整数 LMUL 的基寄存器必须按组大小对齐，否则相邻操作数会产生不可见的重叠。
    if ((base_register % register_count) != 0U ||
        register_count > VectorState::kRegisterCount - base_register) {
        return std::nullopt;
    }
    return VectorRegisterGroup{configuration, base_register};
}

std::optional<VectorElementLocation> VectorRegisterGroup::locate(
    std::uint64_t element_index) const noexcept {
    if (element_index >= configuration_.vlmax) {
        return std::nullopt;
    }
    const auto width_bytes = configuration_.sew_bits / kBitsPerByte;
    const auto byte_index = element_index * width_bytes;
    const auto register_offset = byte_index / VectorState::kRegisterBytes;
    const auto byte_offset = byte_index % VectorState::kRegisterBytes;
    // SEW 限为 8/16/32/64，且每一宽度整除 VLEN，故已验证的元素不会跨寄存器边界。
    if (register_offset >= physical_register_count(configuration_) ||
        byte_offset + width_bytes > VectorState::kRegisterBytes) {
        return std::nullopt;
    }
    return VectorElementLocation{
        static_cast<std::uint8_t>(base_register_ + register_offset),
        static_cast<std::uint8_t>(byte_offset),
        static_cast<std::uint8_t>(width_bytes),
    };
}

std::optional<std::uint64_t> VectorRegisterGroup::read_element(
    const VectorState& state,
    std::uint64_t element_index) const {
    const auto location = locate(element_index);
    if (!location.has_value()) {
        return std::nullopt;
    }
    std::uint64_t value = 0U;
    for (std::uint8_t byte = 0U; byte < location->width_bytes; ++byte) {
        value |= static_cast<std::uint64_t>(
                     state.byte_value(location->register_index, location->byte_offset + byte))
                 << (byte * kBitsPerByte);
    }
    return value;
}

bool VectorRegisterGroup::write_element(
    VectorState& state,
    std::uint64_t element_index,
    std::uint64_t value) const {
    const auto location = locate(element_index);
    if (!location.has_value()) {
        return false;
    }
    // 位置已完整验证后才开始逐字节提交，保证失败分支不留下部分元素写入。
    for (std::uint8_t byte = 0U; byte < location->width_bytes; ++byte) {
        state.set_byte_value(
            location->register_index,
            location->byte_offset + byte,
            static_cast<std::uint8_t>(value >> (byte * kBitsPerByte)));
    }
    return true;
}

std::optional<bool> VectorRegisterGroup::mask_bit(
    const VectorState& state,
    std::uint64_t element_index) noexcept {
    constexpr std::uint64_t kMaskBitCapacity = VectorState::kRegisterBytes * kBitsPerByte;
    if (element_index >= kMaskBitCapacity) {
        return std::nullopt;
    }
    const auto byte_index = static_cast<std::uint8_t>(element_index / kBitsPerByte);
    const auto bit_index = static_cast<std::uint8_t>(element_index % kBitsPerByte);
    return ((state.byte_value(0U, byte_index) >> bit_index) & 0x1U) != 0U;
}

}  // namespace rvemu::vector
