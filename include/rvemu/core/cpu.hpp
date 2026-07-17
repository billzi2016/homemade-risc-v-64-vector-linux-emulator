// 文件职责：实现单 Hart RV64I 的精确取指、译码、执行和结构化同步异常结果。
// 边界：本节点不写 Trap CSR、不做 MMU 翻译，也不实现 M/A/F/D/C/V 扩展指令。

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
    std::uint8_t instruction_length{0U};
    std::optional<Trap> trap{};

    [[nodiscard]] static StepResult success(std::uint8_t length) noexcept {
        return StepResult{true, length, std::nullopt};
    }

    [[nodiscard]] static StepResult failure(Trap failure_trap, std::uint8_t length) {
        return StepResult{false, length, failure_trap};
    }
};

class Cpu final {
public:
    explicit Cpu(bus::Bus& bus) noexcept : bus_(bus) {}

    [[nodiscard]] CpuState& state() noexcept { return state_; }
    [[nodiscard]] const CpuState& state() const noexcept { return state_; }

    [[nodiscard]] StepResult step();

private:
    struct FetchResult final {
        std::optional<InstructionPacket> instruction{};
        std::optional<Trap> trap{};
    };

    [[nodiscard]] FetchResult fetch();
    [[nodiscard]] StepResult execute(const InstructionPacket& packet);
    [[nodiscard]] StepResult illegal(const InstructionPacket& packet) const;

    bus::Bus& bus_;
    CpuState state_{};
};

}  // namespace rvemu::core
