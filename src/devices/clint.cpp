// 文件职责：实现单 Hart CLINT 的标准 MMIO 寄存器访问、计时推进及 MSIP/MTIP 电平生成。
// 边界：本文件不访问 CPU PC、不进行中断委托，也不依赖宿主墙上时间或创建第二套 CSR 状态。

#include "rvemu/devices/clint.hpp"

#include "rvemu/bus/address_map.hpp"
#include "rvemu/core/csr.hpp"

#include <stdexcept>

namespace rvemu::devices {
namespace {

[[nodiscard]] bus::AddressRange require_clint_range() {
    const auto range = bus::AddressRange::create(
        bus::PhysicalAddress{bus::address_map::kClint.base}, bus::address_map::kClint.size);
    if (!range.has_value()) {
        throw std::logic_error("CLINT 固定物理地址范围无效");
    }
    return *range;
}

[[nodiscard]] bus::AccessResult invalid_access(std::uint64_t offset,
                                               bus::AccessWidth width,
                                               const char* detail) {
    return bus::AccessResult::failure(
        bus::BusError{bus::BusErrorCode::InvalidWidth,
                      bus::PhysicalAddress{bus::address_map::kClint.base + offset},
                      bus::width_in_bytes(width),
                      bus::address_map::kClint.name,
                      detail});
}

}  // namespace

Clint::Clint() : AddressRegion(bus::address_map::kClint.name, require_clint_range()) {
}

void Clint::advance(std::uint64_t ticks) noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    mtime_ += ticks;
}

void Clint::synchronize(core::CsrFile& csrs) const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    csrs.set_time(mtime_);
    csrs.set_interrupt_pending(core::InterruptCause::MachineSoftware, msip_);
    csrs.set_interrupt_pending(core::InterruptCause::MachineTimer, mtime_ >= mtimecmp_);
}

bus::AccessResult Clint::validate_access(std::uint64_t offset, bus::AccessWidth width) const {
    const auto byte_count = bus::width_in_bytes(width);
    if (width != bus::AccessWidth::Word && width != bus::AccessWidth::DoubleWord) {
        return invalid_access(offset, width, "CLINT 仅支持自然对齐的 32 位或 64 位寄存器访问");
    }
    if ((offset % byte_count) != 0U) {
        return invalid_access(offset, width, "CLINT 寄存器访问未按访问宽度自然对齐");
    }
    if (offset == kMsipOffset && width == bus::AccessWidth::Word) {
        return bus::AccessResult::success();
    }
    if (is_timer_register_access(offset, width, kMtimecmpOffset)
        || is_timer_register_access(offset, width, kMtimeOffset)) {
        return bus::AccessResult::success();
    }
    return invalid_access(offset, width, "CLINT 未实现该寄存器或访问跨越寄存器边界");
}

bool Clint::is_timer_register_access(std::uint64_t offset,
                                     bus::AccessWidth width,
                                     std::uint64_t register_offset) noexcept {
    if (width == bus::AccessWidth::DoubleWord) {
        return offset == register_offset;
    }
    return width == bus::AccessWidth::Word
           && (offset == register_offset || offset == register_offset + 4U);
}

void Clint::write_timer_register(std::uint64_t& destination,
                                 std::uint64_t offset,
                                 std::uint64_t register_offset,
                                 bus::AccessWidth width,
                                 std::uint64_t value) noexcept {
    if (width == bus::AccessWidth::DoubleWord) {
        destination = value;
        return;
    }
    const auto half = value & 0xFFFF'FFFFULL;
    if (offset == register_offset) {
        destination = (destination & 0xFFFF'FFFF'0000'0000ULL) | half;
    } else {
        destination = (destination & 0x0000'0000'FFFF'FFFFULL) | (half << 32U);
    }
}

std::uint64_t Clint::read_timer_register(std::uint64_t source,
                                         std::uint64_t offset,
                                         std::uint64_t register_offset,
                                         bus::AccessWidth width) noexcept {
    if (width == bus::AccessWidth::DoubleWord) {
        return source;
    }
    return offset == register_offset ? source & 0xFFFF'FFFFULL : source >> 32U;
}

bus::AccessResult Clint::read(std::uint64_t offset, bus::AccessWidth width, bus::AccessType type) {
    static_cast<void>(type);
    const auto validation = validate_access(offset, width);
    if (!validation.ok()) {
        return validation;
    }

    std::lock_guard<std::mutex> lock{mutex_};
    if (offset == kMsipOffset) {
        return bus::AccessResult::success(msip_ ? 1U : 0U);
    }
    if (is_timer_register_access(offset, width, kMtimecmpOffset)) {
        return bus::AccessResult::success(
            read_timer_register(mtimecmp_, offset, kMtimecmpOffset, width));
    }
    return bus::AccessResult::success(read_timer_register(mtime_, offset, kMtimeOffset, width));
}

bus::AccessResult Clint::write(std::uint64_t offset,
                               bus::AccessWidth width,
                               std::uint64_t value,
                               bus::AccessType type) {
    static_cast<void>(type);
    const auto validation = validate_access(offset, width);
    if (!validation.ok()) {
        return validation;
    }

    std::lock_guard<std::mutex> lock{mutex_};
    if (offset == kMsipOffset) {
        // CLINT msip 只有 bit 0 具有架构含义，高位写入必须不影响软件中断电平。
        msip_ = (value & 1U) != 0U;
    } else if (is_timer_register_access(offset, width, kMtimecmpOffset)) {
        write_timer_register(mtimecmp_, offset, kMtimecmpOffset, width, value);
    } else {
        write_timer_register(mtime_, offset, kMtimeOffset, width, value);
    }
    return bus::AccessResult::success();
}

bus::AccessResult Clint::compare_exchange(std::uint64_t offset,
                                          bus::AccessWidth width,
                                          std::uint64_t expected,
                                          std::uint64_t desired,
                                          bus::AccessType type) {
    static_cast<void>(expected);
    static_cast<void>(desired);
    static_cast<void>(type);
    return invalid_access(offset, width, "CLINT 不支持原子 MMIO 访问");
}

}  // namespace rvemu::devices
