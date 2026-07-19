// 文件职责：通过真实 CpuState 与 CsrFile 验证 RVV 寄存器文件、向量 CSR 和 VS 状态门控。
// 边界：本测试不伪造向量译码或元素执行；vset*、访存和算术语义属于后续 RVV 模块。

#include "rvemu/core/cpu_state.hpp"
#include "rvemu/core/csr.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr std::uint64_t bit(std::uint8_t index) noexcept {
    return 1ULL << index;
}

class TestContext final {
   public:
    // 累计独立断言失败，保证同一次 RVV 状态转换中的多个偏差不会被首个失败掩盖。
    void expect(bool condition, const std::string& message) {
        if (!condition) {
            ++failures_;
            std::cerr << "失败：" << message << '\n';
        }
    }

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

   private:
    int failures_{0};
};

// 测试准备同样经过生产 CSR 权限与 WARL 入口，避免私有字段注入制造不存在的状态。
void write_csr(rvemu::core::CsrFile& csrs,
               rvemu::core::CsrAddress address,
               std::uint64_t value,
               rvemu::core::PrivilegeMode privilege = rvemu::core::PrivilegeMode::Machine) {
    const auto result = csrs.access(rvemu::core::CsrAccessRequest{
        address,
        privilege,
        false,
        true,
        rvemu::core::CsrModifyOperation::Replace,
        value,
    });
    if (!result.success) {
        throw std::runtime_error("向量测试准备阶段 CSR 写入失败");
    }
}

// 读取也经过生产访问门控，因而能够验证 VS=Off 时的非法访问而非仅观察内部值。
[[nodiscard]] rvemu::core::CsrAccessResult read_csr(
    rvemu::core::CsrFile& csrs,
    rvemu::core::CsrAddress address,
    rvemu::core::PrivilegeMode privilege = rvemu::core::PrivilegeMode::Machine) {
    return csrs.access(rvemu::core::CsrAccessRequest{
        address,
        privilege,
        true,
        false,
        rvemu::core::CsrModifyOperation::Replace,
        0U,
    });
}

// 验证固定 32×256 位寄存器文件、复位清零、边界保护与由唯一 CPU 提交入口触发的 VS Dirty。
void test_vector_register_file(TestContext& context) {
    rvemu::core::CpuState state;
    state.reset();

    for (std::size_t index = 0U; index < rvemu::core::CpuState::kRegisterCount; ++index) {
        const auto& value = state.vector(index);
        for (const auto byte : value) {
            context.expect(byte == 0U, "复位后每个向量寄存器字节必须为零");
        }
    }

    rvemu::core::CpuState::VectorRegister expected{};
    expected.front() = 0xA5U;
    expected.back() = 0x5AU;
    state.set_vector(31U, expected);
    context.expect(state.vector(31U) == expected, "v31 必须保存完整 256 位字节模式");
    context.expect(((state.csrs().peek(rvemu::core::CsrAddress::Mstatus) >> 9U) & 0x3U) == 0U,
                   "VS=Off 时内部状态准备不得偷偷启用或标记来宾向量上下文");

    write_csr(state.csrs(), rvemu::core::CsrAddress::Mstatus, bit(9U));
    state.set_vector(0U, expected);
    const auto mstatus = state.csrs().peek(rvemu::core::CsrAddress::Mstatus);
    context.expect(((mstatus >> 9U) & 0x3U) == 0x3U,
                   "已启用向量上下文的寄存器写入必须标记 VS Dirty");
    context.expect((mstatus & bit(63U)) != 0U, "VS Dirty 必须派生 mstatus.SD");

    bool read_out_of_range = false;
    try {
        static_cast<void>(state.vector(32U));
    } catch (const std::out_of_range&) {
        read_out_of_range = true;
    }
    context.expect(read_out_of_range, "读取 v32 必须拒绝越界索引");
}

// 验证向量 CSR 的确定性复位、VS 门控、只读配置、vcsr 别名和可写状态造成的 Dirty 提交。
void test_vector_csrs_and_vs_gate(TestContext& context) {
    rvemu::core::CsrFile csrs;
    context.expect(csrs.peek(rvemu::core::CsrAddress::Vl) == 0U, "复位 vl 必须为零");
    context.expect(csrs.peek(rvemu::core::CsrAddress::Vtype) == bit(63U),
                   "复位 vtype 必须仅置 vill");
    context.expect(csrs.peek(rvemu::core::CsrAddress::Vstart) == 0U, "复位 vstart 必须为零");
    context.expect(csrs.peek(rvemu::core::CsrAddress::Vcsr) == 0U, "复位 vcsr 必须为零");
    context.expect(csrs.peek(rvemu::core::CsrAddress::Vlenb) == 32U, "vlenb 必须恒为 32 字节");

    context.expect(!read_csr(csrs, rvemu::core::CsrAddress::Vlenb).success,
                   "VS=Off 时读取 vlenb 必须被拒绝");
    context.expect(!read_csr(csrs, rvemu::core::CsrAddress::Vstart).success,
                   "VS=Off 时读取 vstart 必须被拒绝");

    write_csr(csrs, rvemu::core::CsrAddress::Mstatus, bit(9U));
    context.expect(csrs.vector_state_enabled(), "VS=Initial 必须允许向量上下文访问");
    const auto vlenb = read_csr(csrs, rvemu::core::CsrAddress::Vlenb);
    context.expect(vlenb.success && vlenb.value == 32U, "VS 启用后读取 vlenb 必须返回 32");

    write_csr(csrs, rvemu::core::CsrAddress::Vstart, 17U);
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Vstart).value == 17U,
                   "vstart 必须保存重启元素索引");
    context.expect(((csrs.peek(rvemu::core::CsrAddress::Mstatus) >> 9U) & 0x3U) == 0x3U,
                   "写 vstart 必须标记 VS Dirty");

    write_csr(csrs, rvemu::core::CsrAddress::Vcsr, 0x5U);
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Vxsat).value == 1U,
                   "vcsr.bit0 必须别名到 vxsat");
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Vxrm).value == 2U,
                   "vcsr.bit2:1 必须别名到 vxrm");
    write_csr(csrs, rvemu::core::CsrAddress::Vxrm, 3U);
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Vcsr).value == 0x7U,
                   "写 vxrm 必须更新 vcsr 别名视图");
    write_csr(csrs, rvemu::core::CsrAddress::Vxsat, 0U);
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Vcsr).value == 0x6U,
                   "写 vxsat 必须更新 vcsr 别名视图");

    for (const auto address : {rvemu::core::CsrAddress::Vl,
                               rvemu::core::CsrAddress::Vtype,
                               rvemu::core::CsrAddress::Vlenb}) {
        const auto write_result = csrs.access(rvemu::core::CsrAccessRequest{
            address,
            rvemu::core::PrivilegeMode::Machine,
            false,
            true,
            rvemu::core::CsrModifyOperation::Replace,
            0U,
        });
        context.expect(!write_result.success, "vl、vtype 与 vlenb 必须拒绝普通 CSR 写入");
    }

    const auto sstatus = csrs.peek(rvemu::core::CsrAddress::Sstatus);
    context.expect(((sstatus >> 9U) & 0x3U) == 0x3U, "sstatus 必须投影共享 VS Dirty 状态");
}

}  // namespace

int main() {
    TestContext context;
    try {
        test_vector_register_file(context);
        test_vector_csrs_and_vs_gate(context);
    } catch (const std::exception& error) {
        std::cerr << "RVV 状态测试出现未处理异常：" << error.what() << '\n';
        return 2;
    }

    if (context.failures() != 0) {
        std::cerr << "RVV 状态测试共有 " << context.failures() << " 项断言失败。\n";
        return 1;
    }

    std::cout << "RVV 寄存器状态、CSR 与 VS 门控测试全部通过。\n";
    return 0;
}
