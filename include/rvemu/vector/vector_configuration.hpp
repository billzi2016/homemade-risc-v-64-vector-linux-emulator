// 文件职责：集中定义 RVV 1.0 vtype 的合法性、LMUL/SEW 能力边界和 vl 选择规则。
// 边界：本文件不保存 CSR、不译码整条指令，也不读写向量寄存器；CPU 与 CSR 只能复用这里的结果。

#pragma once

#include <cstdint>

namespace rvemu::vector {

// 本项目固定 VLEN=256 位；该常量是 VLMAX 计算的唯一硬件宽度来源。
constexpr std::uint64_t kVectorLengthBits = 256U;
// RV64 上 vill 位固定处于 vtype 的最高位；非法完整配置必须只保留此位。
constexpr std::uint64_t kVtypeVill = 1ULL << 63U;

// 描述一次完整 vtype 解码结果。valid 为 false 时 vtype 必为 vill、vlmax 必为零。
struct VectorConfiguration final {
    bool valid{false};
    std::uint64_t vtype{kVtypeVill};
    std::uint64_t sew_bits{0U};
    std::uint64_t lmul_numerator{0U};
    std::uint64_t lmul_denominator{1U};
    std::uint64_t vlmax{0U};
};

// 解码完整 XLEN 宽 vtype，拒绝保留位、保留 LMUL、未实现 SEW 与不满足 ELEN 约束的分数 LMUL。
// 返回值始终已归一：非法时返回仅置 vill 的 vtype 和零 vlmax，调用者不得自行再解释原始位域。
[[nodiscard]] VectorConfiguration decode_vector_configuration(std::uint64_t vtype) noexcept;

// 在规范允许的范围内以确定性策略选择 vl：AVL 小于 VLMAX 时精确取 AVL，其余非零容量情形取 VLMAX。
// 该策略覆盖 AVL>=2*VLMAX 和中间区间，避免不同 vset* 路径作出不一致选择。
[[nodiscard]] std::uint64_t select_vector_length(
    std::uint64_t application_vector_length,
    std::uint64_t vlmax) noexcept;

// 只有旧、新配置都合法且 VLMAX 不变时，rs1=x0 且 rd=x0 的保留当前 vl 形式才允许提交。
// 它独立于具体指令编码，使 CPU 不会复制 vill 与 VLMAX 比较规则。
[[nodiscard]] bool can_preserve_vector_length(
    std::uint64_t previous_vtype,
    std::uint64_t requested_vtype) noexcept;

}  // namespace rvemu::vector
