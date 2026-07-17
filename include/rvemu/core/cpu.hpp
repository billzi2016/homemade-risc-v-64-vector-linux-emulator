// 文件职责：实现单 Hart RV64I/Zicsr 取指执行、特权指令以及统一 Trap/中断入口。
// 边界：本节点不做 MMU 翻译，也不实现 M/A/F/D/C/V 扩展或具体中断设备。

#pragma once

#include "rvemu/bus/bus.hpp"
#include "rvemu/core/cpu_state.hpp"
#include "rvemu/core/instruction.hpp"
#include "rvemu/core/trap.hpp"

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
    explicit Cpu(bus::Bus& bus) noexcept : bus_(bus) {}

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

    // 先取低半字判断 16/32 位长度，再按真实总线边界取完整指令，失败时保存精确地址。
    [[nodiscard]] FetchResult fetch();
    // 分发非压缩指令并确保异常路径不提交 PC、目标寄存器或禁止的内存副作用。
    [[nodiscard]] StepResult execute(const InstructionPacket& packet);
    // 集中处理 ECALL/EBREAK/xRET/WFI 和六种 Zicsr 编码，避免 SYSTEM 语义散落到主 switch。
    [[nodiscard]] StepResult execute_system(
        const InstructionPacket& packet,
        const DecodedInstruction& instruction,
        std::uint64_t source1,
        std::uint64_t sequential_pc);
    // 构造统一非法指令结果，tval 使用原始指令编码且 PC 保持故障指令地址。
    [[nodiscard]] StepResult illegal(const InstructionPacket& packet) const;

    bus::Bus& bus_;
    CpuState state_{};
    bool suppress_cycle_increment_{false};
    bool suppress_instret_increment_{false};
};

}  // namespace rvemu::core
