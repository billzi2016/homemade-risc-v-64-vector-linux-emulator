// 文件职责：以真实 OP-V 编码验证 RVV 浮点 vv/vf 四则运算、frm/fflags、掩码、tail 与 vstart。
// 边界：测试只驱动生产 Cpu、CSR 与软浮点路径，绝不提供替代执行器或伪造浮点结果。

#include "rvemu/bus/bus.hpp"
#include "rvemu/core/cpu.hpp"
#include "rvemu/memory/physical_memory.hpp"
#include "rvemu/vector/vector_configuration.hpp"
#include "rvemu/vector/vector_register_group.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
constexpr std::uint64_t kBaseAddress = 0x80000000ULL;
class VectorFloatingFixture final {
   public:
    VectorFloatingFixture()
        : ram_(std::make_shared<rvemu::memory::PhysicalMemory>(
              rvemu::bus::PhysicalAddress{kBaseAddress}, 0x1000U, "rvv-floating")),
          cpu_(bus_) {
        if (!bus_.register_region(ram_).ok())
            throw std::runtime_error("无法注册向量浮点测试内存");
        cpu_.state().reset(kBaseAddress);
        const auto enabled = cpu_.state().csrs().access({rvemu::core::CsrAddress::Mstatus,
                                                         rvemu::core::PrivilegeMode::Machine,
                                                         false,
                                                         true,
                                                         rvemu::core::CsrModifyOperation::Replace,
                                                         (1ULL << 9U) | (1ULL << 13U)});
        if (!enabled.success)
            throw std::runtime_error("无法启用 VS/FS");
    }
    [[nodiscard]] rvemu::core::StepResult run(std::uint32_t instruction) {
        if (!bus_.write(rvemu::bus::PhysicalAddress{kBaseAddress},
                        rvemu::bus::AccessWidth::Word,
                        instruction,
                        rvemu::bus::AccessType::Initialization)
                 .ok())
            throw std::runtime_error("无法写入测试指令");
        cpu_.state().set_program_counter(kBaseAddress);
        return cpu_.step();
    }
    rvemu::bus::Bus bus_{};
    std::shared_ptr<rvemu::memory::PhysicalMemory> ram_;
    rvemu::core::Cpu cpu_;
};
[[nodiscard]] constexpr std::uint32_t vsetivli(std::uint32_t avl, std::uint32_t vtype) {
    return (0x3U << 30U) | ((vtype & 0x3FFU) << 20U) | ((avl & 0x1FU) << 15U) | (0x7U << 12U)
           | 0x57U;
}
[[nodiscard]] constexpr std::uint32_t vector_floating(std::uint32_t funct6,
                                                      std::uint32_t funct3,
                                                      std::uint32_t destination,
                                                      std::uint32_t source2,
                                                      std::uint32_t source1,
                                                      bool unmasked = true) {
    return (funct6 << 26U) | ((unmasked ? 1U : 0U) << 25U) | ((source2 & 0x1FU) << 20U)
           | ((source1 & 0x1FU) << 15U) | (funct3 << 12U) | ((destination & 0x1FU) << 7U) | 0x57U;
}
[[nodiscard]] rvemu::vector::VectorRegisterGroup group(VectorFloatingFixture& fixture,
                                                       std::uint8_t index) {
    const auto c = rvemu::vector::decode_vector_configuration(
        fixture.cpu_.state().csrs().peek(rvemu::core::CsrAddress::Vtype));
    const auto g = rvemu::vector::VectorRegisterGroup::create(c, index);
    if (!g)
        throw std::runtime_error("无法构造向量寄存器组");
    return *g;
}
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "失败：" << message << '\n';
    }
}
void test_single_precision_vv_arithmetic() {
    VectorFloatingFixture f;
    expect(f.run(vsetivli(2U, 0x10U)).retired, "配置 e32,m1");
    const auto lhs = group(f, 2U);
    const auto rhs = group(f, 4U);
    const auto dest = group(f, 6U);
    expect(f.cpu_.state().set_vector_element(lhs, 0U, 0x3F800000U), "写入 1.0f");
    expect(f.cpu_.state().set_vector_element(lhs, 1U, 0x40800000U), "写入 4.0f");
    expect(f.cpu_.state().set_vector_element(rhs, 0U, 0x40000000U), "写入 2.0f");
    expect(f.cpu_.state().set_vector_element(rhs, 1U, 0x40000000U), "写入第二个 2.0f");
    expect(f.run(vector_floating(0x00U, 0x1U, 6U, 2U, 4U)).retired, "vfadd.vv 退休");
    expect(f.cpu_.state().vector_element(dest, 0U).value_or(0U) == 0x40400000U, "vfadd.vv 元素 0");
    expect(f.run(vector_floating(0x02U, 0x1U, 6U, 2U, 4U)).retired, "vfsub.vv 退休");
    expect(f.cpu_.state().vector_element(dest, 1U).value_or(0U) == 0x40000000U, "vfsub.vv 元素 1");
    expect(f.run(vector_floating(0x24U, 0x1U, 6U, 2U, 4U)).retired, "vfmul.vv 退休");
    expect(f.cpu_.state().vector_element(dest, 1U).value_or(0U) == 0x41000000U, "vfmul.vv 元素 1");
    expect(f.run(vector_floating(0x20U, 0x1U, 6U, 2U, 4U)).retired, "vfdiv.vv 退休");
    expect(f.cpu_.state().vector_element(dest, 1U).value_or(0U) == 0x40000000U, "vfdiv.vv 元素 1");
}
void test_vf_mask_tail_and_flags() {
    VectorFloatingFixture f;
    expect(f.run(vsetivli(3U, 0xD0U)).retired, "配置 e32,m1,ta,ma");
    const auto dividend = group(f, 2U);
    const auto divisor = group(f, 4U);
    const auto dest = group(f, 6U);
    expect(f.cpu_.state().set_vector_element(dividend, 0U, 0x3F800000U), "写活动被除数");
    expect(f.cpu_.state().set_vector_element(dividend, 1U, 0x40000000U), "写掩码被除数");
    expect(f.cpu_.state().set_vector_element(dividend, 2U, 0x40400000U), "写第二活动被除数");
    expect(f.cpu_.state().set_vector_element(divisor, 0U, 0U), "写活动零除数");
    expect(f.cpu_.state().set_vector_element(divisor, 1U, 0U), "写掩码零除数");
    expect(f.cpu_.state().set_vector_element(divisor, 2U, 0x40000000U), "写第二活动除数");
    rvemu::core::CpuState::VectorRegister mask{};
    mask[0] = 0x5U;
    f.cpu_.state().set_vector(0U, mask);
    expect(f.run(vector_floating(0x20U, 0x1U, 6U, 2U, 4U, false)).retired, "masked vfdiv.vv 退休");
    expect(f.cpu_.state().vector_element(dest, 0U).value_or(0U) == 0x7F800000U,
           "活动除零产生正无穷");
    expect(f.cpu_.state().vector_element(dest, 1U).value_or(0U) == 0xFFFFFFFFU,
           "掩码 agnostic 为全一");
    // 3.0f / 2.0f 的 IEEE-754 单精度精确结果是 1.5f；掩码元素的零除不得影响此元素。
    expect(f.cpu_.state().vector_element(dest, 2U).value_or(0U) == 0x3FC00000U,
           "第二活动元素结果");
    expect(f.cpu_.state().vector_element(dest, 3U).value_or(0U) == 0xFFFFFFFFU,
           "tail agnostic 为全一");
    expect((f.cpu_.state().csrs().peek(rvemu::core::CsrAddress::Fflags) & 0x8U) != 0U,
           "活动零除累积 DZ");
    expect(f.cpu_.state().csrs().vector_start() == 0U, "成功清零 vstart");
    expect(f.run(vsetivli(1U, 0x18U)).retired, "配置 e64,m1");
    const auto double_source = group(f, 8U);
    const auto double_dest = group(f, 10U);
    expect(f.cpu_.state().set_vector_element(double_source, 0U, 0x4000000000000000ULL), "写入 2.0");
    f.cpu_.state().set_floating(1U, 0x3FF0000000000000ULL);
    expect(f.run(vector_floating(0x00U, 0x5U, 10U, 8U, 1U)).retired, "vfadd.vf 退休");
    expect(f.cpu_.state().vector_element(double_dest, 0U).value_or(0U) == 0x4008000000000000ULL,
           "vfadd.vf 双精度结果");
}
void test_invalid_dynamic_rounding_is_illegal() {
    VectorFloatingFixture f;
    expect(f.run(vsetivli(1U, 0x10U)).retired, "配置非法 frm 前的向量状态");
    const auto wrote = f.cpu_.state().csrs().access({rvemu::core::CsrAddress::Frm,
                                                     rvemu::core::PrivilegeMode::Machine,
                                                     false,
                                                     true,
                                                     rvemu::core::CsrModifyOperation::Replace,
                                                     0x5U});
    expect(wrote.success, "写入保留 frm");
    expect(f.run(vector_floating(0x00U, 0x1U, 6U, 2U, 4U)).trap.has_value(),
           "保留 frm 触发非法指令");
}
}  // namespace
int main() {
    try {
        test_single_precision_vv_arithmetic();
        test_vf_mask_tail_and_flags();
        test_invalid_dynamic_rounding_is_illegal();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    if (failures != 0)
        return 1;
    std::cout << "RVV 浮点测试全部通过。\n";
    return 0;
}
