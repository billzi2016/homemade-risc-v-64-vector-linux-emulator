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

}  // namespace rvemu::vector
