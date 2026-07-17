// 文件职责：保存单 Hart 的整数、浮点、向量寄存器、PC 与当前特权级架构状态。
// 边界：状态对象不译码指令、不访问总线，也不实现浮点或向量运算语义。

#pragma once

#include "rvemu/core/csr.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace rvemu::core {

class CpuState final {
public:
    static constexpr std::size_t kRegisterCount = 32U;
    static constexpr std::size_t kVectorRegisterBytes = 32U;
    using VectorRegister = std::array<std::uint8_t, kVectorRegisterBytes>;

    CpuState() = default;

    // 清空三类寄存器、CSR 与等待标志，并从指定 PC 以 M-mode 确定性启动单 Hart。
    void reset(std::uint64_t reset_pc = 0U) noexcept;

    // 三组寄存器访问器集中执行索引检查；整数写入口还永久丢弃对 x0 的写入。
    [[nodiscard]] std::uint64_t integer(std::size_t index) const;
    void set_integer(std::size_t index, std::uint64_t value);

    [[nodiscard]] std::uint64_t floating(std::size_t index) const;
    void set_floating(std::size_t index, std::uint64_t value);

    [[nodiscard]] const VectorRegister& vector(std::size_t index) const;
    void set_vector(std::size_t index, const VectorRegister& value);

    // 以下简单访问器共同暴露本 Hart 的唯一 PC、特权级、CSR 和 WFI 状态，不维护影子副本。
    [[nodiscard]] std::uint64_t program_counter() const noexcept { return program_counter_; }
    void set_program_counter(std::uint64_t value) noexcept { program_counter_ = value; }

    [[nodiscard]] PrivilegeMode privilege() const noexcept { return privilege_; }
    void set_privilege(PrivilegeMode value) noexcept { privilege_ = value; }

    [[nodiscard]] CsrFile& csrs() noexcept { return csrs_; }
    [[nodiscard]] const CsrFile& csrs() const noexcept { return csrs_; }

    [[nodiscard]] bool waiting_for_interrupt() const noexcept { return waiting_for_interrupt_; }
    void set_waiting_for_interrupt(bool value) noexcept { waiting_for_interrupt_ = value; }

private:
    std::array<std::uint64_t, kRegisterCount> integer_registers_{};
    std::array<std::uint64_t, kRegisterCount> floating_registers_{};
    std::array<VectorRegister, kRegisterCount> vector_registers_{};
    CsrFile csrs_{};
    std::uint64_t program_counter_{0U};
    PrivilegeMode privilege_{PrivilegeMode::Machine};
    bool waiting_for_interrupt_{false};
};

}  // namespace rvemu::core
