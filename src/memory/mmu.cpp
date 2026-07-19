// 文件职责：实现 Sv39 地址规范性检查、三级页表漫游、PTE A/D 原子更新和 64 项 TLB。
// 边界：本文件只把虚拟访问翻译为物理总线访问；最终数据读写仍由 CPU 通过总线完成。

#include "rvemu/memory/mmu.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace rvemu::memory {
namespace {

constexpr std::uint64_t kPageSize = 4096U;
constexpr std::uint64_t kPageOffsetMask = kPageSize - 1U;
constexpr std::uint64_t kSv39Mode = 8U;
constexpr std::uint64_t kSatpModeShift = 60U;
constexpr std::uint64_t kSatpAsidShift = 44U;
constexpr std::uint64_t kSatpAsidMask = 0xFFFFU;
constexpr std::uint64_t kSatpPpnMask = (1ULL << 44U) - 1U;
constexpr std::uint64_t kPtePpnMask = (1ULL << 44U) - 1U;

constexpr std::uint16_t kPteValid = 1U << 0U;
constexpr std::uint16_t kPteRead = 1U << 1U;
constexpr std::uint16_t kPteWrite = 1U << 2U;
constexpr std::uint16_t kPteExecute = 1U << 3U;
constexpr std::uint16_t kPteUser = 1U << 4U;
constexpr std::uint16_t kPteGlobal = 1U << 5U;
constexpr std::uint16_t kPteAccessed = 1U << 6U;
constexpr std::uint16_t kPteDirty = 1U << 7U;

constexpr std::uint64_t kMstatusSum = 1ULL << 18U;
constexpr std::uint64_t kMstatusMxr = 1ULL << 19U;

[[nodiscard]] constexpr std::uint64_t satp_mode(std::uint64_t satp) noexcept {
    return satp >> kSatpModeShift;
}

[[nodiscard]] constexpr std::uint16_t satp_asid(std::uint64_t satp) noexcept {
    return static_cast<std::uint16_t>((satp >> kSatpAsidShift) & kSatpAsidMask);
}

[[nodiscard]] constexpr std::uint64_t satp_root_ppn(std::uint64_t satp) noexcept {
    return satp & kSatpPpnMask;
}

[[nodiscard]] constexpr std::uint64_t vpn(std::uint64_t virtual_address,
                                          std::uint8_t level) noexcept {
    return (virtual_address >> (12U + 9U * level)) & 0x1FFU;
}

[[nodiscard]] constexpr std::uint64_t pte_ppn(std::uint64_t pte) noexcept {
    return (pte >> 10U) & kPtePpnMask;
}

[[nodiscard]] constexpr std::uint16_t pte_flags(std::uint64_t pte) noexcept {
    return static_cast<std::uint16_t>(pte & 0x3FFU);
}

[[nodiscard]] constexpr bool canonical_sv39(std::uint64_t virtual_address) noexcept {
    const auto sign = (virtual_address >> 38U) & 1U;
    const auto high = virtual_address >> 39U;
    return sign == 0U ? high == 0U : high == ((1ULL << 25U) - 1U);
}

[[nodiscard]] constexpr bool pte_valid(std::uint16_t flags) noexcept {
    return (flags & kPteValid) != 0U && !((flags & kPteWrite) != 0U && (flags & kPteRead) == 0U);
}

[[nodiscard]] constexpr bool pte_leaf(std::uint16_t flags) noexcept {
    return (flags & (kPteRead | kPteExecute)) != 0U;
}

[[nodiscard]] constexpr std::uint64_t page_size_for_level(std::uint8_t level) noexcept {
    return kPageSize << (9U * level);
}

[[nodiscard]] constexpr std::uint64_t virtual_page_for_level(std::uint64_t virtual_address,
                                                             std::uint8_t level) noexcept {
    return virtual_address / page_size_for_level(level);
}

[[nodiscard]] bool add_overflows(std::uint64_t lhs,
                                 std::uint64_t rhs,
                                 std::uint64_t& result) noexcept {
    result = lhs + rhs;
    return result < lhs;
}

[[nodiscard]] core::ExceptionCause page_fault_cause(MmuAccessKind kind) noexcept {
    switch (kind) {
        case MmuAccessKind::InstructionFetch:
            return core::ExceptionCause::InstructionPageFault;
        case MmuAccessKind::Load:
            return core::ExceptionCause::LoadPageFault;
        case MmuAccessKind::Store:
        case MmuAccessKind::Atomic:
            return core::ExceptionCause::StorePageFault;
    }
    return core::ExceptionCause::LoadPageFault;
}

[[nodiscard]] MmuFault page_fault(MmuAccessKind kind, std::uint64_t virtual_address) noexcept {
    return MmuFault{page_fault_cause(kind), virtual_address};
}

[[nodiscard]] bool permission_allows(std::uint16_t flags,
                                     MmuAccessKind kind,
                                     MmuContext context) noexcept {
    const auto user_page = (flags & kPteUser) != 0U;
    if (context.privilege == core::PrivilegeMode::User && !user_page) {
        return false;
    }
    if (context.privilege == core::PrivilegeMode::Supervisor && user_page) {
        if (kind == MmuAccessKind::InstructionFetch) {
            return false;
        }
        if ((context.mstatus & kMstatusSum) == 0U) {
            return false;
        }
    }

    switch (kind) {
        case MmuAccessKind::InstructionFetch:
            return (flags & kPteExecute) != 0U;
        case MmuAccessKind::Load:
            return (flags & kPteRead) != 0U
                   || ((context.mstatus & kMstatusMxr) != 0U && (flags & kPteExecute) != 0U);
        case MmuAccessKind::Store:
            return (flags & kPteWrite) != 0U;
        case MmuAccessKind::Atomic:
            return (flags & kPteRead) != 0U && (flags & kPteWrite) != 0U;
    }
    return false;
}

[[nodiscard]] bool superpage_aligned(std::uint64_t ppn, std::uint8_t level) noexcept {
    if (level == 2U) {
        return (ppn & ((1ULL << 18U) - 1U)) == 0U;
    }
    if (level == 1U) {
        return (ppn & ((1ULL << 9U) - 1U)) == 0U;
    }
    return true;
}

[[nodiscard]] std::uint64_t compose_physical_address(std::uint64_t virtual_address,
                                                     std::uint64_t ppn,
                                                     std::uint8_t level) noexcept {
    const auto page_offset = virtual_address & kPageOffsetMask;
    const auto ppn0 = level == 0U ? (ppn & 0x1FFU) : vpn(virtual_address, 0U);
    const auto ppn1 = level <= 1U ? ((ppn >> 9U) & 0x1FFU) : vpn(virtual_address, 1U);
    const auto ppn2 = (ppn >> 18U) & ((1ULL << 26U) - 1U);
    return (ppn2 << 30U) | (ppn1 << 21U) | (ppn0 << 12U) | page_offset;
}

}  // namespace

TranslationResult Mmu::translate(std::uint64_t virtual_address,
                                 bus::AccessWidth width,
                                 MmuAccessKind kind,
                                 MmuContext context) {
    if (context.privilege == core::PrivilegeMode::Machine || satp_mode(context.satp) == 0U) {
        static_cast<void>(width);
        return TranslationResult{bus::PhysicalAddress{virtual_address}, std::nullopt};
    }

    if (satp_mode(context.satp) != kSv39Mode || !canonical_sv39(virtual_address)) {
        return TranslationResult{std::nullopt, page_fault(kind, virtual_address)};
    }

    if (const auto cached = lookup_tlb(virtual_address, kind, context); cached.has_value()) {
        return TranslationResult{*cached, std::nullopt};
    }

    for (;;) {
        const auto walked = walk_page_table(virtual_address, kind, context);
        if (walked.retry) {
            continue;
        }
        return TranslationResult{walked.physical_address, walked.fault};
    }
}

std::optional<bus::PhysicalAddress> Mmu::lookup_tlb(std::uint64_t virtual_address,
                                                    MmuAccessKind kind,
                                                    MmuContext context) {
    const auto asid = satp_asid(context.satp);
    for (auto& entry : tlb_) {
        if (!entry.valid) {
            continue;
        }
        if (!((entry.flags & kPteGlobal) != 0U || entry.asid == asid)) {
            continue;
        }
        if (entry.virtual_page != virtual_page_for_level(virtual_address, entry.level)) {
            continue;
        }
        if (!permission_allows(entry.flags, kind, context)) {
            return std::nullopt;
        }
        if ((kind == MmuAccessKind::Store || kind == MmuAccessKind::Atomic)
            && (entry.flags & kPteDirty) == 0U) {
            return std::nullopt;
        }

        entry.age = next_tlb_age_++;
        const auto physical =
            compose_physical_address(virtual_address, entry.physical_page_number, entry.level);
        return bus::PhysicalAddress{physical};
    }
    return std::nullopt;
}

Mmu::PageWalkResult Mmu::walk_page_table(std::uint64_t virtual_address,
                                         MmuAccessKind kind,
                                         MmuContext context) {
    auto table_ppn = satp_root_ppn(context.satp);
    const auto asid = satp_asid(context.satp);

    for (std::int8_t level = 2; level >= 0; --level) {
        std::uint64_t pte_address = 0U;
        const auto table_base = table_ppn << 12U;
        if (add_overflows(table_base,
                          vpn(virtual_address, static_cast<std::uint8_t>(level)) * 8U,
                          pte_address)) {
            return PageWalkResult{std::nullopt, page_fault(kind, virtual_address), false};
        }

        const auto pte_read = bus_.read(
            bus::PhysicalAddress{pte_address}, bus::AccessWidth::DoubleWord, bus::AccessType::Load);
        if (!pte_read.ok()) {
            return PageWalkResult{std::nullopt, page_fault(kind, virtual_address), false};
        }

        const auto pte = pte_read.value;
        const auto flags = pte_flags(pte);
        if (!pte_valid(flags)) {
            return PageWalkResult{std::nullopt, page_fault(kind, virtual_address), false};
        }
        if (!pte_leaf(flags)) {
            if (level == 0) {
                return PageWalkResult{std::nullopt, page_fault(kind, virtual_address), false};
            }
            table_ppn = pte_ppn(pte);
            continue;
        }

        const auto level_u8 = static_cast<std::uint8_t>(level);
        const auto ppn = pte_ppn(pte);
        if (!superpage_aligned(ppn, level_u8) || !permission_allows(flags, kind, context)) {
            return PageWalkResult{std::nullopt, page_fault(kind, virtual_address), false};
        }

        auto desired_pte = pte;
        desired_pte |= kPteAccessed;
        if (kind == MmuAccessKind::Store || kind == MmuAccessKind::Atomic) {
            desired_pte |= kPteDirty;
        }
        if (desired_pte != pte) {
            const auto updated = bus_.compare_exchange(bus::PhysicalAddress{pte_address},
                                                       bus::AccessWidth::DoubleWord,
                                                       pte,
                                                       desired_pte,
                                                       bus::AccessType::Atomic);
            if (!updated.ok()) {
                return PageWalkResult{std::nullopt, page_fault(kind, virtual_address), false};
            }
            if (!updated.exchanged) {
                return PageWalkResult{std::nullopt, std::nullopt, true};
            }
        }

        const auto final_flags = pte_flags(desired_pte);
        const auto physical = compose_physical_address(virtual_address, ppn, level_u8);
        fill_tlb(virtual_address, level_u8, ppn, final_flags, asid);
        return PageWalkResult{bus::PhysicalAddress{physical}, std::nullopt, false};
    }

    return PageWalkResult{std::nullopt, page_fault(kind, virtual_address), false};
}

void Mmu::fill_tlb(std::uint64_t virtual_address,
                   std::uint8_t level,
                   std::uint64_t physical_page_number,
                   std::uint16_t flags,
                   std::uint16_t asid) noexcept {
    const auto virtual_page = virtual_page_for_level(virtual_address, level);
    auto victim = tlb_.begin();
    for (auto it = tlb_.begin(); it != tlb_.end(); ++it) {
        if (!it->valid) {
            victim = it;
            break;
        }
        if (it->age < victim->age) {
            victim = it;
        }
    }

    *victim =
        TlbEntry{true, virtual_page, asid, level, physical_page_number, flags, next_tlb_age_++};
}

void Mmu::sfence_vma(std::optional<std::uint64_t> virtual_address,
                     std::optional<std::uint16_t> asid) noexcept {
    for (auto& entry : tlb_) {
        if (!entry.valid) {
            continue;
        }
        if (asid.has_value() && entry.asid != *asid && (entry.flags & kPteGlobal) == 0U) {
            continue;
        }
        if (virtual_address.has_value()
            && entry.virtual_page != virtual_page_for_level(*virtual_address, entry.level)) {
            continue;
        }
        entry.valid = false;
    }
}

std::size_t Mmu::valid_tlb_entries() const noexcept {
    return static_cast<std::size_t>(std::count_if(tlb_.begin(), tlb_.end(), [](const auto& entry) {
        return entry.valid;
    }));
}

}  // namespace rvemu::memory
