// 文件职责：定义单 Hart 的 RVV 1.0 向量寄存器文件，固定保存 32 个 VLEN=256 位寄存器。
// 边界：本文件不保存向量 CSR、不译码或执行指令、不访问内存；VS 状态由 CPU/CSR 层统一维护。

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rvemu::vector {

// 向量寄存器文件只承载字节级架构位模式；元素宽度、LMUL 分组和掩码解释属于后续执行器。
class VectorState final {
public:
    static constexpr std::size_t kRegisterCount = 32U;
    static constexpr std::size_t kRegisterBytes = 32U;
    using Register = std::array<std::uint8_t, kRegisterBytes>;

    // 清零全部寄存器，确保重置后的 VLEN 内容不保留上次运行或测试的字节。
    void reset() noexcept;

    // 返回一个寄存器的只读字节视图；索引不在 0..31 表示模拟器调用缺陷并抛出异常。
    [[nodiscard]] const Register& register_value(std::size_t index) const;
    // 原子替换单个完整 256 位寄存器；逐元素提交和 vstart 语义由执行器在调用前决定。
    void set_register_value(std::size_t index, const Register& value);

private:
    std::array<Register, kRegisterCount> registers_{};
};

}  // namespace rvemu::vector
