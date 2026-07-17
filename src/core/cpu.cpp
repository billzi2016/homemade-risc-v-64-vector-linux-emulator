// 文件职责：实现单 Hart、非流水线 RV64I 的精确取指、完整基础整数执行和同步异常返回。
// 边界：本文件不实现 CSR/Trap 入口、MMU、M/A/F/D/C/V 扩展，也不绕过统一物理总线。

#include "rvemu/core/cpu.hpp"

#include "rvemu/core/decoder.hpp"

#include <cstdint>
#include <limits>
#include <optional>

namespace rvemu::core {
namespace {

[[nodiscard]] Trap make_trap(
    const InstructionPacket& packet,
    ExceptionCause cause,
    std::uint64_t value) noexcept {
    return Trap{cause, value, packet.program_counter, packet.bits};
}

[[nodiscard]] bool signed_less(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    constexpr std::uint64_t sign_bit = 1ULL << 63U;
    return (lhs ^ sign_bit) < (rhs ^ sign_bit);
}

[[nodiscard]] std::uint64_t arithmetic_shift_right64(
    std::uint64_t value,
    std::uint8_t amount) noexcept {
    if (amount == 0U) {
        return value;
    }
    auto result = value >> amount;
    if ((value & (1ULL << 63U)) != 0U) {
        result |= std::numeric_limits<std::uint64_t>::max() << (64U - amount);
    }
    return result;
}

[[nodiscard]] std::uint32_t arithmetic_shift_right32(
    std::uint32_t value,
    std::uint8_t amount) noexcept {
    if (amount == 0U) {
        return value;
    }
    auto result = static_cast<std::uint32_t>(value >> amount);
    if ((value & (1U << 31U)) != 0U) {
        result |= std::numeric_limits<std::uint32_t>::max() << (32U - amount);
    }
    return result;
}

[[nodiscard]] std::uint64_t sign_extend_word(std::uint64_t value) noexcept {
    return sign_extend(value & 0xFFFF'FFFFULL, 32U);
}

[[nodiscard]] ExceptionCause environment_call_cause(PrivilegeMode privilege) noexcept {
    switch (privilege) {
    case PrivilegeMode::User:
        return ExceptionCause::EnvironmentCallFromUser;
    case PrivilegeMode::Supervisor:
        return ExceptionCause::EnvironmentCallFromSupervisor;
    case PrivilegeMode::Machine:
        return ExceptionCause::EnvironmentCallFromMachine;
    }
    return ExceptionCause::IllegalInstruction;
}

}  // namespace

Cpu::FetchResult Cpu::fetch() {
    const auto pc = state_.program_counter();
    if ((pc & 0x1U) != 0U) {
        return FetchResult{
            std::nullopt,
            Trap{ExceptionCause::InstructionAddressMisaligned, pc, pc, 0U}};
    }

    const auto lower_result = bus_.read(
        bus::PhysicalAddress{pc},
        bus::AccessWidth::HalfWord,
        bus::AccessType::InstructionFetch);
    if (!lower_result.ok()) {
        return FetchResult{
            std::nullopt,
            Trap{ExceptionCause::InstructionAccessFault, pc, pc, 0U}};
    }

    const auto lower = static_cast<std::uint16_t>(lower_result.value & 0xFFFFU);
    if ((lower & 0x3U) != 0x3U) {
        return FetchResult{InstructionPacket{pc, lower, 2U}, std::nullopt};
    }

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (pc > maximum - 2U) {
        return FetchResult{
            std::nullopt,
            Trap{ExceptionCause::InstructionAccessFault, pc, pc, lower}};
    }

    const auto upper_address = pc + 2U;
    const auto upper_result = bus_.read(
        bus::PhysicalAddress{upper_address},
        bus::AccessWidth::HalfWord,
        bus::AccessType::InstructionFetch);
    if (!upper_result.ok()) {
        return FetchResult{
            std::nullopt,
            Trap{ExceptionCause::InstructionAccessFault, upper_address, pc, lower}};
    }

    const auto upper = static_cast<std::uint32_t>(upper_result.value & 0xFFFFU);
    const auto bits = static_cast<std::uint32_t>(lower) | (upper << 16U);
    return FetchResult{InstructionPacket{pc, bits, 4U}, std::nullopt};
}

StepResult Cpu::step() {
    const auto fetched = fetch();
    if (fetched.trap.has_value()) {
        return StepResult::failure(*fetched.trap, 0U);
    }
    return execute(*fetched.instruction);
}

StepResult Cpu::illegal(const InstructionPacket& packet) const {
    return StepResult::failure(
        make_trap(packet, ExceptionCause::IllegalInstruction, packet.bits),
        packet.length);
}

StepResult Cpu::execute(const InstructionPacket& packet) {
    if (packet.compressed()) {
        return illegal(packet);
    }

    const auto instruction = decode(packet.bits);
    const auto source1 = state_.integer(instruction.source1);
    const auto source2 = state_.integer(instruction.source2);
    const auto sequential_pc = packet.program_counter + packet.length;

    const auto retire = [this, &packet](std::uint64_t next_pc) {
        state_.set_program_counter(next_pc);
        return StepResult::success(packet.length);
    };
    const auto write_and_retire = [this, &packet](
                                      std::uint8_t destination,
                                      std::uint64_t value,
                                      std::uint64_t next_pc) {
        state_.set_integer(destination, value);
        state_.set_program_counter(next_pc);
        return StepResult::success(packet.length);
    };

    switch (instruction.opcode) {
    case 0x37U:  // LUI
        return write_and_retire(instruction.destination, immediate_u(packet.bits), sequential_pc);

    case 0x17U:  // AUIPC
        return write_and_retire(
            instruction.destination,
            packet.program_counter + immediate_u(packet.bits),
            sequential_pc);

    case 0x6FU: {  // JAL
        const auto target = packet.program_counter + immediate_j(packet.bits);
        if ((target & 0x1U) != 0U) {
            return StepResult::failure(
                make_trap(packet, ExceptionCause::InstructionAddressMisaligned, target),
                packet.length);
        }
        return write_and_retire(instruction.destination, sequential_pc, target);
    }

    case 0x67U: {  // JALR
        if (instruction.function3 != 0U) {
            return illegal(packet);
        }
        const auto target = (source1 + immediate_i(packet.bits)) & ~1ULL;
        if ((target & 0x1U) != 0U) {
            return StepResult::failure(
                make_trap(packet, ExceptionCause::InstructionAddressMisaligned, target),
                packet.length);
        }
        return write_and_retire(instruction.destination, sequential_pc, target);
    }

    case 0x63U: {  // 条件分支
        bool taken = false;
        switch (instruction.function3) {
        case 0x0U:
            taken = source1 == source2;
            break;
        case 0x1U:
            taken = source1 != source2;
            break;
        case 0x4U:
            taken = signed_less(source1, source2);
            break;
        case 0x5U:
            taken = !signed_less(source1, source2);
            break;
        case 0x6U:
            taken = source1 < source2;
            break;
        case 0x7U:
            taken = source1 >= source2;
            break;
        default:
            return illegal(packet);
        }

        if (!taken) {
            return retire(sequential_pc);
        }
        const auto target = packet.program_counter + immediate_b(packet.bits);
        if ((target & 0x1U) != 0U) {
            return StepResult::failure(
                make_trap(packet, ExceptionCause::InstructionAddressMisaligned, target),
                packet.length);
        }
        return retire(target);
    }

    case 0x03U: {  // 标量加载
        bus::AccessWidth width = bus::AccessWidth::Byte;
        bool sign_result = true;
        std::uint8_t sign_width = 8U;
        switch (instruction.function3) {
        case 0x0U:  // LB
            break;
        case 0x1U:  // LH
            width = bus::AccessWidth::HalfWord;
            sign_width = 16U;
            break;
        case 0x2U:  // LW
            width = bus::AccessWidth::Word;
            sign_width = 32U;
            break;
        case 0x3U:  // LD
            width = bus::AccessWidth::DoubleWord;
            sign_width = 64U;
            break;
        case 0x4U:  // LBU
            sign_result = false;
            break;
        case 0x5U:  // LHU
            width = bus::AccessWidth::HalfWord;
            sign_result = false;
            break;
        case 0x6U:  // LWU
            width = bus::AccessWidth::Word;
            sign_result = false;
            break;
        default:
            return illegal(packet);
        }

        const auto address = source1 + immediate_i(packet.bits);
        const auto loaded = bus_.read(
            bus::PhysicalAddress{address},
            width,
            bus::AccessType::Load);
        if (!loaded.ok()) {
            return StepResult::failure(
                make_trap(packet, ExceptionCause::LoadAccessFault, address),
                packet.length);
        }
        const auto value = sign_result ? sign_extend(loaded.value, sign_width) : loaded.value;
        return write_and_retire(instruction.destination, value, sequential_pc);
    }

    case 0x23U: {  // 标量存储
        bus::AccessWidth width = bus::AccessWidth::Byte;
        switch (instruction.function3) {
        case 0x0U:
            break;
        case 0x1U:
            width = bus::AccessWidth::HalfWord;
            break;
        case 0x2U:
            width = bus::AccessWidth::Word;
            break;
        case 0x3U:
            width = bus::AccessWidth::DoubleWord;
            break;
        default:
            return illegal(packet);
        }

        const auto address = source1 + immediate_s(packet.bits);
        const auto stored = bus_.write(
            bus::PhysicalAddress{address},
            width,
            source2,
            bus::AccessType::Store);
        if (!stored.ok()) {
            return StepResult::failure(
                make_trap(packet, ExceptionCause::StoreAccessFault, address),
                packet.length);
        }
        return retire(sequential_pc);
    }

    case 0x13U: {  // OP-IMM
        const auto immediate = immediate_i(packet.bits);
        std::uint64_t result = 0U;
        switch (instruction.function3) {
        case 0x0U:
            result = source1 + immediate;
            break;
        case 0x2U:
            result = signed_less(source1, immediate) ? 1U : 0U;
            break;
        case 0x3U:
            result = source1 < immediate ? 1U : 0U;
            break;
        case 0x4U:
            result = source1 ^ immediate;
            break;
        case 0x6U:
            result = source1 | immediate;
            break;
        case 0x7U:
            result = source1 & immediate;
            break;
        case 0x1U: {
            const auto function6 = static_cast<std::uint8_t>((packet.bits >> 26U) & 0x3FU);
            if (function6 != 0U) {
                return illegal(packet);
            }
            const auto amount = static_cast<std::uint8_t>((packet.bits >> 20U) & 0x3FU);
            result = source1 << amount;
            break;
        }
        case 0x5U: {
            const auto function6 = static_cast<std::uint8_t>((packet.bits >> 26U) & 0x3FU);
            const auto amount = static_cast<std::uint8_t>((packet.bits >> 20U) & 0x3FU);
            if (function6 == 0U) {
                result = source1 >> amount;
            } else if (function6 == 0x10U) {
                result = arithmetic_shift_right64(source1, amount);
            } else {
                return illegal(packet);
            }
            break;
        }
        default:
            return illegal(packet);
        }
        return write_and_retire(instruction.destination, result, sequential_pc);
    }

    case 0x1BU: {  // OP-IMM-32
        std::uint32_t result = 0U;
        const auto source_word = static_cast<std::uint32_t>(source1 & 0xFFFF'FFFFULL);
        switch (instruction.function3) {
        case 0x0U:
            result = source_word + static_cast<std::uint32_t>(immediate_i(packet.bits));
            break;
        case 0x1U: {
            if (instruction.function7 != 0U) {
                return illegal(packet);
            }
            const auto amount = static_cast<std::uint8_t>((packet.bits >> 20U) & 0x1FU);
            result = static_cast<std::uint32_t>(source_word << amount);
            break;
        }
        case 0x5U: {
            const auto amount = static_cast<std::uint8_t>((packet.bits >> 20U) & 0x1FU);
            if (instruction.function7 == 0U) {
                result = source_word >> amount;
            } else if (instruction.function7 == 0x20U) {
                result = arithmetic_shift_right32(source_word, amount);
            } else {
                return illegal(packet);
            }
            break;
        }
        default:
            return illegal(packet);
        }
        return write_and_retire(instruction.destination, sign_extend_word(result), sequential_pc);
    }

    case 0x33U: {  // OP
        std::uint64_t result = 0U;
        if (instruction.function7 == 0U) {
            switch (instruction.function3) {
            case 0x0U:
                result = source1 + source2;
                break;
            case 0x1U:
                result = source1 << (source2 & 0x3FU);
                break;
            case 0x2U:
                result = signed_less(source1, source2) ? 1U : 0U;
                break;
            case 0x3U:
                result = source1 < source2 ? 1U : 0U;
                break;
            case 0x4U:
                result = source1 ^ source2;
                break;
            case 0x5U:
                result = source1 >> (source2 & 0x3FU);
                break;
            case 0x6U:
                result = source1 | source2;
                break;
            case 0x7U:
                result = source1 & source2;
                break;
            default:
                return illegal(packet);
            }
        } else if (instruction.function7 == 0x20U && instruction.function3 == 0x0U) {
            result = source1 - source2;
        } else if (instruction.function7 == 0x20U && instruction.function3 == 0x5U) {
            result = arithmetic_shift_right64(source1, static_cast<std::uint8_t>(source2 & 0x3FU));
        } else {
            return illegal(packet);
        }
        return write_and_retire(instruction.destination, result, sequential_pc);
    }

    case 0x3BU: {  // OP-32
        const auto lhs = static_cast<std::uint32_t>(source1 & 0xFFFF'FFFFULL);
        const auto rhs = static_cast<std::uint32_t>(source2 & 0xFFFF'FFFFULL);
        std::uint32_t result = 0U;
        if (instruction.function7 == 0U && instruction.function3 == 0x0U) {
            result = lhs + rhs;
        } else if (instruction.function7 == 0x20U && instruction.function3 == 0x0U) {
            result = lhs - rhs;
        } else if (instruction.function7 == 0U && instruction.function3 == 0x1U) {
            result = static_cast<std::uint32_t>(lhs << (rhs & 0x1FU));
        } else if (instruction.function7 == 0U && instruction.function3 == 0x5U) {
            result = lhs >> (rhs & 0x1FU);
        } else if (instruction.function7 == 0x20U && instruction.function3 == 0x5U) {
            result = arithmetic_shift_right32(lhs, static_cast<std::uint8_t>(rhs & 0x1FU));
        } else {
            return illegal(packet);
        }
        return write_and_retire(instruction.destination, sign_extend_word(result), sequential_pc);
    }

    case 0x0FU:  // FENCE / FENCE.I
        if (instruction.destination != 0U || instruction.source1 != 0U) {
            return illegal(packet);
        }
        if (instruction.function3 == 0U) {
            // 单 Hart、无缓存模型中，所有先前总线事务已同步提交，因此 FENCE 无额外状态。
            return retire(sequential_pc);
        }
        if (instruction.function3 == 1U && (packet.bits >> 20U) == 0U) {
            // 当前解释器没有指令缓存，FENCE.I 的同步要求天然满足。
            return retire(sequential_pc);
        }
        return illegal(packet);

    case 0x73U:  // SYSTEM 中属于基础 ISA 的 ECALL/EBREAK
        if (packet.bits == 0x0000'0073U) {
            return StepResult::failure(
                make_trap(packet, environment_call_cause(state_.privilege()), 0U),
                packet.length);
        }
        if (packet.bits == 0x0010'0073U) {
            return StepResult::failure(
                make_trap(packet, ExceptionCause::Breakpoint, 0U),
                packet.length);
        }
        return illegal(packet);

    default:
        return illegal(packet);
    }
}

}  // namespace rvemu::core
