// 文件职责：定义单 Hart 唯一 CSR 状态源、原子 CSR 访问以及 Trap 状态栈操作。
// 边界：CSR 文件不取指、不访问内存，也不直接修改 CPU 的 PC 或当前特权级。

#pragma once

#include "rvemu/core/trap.hpp"

#include <cstdint>
#include <optional>

namespace rvemu::core {

enum class CsrAddress : std::uint16_t {
    Fflags = 0x001U,
    Frm = 0x002U,
    Fcsr = 0x003U,
    Vstart = 0x008U,
    Vxsat = 0x009U,
    Vxrm = 0x00AU,
    Vcsr = 0x00FU,

    Sstatus = 0x100U,
    Sie = 0x104U,
    Stvec = 0x105U,
    Scounteren = 0x106U,
    Sscratch = 0x140U,
    Sepc = 0x141U,
    Scause = 0x142U,
    Stval = 0x143U,
    Sip = 0x144U,
    Satp = 0x180U,

    Cycle = 0xC00U,
    Time = 0xC01U,
    Instret = 0xC02U,
    Vl = 0xC20U,
    Vtype = 0xC21U,
    Vlenb = 0xC22U,

    Mstatus = 0x300U,
    Misa = 0x301U,
    Medeleg = 0x302U,
    Mideleg = 0x303U,
    Mie = 0x304U,
    Mtvec = 0x305U,
    Mcounteren = 0x306U,
    Mscratch = 0x340U,
    Mepc = 0x341U,
    Mcause = 0x342U,
    Mtval = 0x343U,
    Mip = 0x344U,
    Mcycle = 0xB00U,
    Minstret = 0xB02U,

    Mvendorid = 0xF11U,
    Marchid = 0xF12U,
    Mimpid = 0xF13U,
    Mhartid = 0xF14U,
};

enum class CsrModifyOperation : std::uint8_t {
    Replace,
    SetBits,
    ClearBits,
};

struct CsrAccessRequest final {
    CsrAddress address{CsrAddress::Mstatus};
    PrivilegeMode privilege{PrivilegeMode::Machine};
    bool read{true};
    bool write{false};
    CsrModifyOperation operation{CsrModifyOperation::Replace};
    std::uint64_t operand{0U};
};

struct CsrAccessResult final {
    bool success{false};
    std::uint64_t value{0U};
};

struct TrapReturnState final {
    PrivilegeMode privilege{PrivilegeMode::Machine};
    std::uint64_t program_counter{0U};
};

struct PendingInterrupt final {
    InterruptCause cause{InterruptCause::MachineExternal};
    PrivilegeMode target{PrivilegeMode::Machine};
};

class CsrFile final {
public:
    CsrFile() noexcept { reset(); }

    // 将所有 CSR 恢复为单 Hart 上电状态；实现能力、XLEN 和只读 ID 由读取逻辑派生。
    void reset() noexcept;

    // 原子执行一次来宾 CSR 访问。失败表示地址不存在、权限不足或非法写只读 CSR，且不留副作用。
    // 一个 CSR 指令只调用一次该入口，保证权限检查、旧值读取和条件写入不可分割。
    [[nodiscard]] CsrAccessResult access(const CsrAccessRequest& request) noexcept;

    // peek 只供 CPU 内部编排和白盒架构测试读取已知 CSR，不绕过来宾访问权限写状态。
    [[nodiscard]] std::uint64_t peek(CsrAddress address) const noexcept;

    // FS=Off 时浮点 CSR/指令均不可用；Initial/Clean/Dirty 都表示状态已启用。
    [[nodiscard]] bool floating_state_enabled() const noexcept;
    [[nodiscard]] std::uint8_t floating_rounding_mode() const noexcept;
    [[nodiscard]] std::uint8_t floating_exception_flags() const noexcept;
    // 浮点结果标志按 OR 累积且只保留 NV/DZ/OF/UF/NX；状态启用时同时标记 FS Dirty。
    void accrue_floating_exception_flags(std::uint8_t flags) noexcept;
    // 浮点寄存器或 CSR 发生架构写入时调用；FS=Off 不会被内部写入口偷偷启用。
    void mark_floating_state_dirty() noexcept;

    // VS=Off 时向量 CSR 与向量指令均不可用；Initial/Clean/Dirty 都代表可访问的上下文。
    [[nodiscard]] bool vector_state_enabled() const noexcept;
    // 向量寄存器、可写向量 CSR 或后续 vset* 成功改变状态时调用；VS=Off 不会被内部打开。
    void mark_vector_state_dirty() noexcept;

    // 将 CLINT/PLIC 等真实设备的电平状态投影到唯一 mip 底层值；清除不会影响其他中断源。
    void set_interrupt_pending(InterruptCause cause, bool pending) noexcept;
    // time 由未来 CLINT 的真实计时源更新；CSR 文件自身不创建第二套宿主时间逻辑。
    void set_time(std::uint64_t value) noexcept { time_ = value; }
    // CPU 每完成一次解释器硬件步进调用一次；64 位无符号溢出按架构位模式自然回绕。
    void increment_cycle() noexcept { ++cycle_; }
    // 只对真正退休的指令调用；异常或 WFI 停顿不得增加 instret。
    void increment_instret() noexcept { ++instret_; }
    // 判断 WFI 是否应被局部 enable+pending 唤醒；此判断刻意不考虑全局 xIE 与委托。
    [[nodiscard]] bool has_locally_enabled_interrupt() const noexcept;
    // 按规范固定优先级选择当前模式真正可接收的一个中断，同时返回其唯一目标特权级。
    [[nodiscard]] std::optional<PendingInterrupt> select_pending_interrupt(
        PrivilegeMode current_privilege) const noexcept;

    // 记录 M-mode Trap 的 epc/cause/tval，并把 MIE/来源模式压入 MPIE/MPP。
    void enter_machine_trap(
        PrivilegeMode previous_privilege,
        std::uint64_t program_counter,
        std::uint64_t cause,
        std::uint64_t trap_value) noexcept;
    // 记录 S-mode Trap 的对应状态；不得触碰同名机器级 Trap CSR。
    void enter_supervisor_trap(
        PrivilegeMode previous_privilege,
        std::uint64_t program_counter,
        std::uint64_t cause,
        std::uint64_t trap_value) noexcept;

    // 弹出 M-mode 中断/特权栈并返回 CPU 应恢复的模式和 mepc；同时执行 MPRV 清理规则。
    [[nodiscard]] TrapReturnState return_from_machine() noexcept;
    // 弹出 S-mode 中断/特权栈并返回 CPU 应恢复的模式和 sepc。
    [[nodiscard]] TrapReturnState return_from_supervisor() noexcept;
    // 根据 Direct/Vectored 模式计算入口；同步异常始终使用 BASE，只有中断增加 4*cause。
    [[nodiscard]] std::uint64_t trap_vector(
        PrivilegeMode target,
        bool interrupt,
        std::uint64_t cause) const noexcept;

    // 判断同步异常是否从 S/U 水平委托到 S；M-mode 来源永远不能向下降权。
    [[nodiscard]] bool exception_delegated(
        PrivilegeMode source,
        ExceptionCause cause) const noexcept;
    // 检查 SRET 的最低特权要求以及 S-mode 下 TSR 拦截；M-mode 不受 TSR 限制。
    [[nodiscard]] bool supervisor_return_allowed(PrivilegeMode current) const noexcept;
    // 本实现不开放 U-mode WFI；S-mode 还必须通过 TW 检查。
    [[nodiscard]] bool wait_for_interrupt_allowed(PrivilegeMode current) const noexcept;

private:
    // 仅承认本项目明确实现的 CSR 地址，防止保留地址意外形成可读写存储槽。
    [[nodiscard]] bool exists(CsrAddress address) const noexcept;
    // 集中实施地址编码权限、只读属性、counteren 链和 TVM，调用者不得复制这些判断。
    [[nodiscard]] bool access_allowed(
        CsrAddress address,
        PrivilegeMode privilege,
        bool write) const noexcept;
    // 生成架构可见值，包括 sstatus/sie/sip 别名、固定 misa 和派生 SD 位。
    [[nodiscard]] std::uint64_t read_value(CsrAddress address) const noexcept;
    // 应用 CSR 专属 WARL/写掩码；调用前必须已由 access_allowed 完成访问合法性检查。
    void write_value(CsrAddress address, std::uint64_t value) noexcept;

    std::uint64_t mstatus_{0U};
    std::uint8_t fcsr_{0U};
    std::uint64_t vstart_{0U};
    std::uint8_t vxsat_{0U};
    std::uint8_t vxrm_{0U};
    std::uint64_t vl_{0U};
    std::uint64_t vtype_{0U};
    std::uint64_t medeleg_{0U};
    std::uint64_t mideleg_{0U};
    std::uint64_t mie_{0U};
    std::uint64_t mtvec_{0U};
    std::uint64_t mscratch_{0U};
    std::uint64_t mepc_{0U};
    std::uint64_t mcause_{0U};
    std::uint64_t mtval_{0U};
    std::uint64_t mip_{0U};
    std::uint64_t mcounteren_{0U};

    std::uint64_t stvec_{0U};
    std::uint64_t sscratch_{0U};
    std::uint64_t sepc_{0U};
    std::uint64_t scause_{0U};
    std::uint64_t stval_{0U};
    std::uint64_t scounteren_{0U};
    std::uint64_t satp_{0U};
    std::uint64_t cycle_{0U};
    std::uint64_t time_{0U};
    std::uint64_t instret_{0U};
};

}  // namespace rvemu::core
