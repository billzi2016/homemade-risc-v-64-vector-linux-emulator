// 文件职责：用真实 CPU、CSR、总线和 OP-V 机器码验证 RVV 1.0 三种 vset* 配置语义。
// 边界：本测试不替代生产译码或 vtype 计算；编码器只组装输入字段，所有状态变化均由正式执行路径完成。

#include "rvemu/bus/bus.hpp"
#include "rvemu/core/cpu.hpp"
#include "rvemu/core/csr.hpp"
#include "rvemu/memory/physical_memory.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

constexpr std::uint64_t kRamBase = 0x8000'0000ULL;
constexpr std::uint64_t kVill = 1ULL << 63U;

class TestContext final {
   public:
    // 聚合断言使单个配置场景的多个架构偏差能够同时呈现，避免首个失败掩盖后续状态错误。
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

// 仅按 RVV 指令格式组装 vsetvli；完整 vtype 的合法性由生产配置模块决定。
[[nodiscard]] constexpr std::uint32_t encode_vsetvli(std::uint32_t destination,
                                                     std::uint32_t source1,
                                                     std::uint32_t vtype_immediate) noexcept {
    return ((vtype_immediate & 0x7FFU) << 20U) | ((source1 & 0x1FU) << 15U) | (0x7U << 12U)
           | ((destination & 0x1FU) << 7U) | 0x57U;
}

// vsetivli 的 AVL 是 rs1 字段中的零扩展 5 位立即数，bit31:30=11 区分于 vsetvli。
[[nodiscard]] constexpr std::uint32_t encode_vsetivli(std::uint32_t destination,
                                                      std::uint32_t application_vector_length,
                                                      std::uint32_t vtype_immediate) noexcept {
    return (0x3U << 30U) | ((vtype_immediate & 0x3FFU) << 20U)
           | ((application_vector_length & 0x1FU) << 15U) | (0x7U << 12U)
           | ((destination & 0x1FU) << 7U) | 0x57U;
}

// vsetvl 用 funct7=1000000 选择 rs2 中的完整 XLEN 宽 vtype。
[[nodiscard]] constexpr std::uint32_t encode_vsetvl(std::uint32_t destination,
                                                    std::uint32_t source1,
                                                    std::uint32_t source2) noexcept {
    return (0x40U << 25U) | ((source2 & 0x1FU) << 20U) | ((source1 & 0x1FU) << 15U) | (0x7U << 12U)
           | ((destination & 0x1FU) << 7U) | 0x57U;
}

class CpuFixture final {
   public:
    // 将真实物理 RAM 挂到正式总线，避免测试通过私有内存或替身执行器注入指令。
    CpuFixture()
        : ram_(std::make_shared<rvemu::memory::PhysicalMemory>(
              rvemu::bus::PhysicalAddress{kRamBase}, 0x1000U, "rvv-vset-test-ram")),
          cpu_(bus_) {
        if (!bus_.register_region(ram_).ok()) {
            throw std::runtime_error("RVV 配置测试 RAM 注册失败");
        }
        cpu_.state().reset(kRamBase);
    }

    [[nodiscard]] rvemu::core::Cpu& cpu() noexcept {
        return cpu_;
    }

    // 指令写入通过总线初始化事务完成，随后由 Cpu::step 走正式取指、译码与退休流程。
    [[nodiscard]] rvemu::core::StepResult execute(std::uint32_t instruction) {
        const auto written = bus_.write(rvemu::bus::PhysicalAddress{kRamBase},
                                        rvemu::bus::AccessWidth::Word,
                                        instruction,
                                        rvemu::bus::AccessType::Initialization);
        if (!written.ok()) {
            throw std::runtime_error("RVV 配置测试机器码写入失败");
        }
        cpu_.state().set_program_counter(kRamBase);
        return cpu_.step();
    }

    // VS 必须由生产 CSR 权限入口启用，不能通过修改内部 mstatus 字段伪造向量上下文。
    void enable_vector_state() {
        const auto result = cpu_.state().csrs().access(rvemu::core::CsrAccessRequest{
            rvemu::core::CsrAddress::Mstatus,
            rvemu::core::PrivilegeMode::Machine,
            false,
            true,
            rvemu::core::CsrModifyOperation::Replace,
            0x1ULL << 9U,
        });
        if (!result.success) {
            throw std::runtime_error("RVV 配置测试无法启用 VS");
        }
    }

   private:
    rvemu::bus::Bus bus_{};
    std::shared_ptr<rvemu::memory::PhysicalMemory> ram_;
    rvemu::core::Cpu cpu_;
};

void expect_retired(TestContext& context,
                    const rvemu::core::StepResult& result,
                    const std::string& name) {
    context.expect(result.retired, name + " 应退休");
    context.expect(!result.trap.has_value(), name + " 不应产生 Trap");
}

void expect_illegal(TestContext& context,
                    const rvemu::core::StepResult& result,
                    const std::string& name) {
    context.expect(!result.retired, name + " 不得退休");
    context.expect(result.trap.has_value(), name + " 必须产生 Trap");
    if (result.trap.has_value()) {
        context.expect(result.trap->cause == rvemu::core::ExceptionCause::IllegalInstruction,
                       name + " 必须报告非法指令");
    }
}

// 覆盖寄存器 AVL、立即数 AVL 与 rs2 完整 vtype 三条真实配置路径及各自 rd 写回。
void test_three_vset_forms(TestContext& context) {
    CpuFixture fixture;
    fixture.enable_vector_state();

    fixture.cpu().state().set_integer(1U, 7U);
    expect_retired(context, fixture.execute(encode_vsetvli(5U, 1U, 0x11U)), "vsetvli e32,m2");
    context.expect(fixture.cpu().state().integer(5U) == 7U, "vsetvli 必须把新 vl 写回 rd");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Vl) == 7U,
                   "e32,m2 的 vl 必须取 AVL=7");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Vtype) == 0x11U,
                   "vsetvli 必须提交合法 vtype");

    expect_retired(context, fixture.execute(encode_vsetivli(6U, 31U, 0x18U)), "vsetivli e64,m1");
    context.expect(fixture.cpu().state().integer(6U) == 4U,
                   "e64,m1 的 VLMAX=4，过大 AVL 必须选择 VLMAX");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Vl) == 4U,
                   "vsetivli 必须更新 vl");

    fixture.cpu().state().set_integer(2U, 0x5U);
    expect_retired(context, fixture.execute(encode_vsetvl(7U, 0U, 2U)), "vsetvl e8,mf8");
    context.expect(fixture.cpu().state().integer(7U) == 4U,
                   "rs1=x0 且 rd 非零时 AVL 应为全一并取 mf8 的 VLMAX=4");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Vtype) == 0x5U,
                   "vsetvl 必须读取 rs2 的完整 vtype");
}

// 覆盖非法完整 vtype 的 vill 提交、VS 门控和严格的保持 vl 保留形式，防止错误静默降级。
void test_illegal_and_preserve_rules(TestContext& context) {
    CpuFixture off_fixture;
    expect_illegal(context, off_fixture.execute(encode_vsetivli(1U, 1U, 0U)), "VS=Off 的 vsetivli");

    CpuFixture fixture;
    fixture.enable_vector_state();
    fixture.cpu().state().set_integer(1U, 3U);
    expect_retired(context, fixture.execute(encode_vsetvli(4U, 1U, 0U)), "建立 e8,m1 配置");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Vl) == 3U,
                   "初始 vl 必须为 3");

    // rd=x0/rs1=x0 在相同 VLMAX 下保持 vl，且成功完成 vset* 必须清除旧 vstart。
    const auto set_vstart = fixture.cpu().state().csrs().access(rvemu::core::CsrAccessRequest{
        rvemu::core::CsrAddress::Vstart,
        rvemu::core::PrivilegeMode::Machine,
        false,
        true,
        rvemu::core::CsrModifyOperation::Replace,
        9U,
    });
    if (!set_vstart.success) {
        throw std::runtime_error("RVV 配置测试无法设置 vstart");
    }
    expect_retired(context, fixture.execute(encode_vsetvli(0U, 0U, 0U)), "相同 VLMAX 的保持 vl");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Vl) == 3U,
                   "保持形式不得重算或清零 vl");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Vstart) == 0U,
                   "成功 vset* 必须清零 vstart");

    expect_illegal(
        context, fixture.execute(encode_vsetvli(0U, 0U, 0x10U)), "改变 VLMAX 的保持形式");

    fixture.cpu().state().set_integer(1U, 1U);
    expect_retired(context, fixture.execute(encode_vsetvli(8U, 1U, 0x4U)), "保留 LMUL 的 vsetvli");
    context.expect(fixture.cpu().state().integer(8U) == 0U, "非法 vtype 的 rd 必须写回零 vl");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Vtype) == kVill,
                   "非法 vtype 必须提交仅置 vill 的 vtype");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Vl) == 0U,
                   "非法 vtype 必须提交 vl=0");

    expect_retired(context, fixture.execute(encode_vsetvli(9U, 1U, 0x100U)), "保留高位的 vsetvli");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Vtype) == kVill,
                   "非零保留高位同样必须提交 vill");
}

}  // namespace

int main() {
    TestContext context;
    try {
        test_three_vset_forms(context);
        test_illegal_and_preserve_rules(context);
    } catch (const std::exception& error) {
        std::cerr << "RVV vset 配置测试出现未处理异常：" << error.what() << '\n';
        return 2;
    }

    if (context.failures() != 0) {
        std::cerr << "RVV vset 配置测试共有 " << context.failures() << " 项断言失败。\n";
        return 1;
    }

    std::cout << "RVV vsetvli、vsetivli、vsetvl 配置测试全部通过。\n";
    return 0;
}
