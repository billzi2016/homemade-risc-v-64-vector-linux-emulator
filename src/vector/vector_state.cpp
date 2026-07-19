// 文件职责：实现固定 VLEN 的 RVV 向量寄存器复位与受边界保护的读写。
// 边界：本文件不解释元素、LMUL、vtype 或掩码，不能绕过 CPU 状态层的 VS Dirty 提交规则。

#include "rvemu/vector/vector_state.hpp"

#include <stdexcept>

namespace rvemu::vector {
namespace {

// 寄存器编号来自已译码指令字段；越界只能是模拟器内部调用错误，不能被静默折叠到 v0。
void require_register_index(std::size_t index) {
    if (index >= VectorState::kRegisterCount) {
        throw std::out_of_range("RVV 向量寄存器索引必须位于 0..31");
    }
}

// VLEN 内偏移必须独立检查，不能让数组下标越界把寄存器组错误伪装成宿主未定义行为。
void require_byte_index(std::size_t index) {
    if (index >= VectorState::kRegisterBytes) {
        throw std::out_of_range("RVV 向量寄存器字节索引必须位于 0..31");
    }
}

}  // namespace

void VectorState::reset() noexcept {
    for (auto& register_value : registers_) {
        register_value.fill(0U);
    }
}

const VectorState::Register& VectorState::register_value(std::size_t index) const {
    require_register_index(index);
    return registers_[index];
}

void VectorState::set_register_value(std::size_t index, const Register& value) {
    require_register_index(index);
    registers_[index] = value;
}

std::uint8_t VectorState::byte_value(std::size_t register_index, std::size_t byte_index) const {
    require_register_index(register_index);
    require_byte_index(byte_index);
    return registers_[register_index][byte_index];
}

void VectorState::set_byte_value(std::size_t register_index,
                                 std::size_t byte_index,
                                 std::uint8_t value) {
    require_register_index(register_index);
    require_byte_index(byte_index);
    registers_[register_index][byte_index] = value;
}

}  // namespace rvemu::vector
