// 文件职责：实现单 Hart PLIC 的优先级裁决、MMIO context 状态机和外部中断电平。
// 边界：本文件只维护 PLIC 自身寄存器；最终中断接收、委托和 Trap 入口由 CsrFile/CPU 统一处理。

#include "rvemu/devices/plic.hpp"

#include "rvemu/bus/address_map.hpp"
#include "rvemu/core/csr.hpp"

#include <stdexcept>

namespace rvemu::devices {
namespace {

constexpr std::uint64_t kPendingOffset = 0x1000U;
constexpr std::uint64_t kMachineEnableOffset = 0x2000U;
constexpr std::uint64_t kSupervisorEnableOffset = 0x2080U;
constexpr std::uint64_t kMachineContextOffset = 0x200000U;
constexpr std::uint64_t kSupervisorContextOffset = 0x201000U;
constexpr std::uint32_t kPriorityMask = 0x7U;

[[nodiscard]] bus::AddressRange create_plic_range() {
    const auto range = bus::AddressRange::create(bus::PhysicalAddress{bus::address_map::kPlic.base},
                                                 bus::address_map::kPlic.size);
    if (!range.has_value()) {
        throw std::logic_error("PLIC 固定物理地址范围无效");
    }
    return *range;
}

[[nodiscard]] bus::AccessResult unsupported_access(std::uint64_t offset,
                                                   bus::AccessWidth width,
                                                   const char* detail) {
    return bus::AccessResult::failure({bus::BusErrorCode::InvalidWidth,
                                       bus::PhysicalAddress{bus::address_map::kPlic.base + offset},
                                       bus::width_in_bytes(width),
                                       bus::address_map::kPlic.name,
                                       detail});
}

[[nodiscard]] constexpr std::uint64_t enable_offset(std::uint32_t context) noexcept {
    return context == 0U ? kMachineEnableOffset : kSupervisorEnableOffset;
}

[[nodiscard]] constexpr std::uint64_t context_offset(std::uint32_t context) noexcept {
    return context == 0U ? kMachineContextOffset : kSupervisorContextOffset;
}

}  // namespace

Plic::Plic() : AddressRegion(bus::address_map::kPlic.name, create_plic_range()) {
}

void Plic::set_source_level(std::uint32_t source, bool asserted) noexcept {
    if (source == 0U || source > kSourceCount) {
        return;
    }
    std::lock_guard<std::mutex> lock{mutex_};
    level_[source] = asserted;
    if (asserted && claimed_by_[source] == 0U) {
        pending_[source] = true;
    }
}

std::uint32_t Plic::select(std::uint32_t context) const noexcept {
    std::uint32_t selected = 0U;
    for (std::uint32_t source = 1U; source <= kSourceCount; ++source) {
        const auto enabled = (enable_[context] & (1U << source)) != 0U;
        if (!pending_[source] || claimed_by_[source] != 0U || !enabled
            || priority_[source] <= threshold_[context]) {
            continue;
        }
        // source 递增扫描且仅以严格更高优先级替换，故同级时保留更小 ID。
        if (selected == 0U || priority_[source] > priority_[selected]) {
            selected = source;
        }
    }
    return selected;
}

bool Plic::context_pending(std::uint32_t context) const noexcept {
    return select(context) != 0U;
}

void Plic::synchronize(core::CsrFile& csrs) const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    csrs.set_interrupt_pending(core::InterruptCause::MachineExternal, context_pending(0U));
    csrs.set_interrupt_pending(core::InterruptCause::SupervisorExternal, context_pending(1U));
}

bus::AccessResult Plic::read(std::uint64_t offset, bus::AccessWidth width, bus::AccessType type) {
    static_cast<void>(type);
    if (width != bus::AccessWidth::Word || (offset & 0x3U) != 0U) {
        return unsupported_access(offset, width, "PLIC 仅支持自然对齐的 32 位 MMIO 访问");
    }
    std::lock_guard<std::mutex> lock{mutex_};
    if (offset >= 4U && offset < 4U * (kSourceCount + 1U)) {
        return bus::AccessResult::success(priority_[offset / 4U]);
    }
    if (offset == kPendingOffset) {
        std::uint32_t pending = 0U;
        for (std::uint32_t source = 1U; source <= kSourceCount; ++source) {
            if (pending_[source]) {
                pending |= 1U << source;
            }
        }
        return bus::AccessResult::success(pending);
    }
    for (std::uint32_t context = 0U; context < 2U; ++context) {
        if (offset == enable_offset(context)) {
            return bus::AccessResult::success(enable_[context]);
        }
        if (offset == context_offset(context)) {
            return bus::AccessResult::success(threshold_[context]);
        }
        if (offset == context_offset(context) + 4U) {
            const auto source = select(context);
            if (source != 0U) {
                pending_[source] = false;
                claimed_by_[source] = static_cast<std::uint8_t>(context + 1U);
            }
            return bus::AccessResult::success(source);
        }
    }
    return bus::AccessResult::success();
}

bus::AccessResult Plic::write(std::uint64_t offset,
                              bus::AccessWidth width,
                              std::uint64_t value,
                              bus::AccessType type) {
    static_cast<void>(type);
    if (width != bus::AccessWidth::Word || (offset & 0x3U) != 0U) {
        return unsupported_access(offset, width, "PLIC 仅支持自然对齐的 32 位 MMIO 访问");
    }
    std::lock_guard<std::mutex> lock{mutex_};
    if (offset >= 4U && offset < 4U * (kSourceCount + 1U)) {
        priority_[offset / 4U] = static_cast<std::uint32_t>(value) & kPriorityMask;
        return bus::AccessResult::success();
    }
    for (std::uint32_t context = 0U; context < 2U; ++context) {
        if (offset == enable_offset(context)) {
            enable_[context] = static_cast<std::uint32_t>(value) & ~1U;
            return bus::AccessResult::success();
        }
        if (offset == context_offset(context)) {
            threshold_[context] = static_cast<std::uint32_t>(value) & kPriorityMask;
            return bus::AccessResult::success();
        }
        if (offset == context_offset(context) + 4U) {
            const auto source = static_cast<std::uint32_t>(value);
            if (source > 0U && source <= kSourceCount && claimed_by_[source] == context + 1U) {
                claimed_by_[source] = 0U;
                // complete 向 gateway 表示可以接收下一请求；电平仍高时立即重新锁存。
                pending_[source] = level_[source];
            }
            return bus::AccessResult::success();
        }
    }
    return bus::AccessResult::success();
}

bus::AccessResult Plic::compare_exchange(std::uint64_t offset,
                                         bus::AccessWidth width,
                                         std::uint64_t expected,
                                         std::uint64_t desired,
                                         bus::AccessType type) {
    static_cast<void>(expected);
    static_cast<void>(desired);
    static_cast<void>(type);
    return unsupported_access(offset, width, "PLIC 不支持原子 MMIO 访问");
}

}  // namespace rvemu::devices
