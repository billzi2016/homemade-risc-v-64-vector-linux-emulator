// 文件职责：声明来宾虚拟地址到物理总线地址的唯一 Sv39/TLB 翻译入口。
// 边界：本文件不执行 CPU 指令、不维护 CSR 状态，也不直接读写通用寄存器。

#pragma once

#include "rvemu/bus/bus.hpp"
#include "rvemu/core/trap.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace rvemu::memory {

enum class MmuAccessKind : std::uint8_t {
    InstructionFetch,
    Load,
    Store,
    Atomic,
};

struct MmuContext final {
    core::PrivilegeMode privilege{core::PrivilegeMode::Machine};
    std::uint64_t satp{0U};
    std::uint64_t mstatus{0U};
};

struct MmuFault final {
    core::ExceptionCause cause{core::ExceptionCause::LoadPageFault};
    std::uint64_t value{0U};
};

struct TranslationResult final {
    std::optional<bus::PhysicalAddress> physical_address{};
    std::optional<MmuFault> fault{};
};

class Mmu final {
public:
    explicit Mmu(bus::Bus& bus) noexcept : bus_(bus) {}

    // 将一次来宾访问翻译为物理地址。成功时可能已经原子更新页表 A/D 位并填充 TLB。
    [[nodiscard]] TranslationResult translate(
        std::uint64_t virtual_address,
        bus::AccessWidth width,
        MmuAccessKind kind,
        MmuContext context);

    // 执行 SFENCE.VMA 的 TLB 失效。空 VA 表示全地址，空 ASID 表示全地址空间。
    void sfence_vma(
        std::optional<std::uint64_t> virtual_address,
        std::optional<std::uint16_t> asid) noexcept;

    [[nodiscard]] std::size_t valid_tlb_entries() const noexcept;

private:
    struct TlbEntry final {
        bool valid{false};
        std::uint64_t virtual_page{0U};
        std::uint16_t asid{0U};
        std::uint8_t level{0U};
        std::uint64_t physical_page_number{0U};
        std::uint16_t flags{0U};
        std::uint64_t age{0U};
    };

    struct PageWalkResult final {
        std::optional<bus::PhysicalAddress> physical_address{};
        std::optional<MmuFault> fault{};
        bool retry{false};
    };

    [[nodiscard]] std::optional<bus::PhysicalAddress> lookup_tlb(
        std::uint64_t virtual_address,
        MmuAccessKind kind,
        MmuContext context);

    [[nodiscard]] PageWalkResult walk_page_table(
        std::uint64_t virtual_address,
        MmuAccessKind kind,
        MmuContext context);

    void fill_tlb(
        std::uint64_t virtual_address,
        std::uint8_t level,
        std::uint64_t physical_page_number,
        std::uint16_t flags,
        std::uint16_t asid) noexcept;

    bus::Bus& bus_;
    std::array<TlbEntry, 64U> tlb_{};
    std::uint64_t next_tlb_age_{1U};
};

}  // namespace rvemu::memory
