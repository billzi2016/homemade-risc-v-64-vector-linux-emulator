// 文件职责：实现 CPU 架构状态复位和寄存器访问，并在唯一写入口硬编码 x0 为零。
// 边界：本文件不执行指令，也不隐式改变 PC、特权级或其他寄存器。

#include "rvemu/core/cpu_state.hpp"

#include <algorithm>
#include <stdexcept>

namespace rvemu::core {
namespace {

void require_register_index(std::size_t index) {
    if (index >= CpuState::kRegisterCount) {
        throw std::out_of_range("RISC-V 寄存器索引必须位于 0..31");
    }
}

}  // namespace

void CpuState::reset(std::uint64_t reset_pc) noexcept {
    integer_registers_.fill(0U);
    floating_registers_.fill(0U);
    for (auto& register_value : vector_registers_) {
        register_value.fill(0U);
    }
    program_counter_ = reset_pc;
    privilege_ = PrivilegeMode::Machine;
}

std::uint64_t CpuState::integer(std::size_t index) const {
    require_register_index(index);
    return index == 0U ? 0U : integer_registers_[index];
}

void CpuState::set_integer(std::size_t index, std::uint64_t value) {
    require_register_index(index);
    if (index != 0U) {
        integer_registers_[index] = value;
    }
}

std::uint64_t CpuState::floating(std::size_t index) const {
    require_register_index(index);
    return floating_registers_[index];
}

void CpuState::set_floating(std::size_t index, std::uint64_t value) {
    require_register_index(index);
    floating_registers_[index] = value;
}

const CpuState::VectorRegister& CpuState::vector(std::size_t index) const {
    require_register_index(index);
    return vector_registers_[index];
}

void CpuState::set_vector(std::size_t index, const VectorRegister& value) {
    require_register_index(index);
    vector_registers_[index] = value;
}

}  // namespace rvemu::core
