// 文件职责：实现单 Hart、非流水线 RV64I/M/A/Zicsr 执行、特权指令和统一 Trap/中断入口。
// 边界：本文件不实现 MMU、F/D/C/V 扩展或具体设备，也不绕过统一物理总线。

#include "rvemu/core/cpu.hpp"

#include "rvemu/core/decoder.hpp"
#include "rvemu/core/integer_a.hpp"
#include "rvemu/core/integer_m.hpp"

#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>

namespace rvemu::core {
namespace {

constexpr std::uint64_t kInterruptFlag = 1ULL << 63U;

// 把执行器发现的同步错误绑定原始指令 PC/编码，Trap 入口不再猜测故障现场。
[[nodiscard]] Trap make_trap(
    const InstructionPacket& packet,
    ExceptionCause cause,
    std::uint64_t value) noexcept {
    return Trap{cause, value, packet.program_counter, packet.bits};
}

// 翻转符号位可把二补码有符号序映射为无符号序，避免宿主有符号转换差异。
[[nodiscard]] bool signed_less(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    constexpr std::uint64_t sign_bit = 1ULL << 63U;
    return (lhs ^ sign_bit) < (rhs ^ sign_bit);
}

// 显式补符号位实现 64 位算术右移，不依赖 C++ 对负数右移的实现定义行为。
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

// 32 位版本在无符号域完成移位和补位，避免整数提升引入宿主相关语义。
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

// RV64 W 类结果必须先截为 32 位，再符号扩展为完整 XLEN。
[[nodiscard]] std::uint64_t sign_extend_word(std::uint64_t value) noexcept {
    return sign_extend(value & 0xFFFF'FFFFULL, 32U);
}

// aq/rl 描述同一地址域内的顺序。同步总线已经串行提交事务，宿主栅栏再阻止编译器或
// 异步设备线程把共享状态访问跨越原子指令，明确落实而不是静默丢弃编码位。
void apply_release_ordering(bool release) noexcept {
    if (release) {
        std::atomic_thread_fence(std::memory_order_release);
    }
}

void apply_acquire_ordering(bool acquire) noexcept {
    if (acquire) {
        std::atomic_thread_fence(std::memory_order_acquire);
    }
}

// ECALL cause 只取决于发起模式；集中映射避免 SYSTEM 分支散落原因常量。
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
    suppress_cycle_increment_ = false;
    suppress_instret_increment_ = false;

    if (state_.waiting_for_interrupt()) {
        if (!state_.csrs().has_locally_enabled_interrupt()) {
            state_.csrs().increment_cycle();
            return StepResult::wait();
        }
        // WFI 的唤醒只依赖局部 enable；全局 enable 和委托决定是否真正进入 Trap。
        state_.set_waiting_for_interrupt(false);
    }

    const auto fetched = fetch();
    if (fetched.trap.has_value()) {
        state_.csrs().increment_cycle();
        return StepResult::failure(*fetched.trap, 0U);
    }
    const auto result = execute(*fetched.instruction);
    if (!suppress_cycle_increment_) {
        state_.csrs().increment_cycle();
    }
    if (result.retired && !suppress_instret_increment_) {
        state_.csrs().increment_instret();
    }
    return result;
}

TrapDelivery Cpu::take_trap(const Trap& trap) noexcept {
    const auto source = state_.privilege();
    const auto cause = static_cast<std::uint64_t>(trap.cause);
    const auto target = state_.csrs().exception_delegated(source, trap.cause) ?
                            PrivilegeMode::Supervisor : PrivilegeMode::Machine;

    if (target == PrivilegeMode::Supervisor) {
        state_.csrs().enter_supervisor_trap(source, trap.program_counter, cause, trap.value);
    } else {
        state_.csrs().enter_machine_trap(source, trap.program_counter, cause, trap.value);
    }

    const auto vector = state_.csrs().trap_vector(target, false, cause);
    state_.set_privilege(target);
    state_.set_program_counter(vector);
    state_.set_waiting_for_interrupt(false);
    return TrapDelivery{false, cause, source, target, vector};
}

// pending 位在 Trap 入口不会自动清除：电平源必须由 CLINT/PLIC 或软件完成寄存器清除。
std::optional<TrapDelivery> Cpu::take_pending_interrupt() noexcept {
    const auto source = state_.privilege();
    const auto pending = state_.csrs().select_pending_interrupt(source);
    if (!pending.has_value()) {
        return std::nullopt;
    }

    const auto cause = static_cast<std::uint64_t>(pending->cause);
    const auto encoded_cause = kInterruptFlag | cause;
    const auto interrupted_pc = state_.program_counter();
    if (pending->target == PrivilegeMode::Supervisor) {
        state_.csrs().enter_supervisor_trap(source, interrupted_pc, encoded_cause, 0U);
    } else {
        state_.csrs().enter_machine_trap(source, interrupted_pc, encoded_cause, 0U);
    }

    const auto vector = state_.csrs().trap_vector(pending->target, true, cause);
    state_.set_privilege(pending->target);
    state_.set_program_counter(vector);
    state_.set_waiting_for_interrupt(false);
    return TrapDelivery{true, cause, source, pending->target, vector};
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
        if (instruction.function7 == 0x01U) {
            const auto multiplied_or_divided = execute_integer_m(
                instruction.function3, source1, source2, false);
            if (!multiplied_or_divided.has_value()) {
                return illegal(packet);
            }
            result = *multiplied_or_divided;
        } else if (instruction.function7 == 0U) {
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
        if (instruction.function7 == 0x01U) {
            const auto multiplied_or_divided = execute_integer_m(
                instruction.function3, source1, source2, true);
            if (!multiplied_or_divided.has_value()) {
                return illegal(packet);
            }
            return write_and_retire(
                instruction.destination, *multiplied_or_divided, sequential_pc);
        }
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

    case 0x2FU: {  // RV64A：LR/SC 与 AMO
        const auto word_operation = instruction.function3 == 0x2U;
        if (!word_operation && instruction.function3 != 0x3U) {
            return illegal(packet);
        }

        const auto width = word_operation ? bus::AccessWidth::Word : bus::AccessWidth::DoubleWord;
        const auto width_bytes = bus::width_in_bytes(width);
        const auto address = source1;
        const auto function5 = static_cast<std::uint8_t>((packet.bits >> 27U) & 0x1FU);
        const auto acquire = ((packet.bits >> 26U) & 0x1U) != 0U;
        const auto release = ((packet.bits >> 25U) & 0x1U) != 0U;

        if (function5 == 0x02U) {  // LR.W / LR.D
            if (instruction.source2 != 0U) {
                return illegal(packet);
            }
            if ((address & (width_bytes - 1U)) != 0U) {
                return StepResult::failure(
                    make_trap(packet, ExceptionCause::LoadAddressMisaligned, address),
                    packet.length);
            }

            apply_release_ordering(release);
            const auto loaded = bus_.load_reserved(bus::PhysicalAddress{address}, width);
            if (!loaded.access.ok()) {
                return StepResult::failure(
                    make_trap(packet, ExceptionCause::LoadAccessFault, address),
                    packet.length);
            }
            state_.set_reservation_token(loaded.token);
            apply_acquire_ordering(acquire);
            const auto value = word_operation ? sign_extend_word(loaded.access.value) :
                                                loaded.access.value;
            return write_and_retire(instruction.destination, value, sequential_pc);
        }

        if (function5 == 0x03U) {  // SC.W / SC.D
            if ((address & (width_bytes - 1U)) != 0U) {
                state_.clear_reservation_token();
                return StepResult::failure(
                    make_trap(packet, ExceptionCause::StoreAddressMisaligned, address),
                    packet.length);
            }

            apply_release_ordering(release);
            const auto token = state_.reservation_token();
            state_.clear_reservation_token();
            const auto stored = bus_.store_conditional(
                token, bus::PhysicalAddress{address}, width, source2);
            if (!stored.ok()) {
                return StepResult::failure(
                    make_trap(packet, ExceptionCause::StoreAccessFault, address),
                    packet.length);
            }
            apply_acquire_ordering(acquire);
            return write_and_retire(
                instruction.destination, stored.exchanged ? 0U : 1U, sequential_pc);
        }

        const auto operation = decode_atomic_operation(function5);
        if (!operation.has_value()) {
            return illegal(packet);
        }
        if ((address & (width_bytes - 1U)) != 0U) {
            return StepResult::failure(
                make_trap(packet, ExceptionCause::StoreAddressMisaligned, address),
                packet.length);
        }

        apply_release_ordering(release);
        auto observed = bus_.read(bus::PhysicalAddress{address}, width, bus::AccessType::Atomic);
        if (!observed.ok()) {
            return StepResult::failure(
                make_trap(packet, ExceptionCause::StoreAccessFault, address),
                packet.length);
        }

        // CAS 失败说明另一个总线主设备已更新内存；使用它返回的新观察值重算，直至
        // 某次比较与写入在 RAM 锁内原子提交，期间不会把过期结果写回。
        std::uint64_t original = observed.value;
        for (;;) {
            const auto desired = execute_atomic_operation(
                *operation, original, source2, word_operation);
            const auto exchanged = bus_.compare_exchange(
                bus::PhysicalAddress{address}, width, original, desired);
            if (!exchanged.ok()) {
                return StepResult::failure(
                    make_trap(packet, ExceptionCause::StoreAccessFault, address),
                    packet.length);
            }
            if (exchanged.exchanged) {
                break;
            }
            original = exchanged.value;
        }

        apply_acquire_ordering(acquire);
        const auto result = word_operation ? sign_extend_word(original) : original;
        return write_and_retire(instruction.destination, result, sequential_pc);
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

    case 0x73U:
        return execute_system(packet, instruction, source1, sequential_pc);

    default:
        return illegal(packet);
    }
}

StepResult Cpu::execute_system(
    const InstructionPacket& packet,
    const DecodedInstruction& instruction,
    std::uint64_t source1,
    std::uint64_t sequential_pc) {
    // funct3=0 使用完整机器码区分特权指令；其余 funct3 才解释为 Zicsr 操作。
    if (instruction.function3 == 0U) {
        if (packet.bits == 0x0000'0073U) {  // ECALL
            return StepResult::failure(
                make_trap(packet, environment_call_cause(state_.privilege()), 0U),
                packet.length);
        }
        if (packet.bits == 0x0010'0073U) {  // EBREAK
            return StepResult::failure(
                make_trap(packet, ExceptionCause::Breakpoint, 0U),
                packet.length);
        }
        if (packet.bits == 0x3020'0073U) {  // MRET
            if (state_.privilege() != PrivilegeMode::Machine) {
                return illegal(packet);
            }
            const auto returned = state_.csrs().return_from_machine();
            state_.set_privilege(returned.privilege);
            state_.set_program_counter(returned.program_counter);
            state_.set_waiting_for_interrupt(false);
            return StepResult::success(packet.length);
        }
        if (packet.bits == 0x1020'0073U) {  // SRET
            if (!state_.csrs().supervisor_return_allowed(state_.privilege())) {
                return illegal(packet);
            }
            const auto returned = state_.csrs().return_from_supervisor();
            state_.set_privilege(returned.privilege);
            state_.set_program_counter(returned.program_counter);
            state_.set_waiting_for_interrupt(false);
            return StepResult::success(packet.length);
        }
        if (packet.bits == 0x1050'0073U) {  // WFI
            if (!state_.csrs().wait_for_interrupt_allowed(state_.privilege())) {
                return illegal(packet);
            }
            state_.set_program_counter(sequential_pc);
            state_.set_waiting_for_interrupt(true);
            return StepResult::success(packet.length);
        }
        return illegal(packet);
    }

    CsrModifyOperation operation = CsrModifyOperation::Replace;
    bool immediate = false;
    switch (instruction.function3) {
    case 0x1U:  // CSRRW
        break;
    case 0x2U:  // CSRRS
        operation = CsrModifyOperation::SetBits;
        break;
    case 0x3U:  // CSRRC
        operation = CsrModifyOperation::ClearBits;
        break;
    case 0x5U:  // CSRRWI
        immediate = true;
        break;
    case 0x6U:  // CSRRSI
        immediate = true;
        operation = CsrModifyOperation::SetBits;
        break;
    case 0x7U:  // CSRRCI
        immediate = true;
        operation = CsrModifyOperation::ClearBits;
        break;
    default:
        return illegal(packet);
    }

    // CSRRS/CSRRC 是否产生写访问由 rs1 字段是否为 x0 决定，而不是寄存器运行值是否为零。
    const auto source_field_is_zero = instruction.source1 == 0U;
    const auto replace = operation == CsrModifyOperation::Replace;
    const auto read = !replace || instruction.destination != 0U;
    const auto write = replace || !source_field_is_zero;
    const auto operand = immediate ? static_cast<std::uint64_t>(instruction.source1) : source1;
    const auto address = static_cast<CsrAddress>((packet.bits >> 20U) & 0xFFFU);
    const auto accessed = state_.csrs().access(CsrAccessRequest{
        address,
        state_.privilege(),
        read,
        write,
        operation,
        operand,
    });
    if (!accessed.success) {
        return illegal(packet);
    }

    // 规范要求显式计数器写替代该指令本身的隐式更新，不能先写再额外加一。
    if (write && address == CsrAddress::Mcycle) {
        suppress_cycle_increment_ = true;
    }
    if (write && address == CsrAddress::Minstret) {
        suppress_instret_increment_ = true;
    }

    state_.set_integer(instruction.destination, accessed.value);
    state_.set_program_counter(sequential_pc);
    return StepResult::success(packet.length);
}

}  // namespace rvemu::core
