// 文件职责：声明单 Hart RV64I/M/A/F/D/C/Zicsr 取指执行、特权指令以及统一 Trap/中断入口。
// 边界：本节点不做 MMU 翻译，也不实现 V 扩展或具体中断设备。

#pragma once

#include "rvemu/bus/bus.hpp"
#include "rvemu/core/cpu_state.hpp"
#include "rvemu/core/instruction.hpp"
#include "rvemu/core/trap.hpp"
#include "rvemu/memory/mmu.hpp"

#include <cstdint>
#include <optional>

namespace rvemu::core {

struct StepResult final {
    bool retired{false};
    bool stalled{false};
    std::uint8_t instruction_length{0U};
    std::optional<Trap> trap{};

    [[nodiscard]] static StepResult success(std::uint8_t length) noexcept {
        return StepResult{true, false, length, std::nullopt};
    }

    [[nodiscard]] static StepResult failure(Trap failure_trap, std::uint8_t length) {
        return StepResult{false, false, length, failure_trap};
    }

    [[nodiscard]] static StepResult wait() noexcept {
        return StepResult{false, true, 0U, std::nullopt};
    }
};

struct TrapDelivery final {
    bool interrupt{false};
    std::uint64_t cause{0U};
    PrivilegeMode source{PrivilegeMode::Machine};
    PrivilegeMode target{PrivilegeMode::Machine};
    std::uint64_t vector{0U};
};

class Cpu final {
public:
    explicit Cpu(bus::Bus& bus) noexcept : bus_(bus), mmu_(bus) {}

    // 暴露本 Hart 的唯一架构状态，平台与测试不得另建寄存器或 CSR 副本。
    [[nodiscard]] CpuState& state() noexcept { return state_; }
    [[nodiscard]] const CpuState& state() const noexcept { return state_; }

    // 完成一次取指与执行步进；同步异常精确返回给主循环，WFI 停顿以 stalled 明示。
    [[nodiscard]] StepResult step();
    // 将 step 返回的同步异常送入唯一委托/Trap 状态机，并返回实际目标和向量供诊断。
    [[nodiscard]] TrapDelivery take_trap(const Trap& trap) noexcept;
    // 从 mie/mip/委托/全局使能中选择并注入一个最高优先级中断；无可接收项返回空。
    [[nodiscard]] std::optional<TrapDelivery> take_pending_interrupt() noexcept;

private:
    struct FetchResult final {
        std::optional<InstructionPacket> instruction{};
        std::optional<Trap> trap{};
    };

    struct GuestAccessResult final {
        bus::AccessResult access{};
        std::optional<Trap> trap{};
        bus::ReservationToken token{};
    };

    [[nodiscard]] memory::MmuContext fetch_context() const noexcept;
    [[nodiscard]] memory::MmuContext data_context() const noexcept;
    [[nodiscard]] GuestAccessResult guest_read(
        const InstructionPacket& packet,
        std::uint64_t virtual_address,
        bus::AccessWidth width,
        memory::MmuAccessKind kind,
        bus::AccessType bus_type);
    [[nodiscard]] GuestAccessResult guest_write(
        const InstructionPacket& packet,
        std::uint64_t virtual_address,
        bus::AccessWidth width,
        std::uint64_t value,
        bus::AccessType bus_type);
    [[nodiscard]] GuestAccessResult guest_load_reserved(
        const InstructionPacket& packet,
        std::uint64_t virtual_address,
        bus::AccessWidth width);
    [[nodiscard]] GuestAccessResult guest_store_conditional(
        const InstructionPacket& packet,
        bus::ReservationToken token,
        std::uint64_t virtual_address,
        bus::AccessWidth width,
        std::uint64_t value);
    [[nodiscard]] GuestAccessResult guest_compare_exchange(
        const InstructionPacket& packet,
        std::uint64_t virtual_address,
        bus::AccessWidth width,
        std::uint64_t expected,
        std::uint64_t desired);

    // 先取低半字判断 16/32 位长度，再按真实总线边界取完整指令，失败时保存精确地址。
    [[nodiscard]] FetchResult fetch();
    // 识别原始指令长度，压缩编码先解压；所有异常路径都不得提交禁止的架构副作用。
    [[nodiscard]] StepResult execute(const InstructionPacket& packet);
    // 执行已确认的 32 位语义编码；packet.length 仍保留原始 2/4 字节长度用于 PC 和链接值。
    [[nodiscard]] StepResult execute_standard(const InstructionPacket& packet);
    // 集中处理 ECALL/EBREAK/xRET/WFI 和六种 Zicsr 编码，避免 SYSTEM 语义散落到主 switch。
    [[nodiscard]] StepResult execute_system(
        const InstructionPacket& packet,
        const DecodedInstruction& instruction,
        std::uint64_t source1,
        std::uint64_t sequential_pc);
    // 执行 OP-V 中仅负责配置的三种 vset*；未声明的 OP-V 编码必须统一返回非法指令。
    [[nodiscard]] StepResult execute_vector_configuration(
        const InstructionPacket& packet,
        const DecodedInstruction& instruction,
        std::uint64_t sequential_pc);
    // 集中处理 LOAD/STORE-FP、OP-FP 与 R4 FMA，校验完成后才提交浮点副作用。
    [[nodiscard]] StepResult execute_floating(
        const InstructionPacket& packet,
        const DecodedInstruction& instruction,
        std::uint64_t sequential_pc);
    // 构造统一非法指令结果，tval 使用原始指令编码且 PC 保持故障指令地址。
    [[nodiscard]] StepResult illegal(const InstructionPacket& packet) const;

    bus::Bus& bus_;
    memory::Mmu mmu_;
    CpuState state_{};
    bool suppress_cycle_increment_{false};
    bool suppress_instret_increment_{false};
};

}  // namespace rvemu::core
