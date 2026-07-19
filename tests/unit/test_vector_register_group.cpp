// 文件职责：通过正式向量状态、CSR 与 CpuState 验证 RVV 寄存器组、元素映射和执行状态基础层。
// 边界：本测试不模拟或替代任何向量算术指令；它只验证后续执行器必须共同依赖的生产布局与状态入口。

#include "rvemu/core/cpu_state.hpp"
#include "rvemu/core/csr.hpp"
#include "rvemu/vector/vector_configuration.hpp"
#include "rvemu/vector/vector_register_group.hpp"
#include "rvemu/vector/vector_state.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

class TestContext final {
public:
    // 聚合断言确保一个寄存器组场景中的地址、数据和状态偏差均能一次性报告。
    void expect(bool condition, const std::string& message) {
        if (!condition) {
            ++failures_;
            std::cerr << "失败：" << message << '\n';
        }
    }

    [[nodiscard]] int failures() const noexcept { return failures_; }

private:
    int failures_{0};
};

// 生产 CSR 访问入口启用 VS，禁止测试直接修改私有 mstatus 或伪造向量上下文。
void enable_vector_state(rvemu::core::CsrFile& csrs) {
    const auto result = csrs.access(rvemu::core::CsrAccessRequest{
        rvemu::core::CsrAddress::Mstatus,
        rvemu::core::PrivilegeMode::Machine,
        false,
        true,
        rvemu::core::CsrModifyOperation::Replace,
        0x1ULL << 9U,
    });
    if (!result.success) {
        throw std::runtime_error("向量寄存器组测试无法启用 VS");
    }
}

// 验证 m2 的连续跨寄存器元素映射、组对齐与物理寄存器边界，不能让错误编号悄悄别名到其他组。
void test_integer_lmul_group_mapping(TestContext& context) {
    const auto configuration = rvemu::vector::decode_vector_configuration(0x11U);  // e32,m2
    const auto group = rvemu::vector::VectorRegisterGroup::create(configuration, 2U);
    context.expect(group.has_value(), "e32,m2 的 v2 必须形成合法寄存器组");
    if (!group.has_value()) {
        return;
    }

    rvemu::vector::VectorState state;
    context.expect(group->write_element(state, 7U, 0x1122'3344U), "m2 组内最后一个 v2 元素必须可写");
    context.expect(group->write_element(state, 8U, 0x5566'7788U), "m2 组跨至 v3 的首元素必须可写");
    const auto final_location = group->locate(15U);
    context.expect(
        final_location.has_value() && final_location->register_index == 3U &&
            final_location->byte_offset == 28U,
        "e32,m2 的元素 15 必须位于 v3 的最后四字节");
    context.expect(
        group->read_element(state, 7U).value_or(0U) == 0x1122'3344U &&
            group->read_element(state, 8U).value_or(0U) == 0x5566'7788U,
        "跨寄存器元素必须按小端完整往返");
    context.expect(state.byte_value(2U, 28U) == 0x44U, "小端元素最低字节必须位于最低地址");
    context.expect(!group->write_element(state, 16U, 1U), "超过 VLMAX 的元素写入必须失败且无副作用");

    context.expect(
        !rvemu::vector::VectorRegisterGroup::create(configuration, 3U).has_value(),
        "m2 的奇数基寄存器必须因组对齐被拒绝");
    const auto m8 = rvemu::vector::decode_vector_configuration(0x3U);  // e8,m8
    context.expect(
        !rvemu::vector::VectorRegisterGroup::create(m8, 28U).has_value(),
        "跨越 v31 的 m8 寄存器组必须被拒绝");
}

// 验证分数 LMUL 只使用基寄存器低部容量，剩余字节既不属于 VLMAX 也不能被元素接口访问。
void test_fractional_lmul_and_mask_layout(TestContext& context) {
    const auto configuration = rvemu::vector::decode_vector_configuration(0xEU);  // e16,mf4
    const auto group = rvemu::vector::VectorRegisterGroup::create(configuration, 31U);
    context.expect(group.has_value(), "e16,mf4 的 v31 必须是合法分数 LMUL 组");
    if (!group.has_value()) {
        return;
    }

    rvemu::vector::VectorState state;
    context.expect(group->write_element(state, 3U, 0xABCDU), "mf4 有效低 8 字节中的元素 3 必须可写");
    context.expect(state.byte_value(31U, 6U) == 0xCDU, "mf4 元素 3 的低字节必须位于字节 6");
    context.expect(state.byte_value(31U, 7U) == 0xABU, "mf4 元素 3 的高字节必须位于字节 7");
    context.expect(!group->write_element(state, 4U, 0xFFFFU), "mf4 尾部保留空间不得被当作元素 4 写入");
    context.expect(state.byte_value(31U, 8U) == 0U, "分数 LMUL 有效范围外字节必须保持未修改");

    state.set_byte_value(0U, 0U, 0x81U);
    state.set_byte_value(0U, 1U, 0x02U);
    context.expect(
        rvemu::vector::VectorRegisterGroup::mask_bit(state, 0U).value_or(false),
        "v0.bit0 必须作为元素 0 掩码位读取");
    context.expect(
        rvemu::vector::VectorRegisterGroup::mask_bit(state, 7U).value_or(false),
        "v0.bit7 必须作为元素 7 掩码位读取");
    context.expect(
        rvemu::vector::VectorRegisterGroup::mask_bit(state, 9U).value_or(false),
        "掩码位必须按字节内低位优先连续映射");
    context.expect(
        !rvemu::vector::VectorRegisterGroup::mask_bit(state, 8U).value_or(true),
        "未置位的掩码元素必须返回 false");
    context.expect(
        !rvemu::vector::VectorRegisterGroup::mask_bit(state, 256U).has_value(),
        "超过 v0 的 VLEN 位掩码索引必须被拒绝");
}

// 验证 CpuState 作为元素写入唯一提交入口会更新 VS，并验证执行器专用 CSR 方法的有限状态语义。
void test_cpu_commit_and_vector_execution_csrs(TestContext& context) {
    rvemu::core::CpuState state;
    state.reset();
    enable_vector_state(state.csrs());

    const auto configuration = rvemu::vector::decode_vector_configuration(0x18U);  // e64,m1
    const auto group = rvemu::vector::VectorRegisterGroup::create(configuration, 4U);
    if (!group.has_value()) {
        throw std::runtime_error("e64,m1 测试组创建失败");
    }
    context.expect(state.set_vector_element(*group, 3U, 0x0123'4567'89AB'CDEFULL), "CpuState 元素写入必须成功");
    context.expect(
        state.vector_element(*group, 3U).value_or(0U) == 0x0123'4567'89AB'CDEFULL,
        "CpuState 元素读写必须保持完整 64 位位模式");
    context.expect(
        ((state.csrs().peek(rvemu::core::CsrAddress::Mstatus) >> 9U) & 0x3U) == 0x3U,
        "受控元素写入必须把 VS 标记为 Dirty");

    const auto write_vstart = state.csrs().access(rvemu::core::CsrAccessRequest{
        rvemu::core::CsrAddress::Vstart,
        rvemu::core::PrivilegeMode::Machine,
        false,
        true,
        rvemu::core::CsrModifyOperation::Replace,
        0x1FFU,
    });
    context.expect(write_vstart.success, "已启用 VS 时写 vstart 必须成功");
    context.expect(state.csrs().vector_start() == 0xFFU, "固定 VLEN=256 的 vstart 仅保留低 8 位");
    state.csrs().set_vector_start_for_trap(0x123U);
    context.expect(state.csrs().vector_start() == 0x23U, "异常元素索引同样必须受 vstart 宽度约束");
    state.csrs().clear_vector_start_after_instruction();
    context.expect(state.csrs().vector_start() == 0U, "成功向量指令必须清零 vstart");

    context.expect(state.csrs().vector_rounding_mode() == 0U, "复位 vxrm 必须经唯一读取入口返回零");
    context.expect(!state.csrs().vector_saturation(), "复位 vxsat 必须为未饱和");
    state.csrs().accrue_vector_saturation(false);
    context.expect(!state.csrs().vector_saturation(), "未饱和元素不得改变黏滞 vxsat");
    state.csrs().accrue_vector_saturation(true);
    context.expect(state.csrs().vector_saturation(), "任一饱和元素必须置位黏滞 vxsat");
}

}  // namespace

int main() {
    TestContext context;
    try {
        test_integer_lmul_group_mapping(context);
        test_fractional_lmul_and_mask_layout(context);
        test_cpu_commit_and_vector_execution_csrs(context);
    } catch (const std::exception& error) {
        std::cerr << "RVV 寄存器组测试出现未处理异常：" << error.what() << '\n';
        return 2;
    }

    if (context.failures() != 0) {
        std::cerr << "RVV 寄存器组测试共有 " << context.failures() << " 项断言失败。\n";
        return 1;
    }

    std::cout << "RVV 寄存器组、元素映射和执行状态测试全部通过。\n";
    return 0;
}
