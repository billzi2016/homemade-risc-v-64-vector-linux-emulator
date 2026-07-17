// 文件职责：实现 RV64 M/S CSR 的唯一底层状态、WARL 规则、别名、委托和状态栈。
// 边界：本文件不执行 SYSTEM 机器码；外设只能通过 pending 接口投影中断源状态。

#include "rvemu/core/csr.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace rvemu::core {
namespace {

// 本组 constexpr 辅助只构造安全位掩码和 cause 编码，不读取或改变 CSR 状态。
constexpr std::uint64_t bit(std::uint8_t index) noexcept {
    return 1ULL << index;
}

constexpr std::uint64_t kSstatusWriteMask =
    bit(1U) | bit(5U) | bit(8U) | (0x3ULL << 9U) | (0x3ULL << 13U) |
    bit(18U) | bit(19U);
constexpr std::uint64_t kSstatusReadMask = kSstatusWriteMask | (0x3ULL << 32U) | bit(63U);
constexpr std::uint64_t kMstatusWriteMask =
    kSstatusWriteMask | bit(3U) | bit(7U) | (0x3ULL << 11U) |
    bit(17U) | bit(20U) | bit(21U) | bit(22U);
constexpr std::uint64_t kXlenFields = (0x2ULL << 32U) | (0x2ULL << 34U);

constexpr std::uint64_t kSupervisorInterruptMask = bit(1U) | bit(5U) | bit(9U);
constexpr std::uint64_t kMachineInterruptMask = bit(3U) | bit(7U) | bit(11U);
constexpr std::uint64_t kImplementedInterruptMask =
    kSupervisorInterruptMask | kMachineInterruptMask;
constexpr std::uint64_t kSoftwareWritablePendingMask = bit(1U);

constexpr std::uint64_t kDelegatableExceptionMask =
    bit(0U) | bit(1U) | bit(2U) | bit(3U) | bit(4U) | bit(5U) | bit(6U) |
    bit(7U) | bit(8U) | bit(9U) | bit(12U) | bit(13U) | bit(15U);

constexpr std::uint64_t kMisa =
    (0x2ULL << 62U) | bit(static_cast<std::uint8_t>('I' - 'A')) |
    bit(static_cast<std::uint8_t>('M' - 'A')) | bit(static_cast<std::uint8_t>('S' - 'A')) |
    bit(static_cast<std::uint8_t>('U' - 'A'));
constexpr std::uint64_t kSatpModeMask = 0xFULL << 60U;
constexpr std::uint64_t kSatpSv39Mode = 8ULL << 60U;

// PrivilegeMode 编码与架构等级一致，但比较集中在此处，避免调用点散落强制转换。
[[nodiscard]] constexpr std::uint8_t privilege_rank(PrivilegeMode privilege) noexcept {
    return static_cast<std::uint8_t>(privilege);
}

// cause 超出 XLEN 时返回零，避免对 64 位整数执行未定义移位。
[[nodiscard]] constexpr std::uint64_t cause_bit(std::uint64_t cause) noexcept {
    return cause < 64U ? bit(static_cast<std::uint8_t>(cause)) : 0U;
}

// 中断枚举值等于 cause 编码；集中转换可明确保留其无符号位模式。
[[nodiscard]] constexpr std::uint64_t interrupt_code(InterruptCause cause) noexcept {
    return static_cast<std::uint64_t>(cause);
}

}  // namespace

void CsrFile::reset() noexcept {
    mstatus_ = kXlenFields;
    medeleg_ = 0U;
    mideleg_ = 0U;
    mie_ = 0U;
    mtvec_ = 0U;
    mscratch_ = 0U;
    mepc_ = 0U;
    mcause_ = 0U;
    mtval_ = 0U;
    mip_ = 0U;
    mcounteren_ = 0U;
    stvec_ = 0U;
    sscratch_ = 0U;
    sepc_ = 0U;
    scause_ = 0U;
    stval_ = 0U;
    scounteren_ = 0U;
    satp_ = 0U;
    cycle_ = 0U;
    time_ = 0U;
    instret_ = 0U;
}

// 地址存在性使用穷举列表，而非为 4096 个 CSR 分配通用数组，避免保留地址被误实现。
bool CsrFile::exists(CsrAddress address) const noexcept {
    switch (address) {
    case CsrAddress::Sstatus:
    case CsrAddress::Sie:
    case CsrAddress::Stvec:
    case CsrAddress::Scounteren:
    case CsrAddress::Sscratch:
    case CsrAddress::Sepc:
    case CsrAddress::Scause:
    case CsrAddress::Stval:
    case CsrAddress::Sip:
    case CsrAddress::Satp:
    case CsrAddress::Cycle:
    case CsrAddress::Time:
    case CsrAddress::Instret:
    case CsrAddress::Mstatus:
    case CsrAddress::Misa:
    case CsrAddress::Medeleg:
    case CsrAddress::Mideleg:
    case CsrAddress::Mie:
    case CsrAddress::Mtvec:
    case CsrAddress::Mcounteren:
    case CsrAddress::Mscratch:
    case CsrAddress::Mepc:
    case CsrAddress::Mcause:
    case CsrAddress::Mtval:
    case CsrAddress::Mip:
    case CsrAddress::Mcycle:
    case CsrAddress::Minstret:
    case CsrAddress::Mvendorid:
    case CsrAddress::Marchid:
    case CsrAddress::Mimpid:
    case CsrAddress::Mhartid:
        return true;
    }
    return false;
}

// 地址高位提供第一层通用权限；counteren 和 TVM 属于无法仅靠地址编码表达的第二层门控。
bool CsrFile::access_allowed(
    CsrAddress address,
    PrivilegeMode privilege,
    bool write) const noexcept {
    if (!exists(address)) {
        return false;
    }

    const auto encoded = static_cast<std::uint16_t>(address);
    const auto required = static_cast<std::uint8_t>((encoded >> 8U) & 0x3U);
    if (required == 0x2U || privilege_rank(privilege) < required) {
        return false;
    }
    constexpr std::uint16_t read_only_encoding = 0x0C00U;
    if (write && (encoded & read_only_encoding) == read_only_encoding) {
        return false;
    }

    if (address == CsrAddress::Cycle || address == CsrAddress::Time ||
        address == CsrAddress::Instret) {
        const auto counter_index = static_cast<std::uint8_t>(encoded & 0x1FU);
        const auto counter_mask = bit(counter_index);
        if (privilege == PrivilegeMode::Supervisor &&
            (mcounteren_ & counter_mask) == 0U) {
            return false;
        }
        if (privilege == PrivilegeMode::User &&
            ((mcounteren_ & counter_mask) == 0U ||
             (scounteren_ & counter_mask) == 0U)) {
            return false;
        }
    }

    // TVM 只在 S-mode 拦截 satp；M-mode 始终能够访问监督级地址翻译控制。
    if (address == CsrAddress::Satp && privilege == PrivilegeMode::Supervisor &&
        (mstatus_ & bit(20U)) != 0U) {
        return false;
    }
    return true;
}

// Set/Clear 必须读取旧值参与计算；Replace 且 rd=x0 时刻意不读，以满足 Zicsr 读副作用规则。
CsrAccessResult CsrFile::access(const CsrAccessRequest& request) noexcept {
    if (!access_allowed(request.address, request.privilege, request.write)) {
        return CsrAccessResult{};
    }

    const auto operation_needs_old_value = request.operation != CsrModifyOperation::Replace;
    const auto previous = request.read || (request.write && operation_needs_old_value) ?
                              read_value(request.address) : 0U;
    if (request.write) {
        std::uint64_t desired = request.operand;
        if (request.operation == CsrModifyOperation::SetBits) {
            desired = previous | request.operand;
        } else if (request.operation == CsrModifyOperation::ClearBits) {
            desired = previous & ~request.operand;
        }
        write_value(request.address, desired);
    }
    return CsrAccessResult{true, request.read ? previous : 0U};
}

// 该观察入口只返回已实现地址的架构值，不允许通过强转未知地址读取未初始化存储。
std::uint64_t CsrFile::peek(CsrAddress address) const noexcept {
    return exists(address) ? read_value(address) : 0U;
}

// Supervisor CSR 在此处直接从 Machine 底层字段投影，保证别名永远不可能分叉。
std::uint64_t CsrFile::read_value(CsrAddress address) const noexcept {
    switch (address) {
    case CsrAddress::Sstatus:
        return read_value(CsrAddress::Mstatus) & kSstatusReadMask;
    case CsrAddress::Sie:
        return mie_ & mideleg_ & kSupervisorInterruptMask;
    case CsrAddress::Stvec:
        return stvec_;
    case CsrAddress::Scounteren:
        return scounteren_;
    case CsrAddress::Sscratch:
        return sscratch_;
    case CsrAddress::Sepc:
        return sepc_;
    case CsrAddress::Scause:
        return scause_;
    case CsrAddress::Stval:
        return stval_;
    case CsrAddress::Sip:
        return mip_ & mideleg_ & kSupervisorInterruptMask;
    case CsrAddress::Satp:
        return satp_;
    case CsrAddress::Cycle:
    case CsrAddress::Mcycle:
        return cycle_;
    case CsrAddress::Time:
        return time_;
    case CsrAddress::Instret:
    case CsrAddress::Minstret:
        return instret_;
    case CsrAddress::Mstatus: {
        auto value = (mstatus_ & ~bit(63U)) | kXlenFields;
        const auto fs = (value >> 13U) & 0x3U;
        const auto vs = (value >> 9U) & 0x3U;
        if (fs == 0x3U || vs == 0x3U) {
            value |= bit(63U);
        }
        return value;
    }
    case CsrAddress::Misa:
        return kMisa;
    case CsrAddress::Medeleg:
        return medeleg_;
    case CsrAddress::Mideleg:
        return mideleg_;
    case CsrAddress::Mie:
        return mie_;
    case CsrAddress::Mtvec:
        return mtvec_;
    case CsrAddress::Mcounteren:
        return mcounteren_;
    case CsrAddress::Mscratch:
        return mscratch_;
    case CsrAddress::Mepc:
        return mepc_;
    case CsrAddress::Mcause:
        return mcause_;
    case CsrAddress::Mtval:
        return mtval_;
    case CsrAddress::Mip:
        return mip_;
    case CsrAddress::Mvendorid:
    case CsrAddress::Marchid:
    case CsrAddress::Mimpid:
    case CsrAddress::Mhartid:
        return 0U;
    }
    return 0U;
}

// 每个 case 只更新自身可写域；只读、WPRI 和未委托位在进入这里前后都不会形成隐藏状态。
void CsrFile::write_value(CsrAddress address, std::uint64_t value) noexcept {
    switch (address) {
    case CsrAddress::Sstatus:
        mstatus_ = (mstatus_ & ~kSstatusWriteMask) | (value & kSstatusWriteMask);
        break;
    case CsrAddress::Sie: {
        const auto mask = mideleg_ & kSupervisorInterruptMask;
        mie_ = (mie_ & ~mask) | (value & mask);
        break;
    }
    case CsrAddress::Stvec: {
        const auto mode = (value & 0x3U) <= 1U ? value & 0x3U : 0U;
        stvec_ = (value & ~0x3ULL) | mode;
        break;
    }
    case CsrAddress::Scounteren:
        scounteren_ = value & 0x7U;
        break;
    case CsrAddress::Sscratch:
        sscratch_ = value;
        break;
    case CsrAddress::Sepc:
        sepc_ = value & ~0x3ULL;
        break;
    case CsrAddress::Scause:
        scause_ = value;
        break;
    case CsrAddress::Stval:
        stval_ = value;
        break;
    case CsrAddress::Sip: {
        const auto mask = mideleg_ & kSoftwareWritablePendingMask;
        mip_ = (mip_ & ~mask) | (value & mask);
        break;
    }
    case CsrAddress::Satp: {
        const auto mode = value & kSatpModeMask;
        if (mode == 0U || mode == kSatpSv39Mode) {
            satp_ = value;
        }
        break;
    }
    case CsrAddress::Mstatus: {
        auto desired = (mstatus_ & ~kMstatusWriteMask) | (value & kMstatusWriteMask);
        const auto mpp = (desired >> 11U) & 0x3U;
        if (mpp == 0x2U) {
            desired = (desired & ~(0x3ULL << 11U)) | (0x3ULL << 11U);
        }
        mstatus_ = desired | kXlenFields;
        break;
    }
    case CsrAddress::Medeleg:
        medeleg_ = value & kDelegatableExceptionMask;
        break;
    case CsrAddress::Mideleg:
        mideleg_ = value & kSupervisorInterruptMask;
        break;
    case CsrAddress::Mie:
        mie_ = value & kImplementedInterruptMask;
        break;
    case CsrAddress::Mtvec: {
        const auto mode = (value & 0x3U) <= 1U ? value & 0x3U : 0U;
        mtvec_ = (value & ~0x3ULL) | mode;
        break;
    }
    case CsrAddress::Mcounteren:
        mcounteren_ = value & 0x7U;
        break;
    case CsrAddress::Mscratch:
        mscratch_ = value;
        break;
    case CsrAddress::Mepc:
        mepc_ = value & ~0x3ULL;
        break;
    case CsrAddress::Mcause:
        mcause_ = value;
        break;
    case CsrAddress::Mtval:
        mtval_ = value;
        break;
    case CsrAddress::Mip:
        mip_ = (mip_ & ~kSoftwareWritablePendingMask) |
               (value & kSoftwareWritablePendingMask);
        break;
    case CsrAddress::Mcycle:
        cycle_ = value;
        break;
    case CsrAddress::Minstret:
        instret_ = value;
        break;
    case CsrAddress::Cycle:
    case CsrAddress::Time:
    case CsrAddress::Instret:
    case CsrAddress::Misa:
    case CsrAddress::Mvendorid:
    case CsrAddress::Marchid:
    case CsrAddress::Mimpid:
    case CsrAddress::Mhartid:
        break;
    }
}

// 外设电平更新与来宾软件写 mip 分离，确保软件不能清除 MTIP/MEIP 等只读硬件来源。
void CsrFile::set_interrupt_pending(InterruptCause cause, bool pending) noexcept {
    const auto mask = cause_bit(interrupt_code(cause)) & kImplementedInterruptMask;
    if (pending) {
        mip_ |= mask;
    } else {
        mip_ &= ~mask;
    }
}

// WFI 唤醒条件比“立即接收中断”宽：规范要求忽略全局 xIE 和 mideleg，但尊重单项 mie。
bool CsrFile::has_locally_enabled_interrupt() const noexcept {
    return (mip_ & mie_ & kImplementedInterruptMask) != 0U;
}

// 在单一循环中同时完成优先级、委托目标和当前特权全局使能判断，禁止设备各自路由。
std::optional<PendingInterrupt> CsrFile::select_pending_interrupt(
    PrivilegeMode current_privilege) const noexcept {
    // Privileged 1.13 规定的递减优先级：MEI、MSI、MTI、SEI、SSI、STI。
    constexpr std::array<InterruptCause, 6U> priority{
        InterruptCause::MachineExternal,
        InterruptCause::MachineSoftware,
        InterruptCause::MachineTimer,
        InterruptCause::SupervisorExternal,
        InterruptCause::SupervisorSoftware,
        InterruptCause::SupervisorTimer,
    };

    const auto candidates = mip_ & mie_ & kImplementedInterruptMask;
    for (const auto cause : priority) {
        const auto mask = cause_bit(interrupt_code(cause));
        if ((candidates & mask) == 0U) {
            continue;
        }

        const auto delegated = (mideleg_ & mask) != 0U;
        if (delegated) {
            if (current_privilege == PrivilegeMode::Machine) {
                continue;
            }
            const auto globally_enabled = current_privilege == PrivilegeMode::User ||
                                          (mstatus_ & bit(1U)) != 0U;
            if (globally_enabled) {
                return PendingInterrupt{cause, PrivilegeMode::Supervisor};
            }
            continue;
        }

        const auto globally_enabled = current_privilege != PrivilegeMode::Machine ||
                                      (mstatus_ & bit(3U)) != 0U;
        if (globally_enabled) {
            return PendingInterrupt{cause, PrivilegeMode::Machine};
        }
    }
    return std::nullopt;
}

// M Trap 只修改机器级状态栈和 Trap CSR；先前 MIE 即使来源低于 M 也必须保存到 MPIE。
void CsrFile::enter_machine_trap(
    PrivilegeMode previous_privilege,
    std::uint64_t program_counter,
    std::uint64_t cause,
    std::uint64_t trap_value) noexcept {
    const auto mie = (mstatus_ & bit(3U)) != 0U;
    mstatus_ = mie ? mstatus_ | bit(7U) : mstatus_ & ~bit(7U);
    mstatus_ &= ~bit(3U);
    mstatus_ = (mstatus_ & ~(0x3ULL << 11U)) |
               (static_cast<std::uint64_t>(previous_privilege) << 11U);
    mepc_ = program_counter & ~0x3ULL;
    mcause_ = cause;
    mtval_ = trap_value;
}

// SPP 只有一位：U 来源编码为 0，S 来源编码为 1；M 来源不会合法到达该入口。
void CsrFile::enter_supervisor_trap(
    PrivilegeMode previous_privilege,
    std::uint64_t program_counter,
    std::uint64_t cause,
    std::uint64_t trap_value) noexcept {
    const auto sie = (mstatus_ & bit(1U)) != 0U;
    mstatus_ = sie ? mstatus_ | bit(5U) : mstatus_ & ~bit(5U);
    mstatus_ &= ~bit(1U);
    if (previous_privilege == PrivilegeMode::User) {
        mstatus_ &= ~bit(8U);
    } else {
        mstatus_ |= bit(8U);
    }
    sepc_ = program_counter & ~0x3ULL;
    scause_ = cause;
    stval_ = trap_value;
}

// MRET 恢复完成后把 MPP 置为最低实现模式 U，以便尽早暴露错误的重复返回。
TrapReturnState CsrFile::return_from_machine() noexcept {
    const auto encoded = static_cast<std::uint8_t>((mstatus_ >> 11U) & 0x3U);
    const auto target = encoded == 0U ? PrivilegeMode::User
                        : encoded == 1U ? PrivilegeMode::Supervisor
                                        : PrivilegeMode::Machine;
    const auto mpie = (mstatus_ & bit(7U)) != 0U;
    mstatus_ = mpie ? mstatus_ | bit(3U) : mstatus_ & ~bit(3U);
    mstatus_ |= bit(7U);
    mstatus_ &= ~(0x3ULL << 11U);
    if (target != PrivilegeMode::Machine) {
        mstatus_ &= ~bit(17U);
    }
    return TrapReturnState{target, mepc_};
}

// SRET 总会返回 S 或 U，因此按规范无条件清除 MPRV，并把 SPP 重置为 U。
TrapReturnState CsrFile::return_from_supervisor() noexcept {
    const auto target = (mstatus_ & bit(8U)) != 0U ?
                            PrivilegeMode::Supervisor : PrivilegeMode::User;
    const auto spie = (mstatus_ & bit(5U)) != 0U;
    mstatus_ = spie ? mstatus_ | bit(1U) : mstatus_ & ~bit(1U);
    mstatus_ |= bit(5U);
    mstatus_ &= ~bit(8U);
    mstatus_ &= ~bit(17U);
    return TrapReturnState{target, sepc_};
}

// tvec 低两位不是地址；Vectored 偏移使用裸 cause，不包含 mcause 的 interrupt 最高位。
std::uint64_t CsrFile::trap_vector(
    PrivilegeMode target,
    bool interrupt,
    std::uint64_t cause) const noexcept {
    const auto vector = target == PrivilegeMode::Machine ? mtvec_ : stvec_;
    const auto base = vector & ~0x3ULL;
    const auto mode = vector & 0x3U;
    return interrupt && mode == 1U ? base + (4U * cause) : base;
}

// medeleg 位只对 M 以下来源生效；该入口是所有同步异常的唯一目标选择规则。
bool CsrFile::exception_delegated(
    PrivilegeMode source,
    ExceptionCause cause) const noexcept {
    if (source == PrivilegeMode::Machine) {
        return false;
    }
    return (medeleg_ & cause_bit(static_cast<std::uint64_t>(cause))) != 0U;
}

// 特权级数值不能直接用于 TSR 逻辑：TSR 只拦截恰好在 S-mode 执行的 SRET。
bool CsrFile::supervisor_return_allowed(PrivilegeMode current) const noexcept {
    if (privilege_rank(current) < privilege_rank(PrivilegeMode::Supervisor)) {
        return false;
    }
    return current != PrivilegeMode::Supervisor || (mstatus_ & bit(22U)) == 0U;
}

// U-mode WFI 是可选能力，本机器模型明确不实现；M-mode 不受 TW 限制。
bool CsrFile::wait_for_interrupt_allowed(PrivilegeMode current) const noexcept {
    if (current == PrivilegeMode::User) {
        return false;
    }
    return current == PrivilegeMode::Machine || (mstatus_ & bit(21U)) == 0U;
}

}  // namespace rvemu::core
