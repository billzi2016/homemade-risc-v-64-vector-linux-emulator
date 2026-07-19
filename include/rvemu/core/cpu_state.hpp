// 文件职责：保存单 Hart 的整数、浮点、向量寄存器、PC 与当前特权级架构状态。
// 边界：状态对象不译码指令、不访问总线，也不实现浮点或向量运算语义。

#pragma once

#include "rvemu/bus/access.hpp"
#include "rvemu/core/csr.hpp"
#include "rvemu/core/floating_state.hpp"
#include "rvemu/vector/vector_register_group.hpp"
#include "rvemu/vector/vector_state.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace rvemu::core {

class CpuState final {
public:
    static constexpr std::size_t kRegisterCount = 32U;
    static constexpr std::size_t kVectorRegisterBytes = vector::VectorState::kRegisterBytes;
    using VectorRegister = vector::VectorState::Register;

    CpuState() = default;

    // 清空三类寄存器、CSR 与等待标志，并从指定 PC 以 M-mode 确定性启动单 Hart。
    void reset(std::uint64_t reset_pc = 0U) noexcept;

    // 三组寄存器访问器集中执行索引检查；整数写入口还永久丢弃对 x0 的写入。
    [[nodiscard]] std::uint64_t integer(std::size_t index) const;
    void set_integer(std::size_t index, std::uint64_t value);

    [[nodiscard]] std::uint64_t floating(std::size_t index) const;
    void set_floating(std::size_t index, std::uint64_t value);
    // 单精度入口统一实施 NaN boxing/unboxing；调用者不得手工复制上 32 位检查。
    [[nodiscard]] std::uint32_t floating_single(std::size_t index) const;
    void set_floating_single(std::size_t index, std::uint32_t value);

    // 向量寄存器由独立的 VectorState 统一保存；CPU 状态层只负责与本 Hart 的 VS Dirty 语义衔接。
    [[nodiscard]] const VectorRegister& vector(std::size_t index) const;
    void set_vector(std::size_t index, const VectorRegister& value);
    // 以下元素入口只接受已经完成寄存器组验证的布局对象；失败不改变状态，成功写入统一标记 VS Dirty。
    [[nodiscard]] std::optional<std::uint64_t> vector_element(
        const vector::VectorRegisterGroup& group,
        std::uint64_t element_index) const;
    [[nodiscard]] bool set_vector_element(
        const vector::VectorRegisterGroup& group,
        std::uint64_t element_index,
        std::uint64_t value);
    // 掩码总是从 v0 的位布局读取，不与当前 SEW/LMUL 或普通向量寄存器编号混淆。
    [[nodiscard]] std::optional<bool> vector_mask_bit(std::uint64_t element_index) const noexcept;

    // 以下简单访问器共同暴露本 Hart 的唯一 PC、特权级、CSR 和 WFI 状态，不维护影子副本。
    [[nodiscard]] std::uint64_t program_counter() const noexcept { return program_counter_; }
    void set_program_counter(std::uint64_t value) noexcept { program_counter_ = value; }

    [[nodiscard]] PrivilegeMode privilege() const noexcept { return privilege_; }
    void set_privilege(PrivilegeMode value) noexcept { privilege_ = value; }

    [[nodiscard]] CsrFile& csrs() noexcept { return csrs_; }
    [[nodiscard]] const CsrFile& csrs() const noexcept { return csrs_; }

    [[nodiscard]] bool waiting_for_interrupt() const noexcept { return waiting_for_interrupt_; }
    void set_waiting_for_interrupt(bool value) noexcept { waiting_for_interrupt_ = value; }

    // LR/SC token 是本 Hart 的不可见执行状态；复位清除，具体地址与失效判定仍由总线负责。
    [[nodiscard]] bus::ReservationToken reservation_token() const noexcept {
        return reservation_token_;
    }
    void set_reservation_token(bus::ReservationToken token) noexcept {
        reservation_token_ = token;
    }
    void clear_reservation_token() noexcept { reservation_token_ = bus::ReservationToken{}; }

private:
    std::array<std::uint64_t, kRegisterCount> integer_registers_{};
    std::array<std::uint64_t, kRegisterCount> floating_registers_{};
    vector::VectorState vector_state_{};
    CsrFile csrs_{};
    std::uint64_t program_counter_{0U};
    PrivilegeMode privilege_{PrivilegeMode::Machine};
    bool waiting_for_interrupt_{false};
    bus::ReservationToken reservation_token_{};
};

}  // namespace rvemu::core
