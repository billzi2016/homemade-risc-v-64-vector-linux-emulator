// 文件职责：将已验证的 RVV vtype 映射为唯一的寄存器组、元素字节位置与掩码位布局。
// 边界：本文件不保存 CSR、不决定 tail/mask 策略，也不执行指令；调用者负责把失败转为相应非法指令或
// Trap。

#pragma once

#include "rvemu/vector/vector_configuration.hpp"
#include "rvemu/vector/vector_state.hpp"

#include <cstdint>
#include <optional>

namespace rvemu::vector {

// 说明一个逻辑元素在 32×VLEN 字节寄存器文件中的唯一小端物理位置。
struct VectorElementLocation final {
    std::uint8_t register_index{0U};
    std::uint8_t byte_offset{0U};
    std::uint8_t width_bytes{0U};
};

// 描述一个由 SEW/LMUL 和基寄存器共同确定的逻辑寄存器组。
// 对整数 LMUL，组跨越相邻的 1/2/4/8 个寄存器；对分数 LMUL，组只能使用单个寄存器的低部字节。
class VectorRegisterGroup final {
   public:
    // 校验配置、整数 LMUL 的起始编号对齐和 v31 边界；失败表示指令操作数非法而非可截断的组。
    [[nodiscard]] static std::optional<VectorRegisterGroup> create(
        const VectorConfiguration& configuration, std::uint8_t base_register) noexcept;

    // 返回第 element_index 个逻辑元素的位置；越过 VLMAX 返回空，不读取或修改寄存器。
    [[nodiscard]] std::optional<VectorElementLocation> locate(
        std::uint64_t element_index) const noexcept;
    // 以 RVV 小端布局读取一个最多 64 位的元素；无效索引返回空，永不跨越本组有效范围。
    [[nodiscard]] std::optional<std::uint64_t> read_element(const VectorState& state,
                                                            std::uint64_t element_index) const;
    // 以 RVV 小端布局写入一个最多 64 位的元素；返回 false 表示索引无效且没有部分写入。
    [[nodiscard]] bool write_element(VectorState& state,
                                     std::uint64_t element_index,
                                     std::uint64_t value) const;

    // 按 RVV 掩码规则读取 v0 的第 element_index 位；掩码位与 SEW/LMUL 无关，超出 VLEN 返回空。
    [[nodiscard]] static std::optional<bool> mask_bit(const VectorState& state,
                                                      std::uint64_t element_index) noexcept;

    [[nodiscard]] const VectorConfiguration& configuration() const noexcept {
        return configuration_;
    }
    [[nodiscard]] std::uint8_t base_register() const noexcept {
        return base_register_;
    }

   private:
    VectorRegisterGroup(VectorConfiguration configuration, std::uint8_t base_register) noexcept
        : configuration_(configuration), base_register_(base_register) {
    }

    VectorConfiguration configuration_{};
    std::uint8_t base_register_{0U};
};

}  // namespace rvemu::vector
