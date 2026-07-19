// 文件职责：实现 CPU 架构状态复位和寄存器访问，并在唯一写入口硬编码 x0 为零。
// 边界：本文件不执行指令，也不隐式改变 PC、特权级或其他寄存器。

#include "rvemu/core/cpu_state.hpp"

#include <algorithm>
#include <stdexcept>

namespace rvemu::core {
namespace {

// 所有寄存器族共享同一索引约束；越界属于模拟器调用缺陷，而不是来宾 Trap。
void require_register_index(std::size_t index) {
    if (index >= CpuState::kRegisterCount) {
        throw std::out_of_range("RISC-V 寄存器索引必须位于 0..31");
    }
}

}  // namespace

// 复位不保留前一次运行的任何架构状态，确保同一对象可以重复执行确定性启动测试。
void CpuState::reset(std::uint64_t reset_pc) noexcept {
    integer_registers_.fill(0U);
    floating_registers_.fill(0U);
    vector_state_.reset();
    csrs_.reset();
    program_counter_ = reset_pc;
    privilege_ = PrivilegeMode::Machine;
    waiting_for_interrupt_ = false;
    reservation_token_ = bus::ReservationToken{};
}

// x0 即使底层数组被未来诊断设施触碰，架构读取仍强制返回零。
std::uint64_t CpuState::integer(std::size_t index) const {
    require_register_index(index);
    return index == 0U ? 0U : integer_registers_[index];
}

// x0 写保护只存在这一处，所有指令执行路径都必须通过该入口提交整数结果。
void CpuState::set_integer(std::size_t index, std::uint64_t value) {
    require_register_index(index);
    if (index != 0U) {
        integer_registers_[index] = value;
    }
}

// 浮点寄存器保存无类型 64 位位模式，F/D 解释与 NaN boxing 不属于状态层职责。
std::uint64_t CpuState::floating(std::size_t index) const {
    require_register_index(index);
    return floating_registers_[index];
}

// 64 位写入保持完整位模式；若软件已通过 FS 启用浮点上下文，则唯一入口标记 Dirty。
void CpuState::set_floating(std::size_t index, std::uint64_t value) {
    require_register_index(index);
    floating_registers_[index] = value;
    csrs_.mark_floating_state_dirty();
}

// 非法 NaN box 在作为单精度源读取时统一变为 canonical qNaN，不污染原寄存器位模式。
std::uint32_t CpuState::floating_single(std::size_t index) const {
    return unbox_single_precision(floating(index));
}

// 所有单精度结果都经同一入口补齐上 32 位一，随后复用 64 位写入口更新 FS。
void CpuState::set_floating_single(std::size_t index, std::uint32_t value) {
    set_floating(index, box_single_precision(value));
}

// 返回固定 32 字节寄存器的只读引用，观察路径不会复制或改变 VLEN。
const CpuState::VectorRegister& CpuState::vector(std::size_t index) const {
    return vector_state_.register_value(index);
}

// 整体提交一个 256 位寄存器；逐元素异常提交策略由后续 RVV 执行器负责。只有已经启用的
// 向量上下文会变为 Dirty，避免测试或内部复位路径在 VS=Off 时偷偷打开来宾扩展状态。
void CpuState::set_vector(std::size_t index, const VectorRegister& value) {
    vector_state_.set_register_value(index, value);
    csrs_.mark_vector_state_dirty();
}

std::optional<std::uint64_t> CpuState::vector_element(
    const vector::VectorRegisterGroup& group,
    std::uint64_t element_index) const {
    return group.read_element(vector_state_, element_index);
}

bool CpuState::set_vector_element(
    const vector::VectorRegisterGroup& group,
    std::uint64_t element_index,
    std::uint64_t value) {
    if (!group.write_element(vector_state_, element_index, value)) {
        return false;
    }
    csrs_.mark_vector_state_dirty();
    return true;
}

std::optional<bool> CpuState::vector_mask_bit(std::uint64_t element_index) const noexcept {
    return vector::VectorRegisterGroup::mask_bit(vector_state_, element_index);
}

}  // namespace rvemu::core
