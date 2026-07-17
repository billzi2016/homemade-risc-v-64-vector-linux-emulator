// 文件职责：译码并执行 RV64F/D 2.2 指令，通过软件浮点核心和统一总线提交架构状态。
// 边界：本文件不实现浮点数值算法、不绕过 CpuState/CSR 写入口，也不处理压缩浮点指令。

#include "rvemu/core/cpu.hpp"

#include "rvemu/core/decoder.hpp"
#include "rvemu/core/soft_float.hpp"

#include <cstdint>
#include <optional>

namespace rvemu::core {
namespace {

[[nodiscard]] std::optional<FloatingFormat> decode_format(std::uint8_t encoded) noexcept {
    if (encoded == 0U) {
        return FloatingFormat::Single;
    }
    if (encoded == 1U) {
        return FloatingFormat::Double;
    }
    return std::nullopt;
}

[[nodiscard]] std::uint64_t read_computational_operand(
    const CpuState& state,
    FloatingFormat format,
    std::uint8_t index) {
    return format == FloatingFormat::Single ? state.floating_single(index) :
                                              state.floating(index);
}

void write_floating_result(
    CpuState& state,
    FloatingFormat format,
    std::uint8_t destination,
    const FloatingResult& result) {
    if (format == FloatingFormat::Single) {
        state.set_floating_single(destination, static_cast<std::uint32_t>(result.bits));
    } else {
        state.set_floating(destination, result.bits);
    }
    state.csrs().accrue_floating_exception_flags(result.flags);
}

[[nodiscard]] Trap access_trap(
    const InstructionPacket& packet,
    ExceptionCause cause,
    std::uint64_t address) noexcept {
    return Trap{cause, address, packet.program_counter, packet.bits};
}

}  // namespace

StepResult Cpu::execute_floating(
    const InstructionPacket& packet,
    const DecodedInstruction& instruction,
    std::uint64_t sequential_pc) {
    // FS 门控必须先于地址计算和总线访问，确保 Off 状态不留下访存或状态副作用。
    if (!state_.csrs().floating_state_enabled()) {
        return illegal(packet);
    }

    const auto retire = [this, &packet, sequential_pc]() {
        state_.set_program_counter(sequential_pc);
        return StepResult::success(packet.length);
    };
    const auto resolve_rounding = [this, &instruction]() {
        return resolve_floating_rounding_mode(
            instruction.function3, state_.csrs().floating_rounding_mode());
    };

    if (instruction.opcode == 0x07U) {  // FLW / FLD
        bus::AccessWidth width{};
        FloatingFormat format{};
        if (instruction.function3 == 0x2U) {
            width = bus::AccessWidth::Word;
            format = FloatingFormat::Single;
        } else if (instruction.function3 == 0x3U) {
            width = bus::AccessWidth::DoubleWord;
            format = FloatingFormat::Double;
        } else {
            return illegal(packet);
        }
        const auto address = state_.integer(instruction.source1) + immediate_i(packet.bits);
        const auto loaded = bus_.read(
            bus::PhysicalAddress{address}, width, bus::AccessType::Load);
        if (!loaded.ok()) {
            return StepResult::failure(
                access_trap(packet, ExceptionCause::LoadAccessFault, address), packet.length);
        }
        if (format == FloatingFormat::Single) {
            state_.set_floating_single(
                instruction.destination, static_cast<std::uint32_t>(loaded.value));
        } else {
            state_.set_floating(instruction.destination, loaded.value);
        }
        return retire();
    }

    if (instruction.opcode == 0x27U) {  // FSW / FSD
        bus::AccessWidth width{};
        if (instruction.function3 == 0x2U) {
            width = bus::AccessWidth::Word;
        } else if (instruction.function3 == 0x3U) {
            width = bus::AccessWidth::DoubleWord;
        } else {
            return illegal(packet);
        }
        const auto address = state_.integer(instruction.source1) + immediate_s(packet.bits);
        // FSW 是位模式传输，不执行 NaN-box 合法性检查；这与计算型单精度源不同。
        const auto stored = bus_.write(
            bus::PhysicalAddress{address},
            width,
            state_.floating(instruction.source2),
            bus::AccessType::Store);
        if (!stored.ok()) {
            return StepResult::failure(
                access_trap(packet, ExceptionCause::StoreAccessFault, address), packet.length);
        }
        return retire();
    }

    if (instruction.opcode == 0x43U || instruction.opcode == 0x47U ||
        instruction.opcode == 0x4BU || instruction.opcode == 0x4FU) {
        const auto format = decode_format(static_cast<std::uint8_t>((packet.bits >> 25U) & 0x3U));
        const auto rounding = resolve_rounding();
        if (!format.has_value() || !rounding.has_value()) {
            return illegal(packet);
        }
        const auto source3 = static_cast<std::uint8_t>((packet.bits >> 27U) & 0x1FU);
        const auto negate_product = instruction.opcode == 0x4BU || instruction.opcode == 0x4FU;
        const auto negate_addend = instruction.opcode == 0x47U || instruction.opcode == 0x4FU;
        const auto result = floating_fused_multiply_add(
            *format,
            read_computational_operand(state_, *format, instruction.source1),
            read_computational_operand(state_, *format, instruction.source2),
            read_computational_operand(state_, *format, source3),
            negate_product,
            negate_addend,
            *rounding);
        write_floating_result(state_, *format, instruction.destination, result);
        return retire();
    }

    if (instruction.opcode != 0x53U) {
        return illegal(packet);
    }

    const auto function5 = static_cast<std::uint8_t>(instruction.function7 >> 2U);
    const auto format = decode_format(static_cast<std::uint8_t>(instruction.function7 & 0x3U));

    // 基本算术的 funct5 共享格式低位，全部需要合法 rm。
    if (format.has_value() && function5 <= 0x03U) {
        const auto rounding = resolve_rounding();
        if (!rounding.has_value()) {
            return illegal(packet);
        }
        const auto lhs = read_computational_operand(state_, *format, instruction.source1);
        const auto rhs = read_computational_operand(state_, *format, instruction.source2);
        FloatingResult result{};
        if (function5 == 0U) {
            result = floating_add(*format, lhs, rhs, *rounding);
        } else if (function5 == 1U) {
            result = floating_subtract(*format, lhs, rhs, *rounding);
        } else if (function5 == 2U) {
            result = floating_multiply(*format, lhs, rhs, *rounding);
        } else {
            result = floating_divide(*format, lhs, rhs, *rounding);
        }
        write_floating_result(state_, *format, instruction.destination, result);
        return retire();
    }

    if (format.has_value() && function5 == 0x0BU) {  // FSQRT.S/D
        const auto rounding = resolve_rounding();
        if (instruction.source2 != 0U || !rounding.has_value()) {
            return illegal(packet);
        }
        const auto result = floating_square_root(
            *format,
            read_computational_operand(state_, *format, instruction.source1),
            *rounding);
        write_floating_result(state_, *format, instruction.destination, result);
        return retire();
    }

    if (format.has_value() && function5 == 0x04U) {  // FSGNJ[N/X].S/D
        if (instruction.function3 > 2U) {
            return illegal(packet);
        }
        const auto result = floating_sign_inject(
            *format,
            read_computational_operand(state_, *format, instruction.source1),
            read_computational_operand(state_, *format, instruction.source2),
            instruction.function3);
        write_floating_result(state_, *format, instruction.destination, FloatingResult{result, 0U});
        return retire();
    }

    if (format.has_value() && function5 == 0x05U) {  // FMIN/FMAX.S/D
        if (instruction.function3 > 1U) {
            return illegal(packet);
        }
        const auto result = floating_minimum_maximum(
            *format,
            read_computational_operand(state_, *format, instruction.source1),
            read_computational_operand(state_, *format, instruction.source2),
            instruction.function3 == 1U);
        write_floating_result(state_, *format, instruction.destination, result);
        return retire();
    }

    if (format.has_value() && function5 == 0x14U) {  // FLE/FLT/FEQ.S/D
        FloatingComparison comparison{};
        if (instruction.function3 == 0U) {
            comparison = FloatingComparison::LessThanOrEqual;
        } else if (instruction.function3 == 1U) {
            comparison = FloatingComparison::LessThan;
        } else if (instruction.function3 == 2U) {
            comparison = FloatingComparison::Equal;
        } else {
            return illegal(packet);
        }
        const auto result = floating_compare(
            *format,
            read_computational_operand(state_, *format, instruction.source1),
            read_computational_operand(state_, *format, instruction.source2),
            comparison);
        state_.set_integer(instruction.destination, result.value ? 1U : 0U);
        state_.csrs().accrue_floating_exception_flags(result.flags);
        return retire();
    }

    if (format.has_value() && function5 == 0x18U) {  // FCVT.W[U]/L[U].S/D
        const auto rounding = resolve_rounding();
        if (instruction.source2 > 3U || !rounding.has_value()) {
            return illegal(packet);
        }
        const auto integer_width = static_cast<std::uint8_t>(
            instruction.source2 < 2U ? 32U : 64U);
        const auto unsigned_integer = (instruction.source2 & 1U) != 0U;
        auto result = floating_to_integer(
            *format,
            read_computational_operand(state_, *format, instruction.source1),
            unsigned_integer,
            integer_width,
            *rounding);
        if (integer_width == 32U) {
            result.value = sign_extend(result.value & 0xFFFF'FFFFULL, 32U);
        }
        state_.set_integer(instruction.destination, result.value);
        state_.csrs().accrue_floating_exception_flags(result.flags);
        return retire();
    }

    if (format.has_value() && function5 == 0x1AU) {  // FCVT.S/D.W[U]/L[U]
        const auto rounding = resolve_rounding();
        if (instruction.source2 > 3U || !rounding.has_value()) {
            return illegal(packet);
        }
        const auto result = floating_from_integer(
            *format,
            state_.integer(instruction.source1),
            (instruction.source2 & 1U) != 0U,
            instruction.source2 < 2U ? 32U : 64U,
            *rounding);
        write_floating_result(state_, *format, instruction.destination, result);
        return retire();
    }

    if (format.has_value() && function5 == 0x1CU && instruction.source2 == 0U) {
        if (instruction.function3 == 1U) {  // FCLASS.S/D
            state_.set_integer(
                instruction.destination,
                floating_classify(
                    *format,
                    read_computational_operand(state_, *format, instruction.source1)));
            return retire();
        }
        if (instruction.function3 == 0U) {  // FMV.X.W / FMV.X.D
            const auto raw = state_.floating(instruction.source1);
            const auto value = *format == FloatingFormat::Single ?
                sign_extend(raw & 0xFFFF'FFFFULL, 32U) : raw;
            state_.set_integer(instruction.destination, value);
            return retire();
        }
        return illegal(packet);
    }

    if (format.has_value() && function5 == 0x1EU && instruction.source2 == 0U &&
        instruction.function3 == 0U) {  // FMV.W.X / FMV.D.X
        if (*format == FloatingFormat::Single) {
            state_.set_floating_single(
                instruction.destination,
                static_cast<std::uint32_t>(state_.integer(instruction.source1)));
        } else {
            state_.set_floating(instruction.destination, state_.integer(instruction.source1));
        }
        return retire();
    }

    // FCVT.S.D 与 FCVT.D.S 的 funct7 不按普通 fmt 解释，rs2 固定指定源格式。
    if ((instruction.function7 == 0x20U && instruction.source2 == 1U) ||
        (instruction.function7 == 0x21U && instruction.source2 == 0U)) {
        const auto rounding = resolve_rounding();
        if (!rounding.has_value()) {
            return illegal(packet);
        }
        const auto destination = instruction.function7 == 0x20U ?
            FloatingFormat::Single : FloatingFormat::Double;
        const auto source = destination == FloatingFormat::Single ?
            FloatingFormat::Double : FloatingFormat::Single;
        const auto result = floating_convert_format(
            destination,
            source,
            read_computational_operand(state_, source, instruction.source1),
            *rounding);
        write_floating_result(state_, destination, instruction.destination, result);
        return retire();
    }

    return illegal(packet);
}

}  // namespace rvemu::core
