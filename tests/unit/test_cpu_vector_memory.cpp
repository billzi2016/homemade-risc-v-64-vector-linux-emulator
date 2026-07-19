// 文件职责：以真实 CPU、RAM、MMU 访问入口验证 RVV unit-stride/strided 访存和逐元素精确异常。
// 边界：测试只组装机器码与观察架构状态，不实现替代访存、页表或向量元素逻辑。

#include "rvemu/bus/bus.hpp"
#include "rvemu/core/cpu.hpp"
#include "rvemu/core/csr.hpp"
#include "rvemu/memory/physical_memory.hpp"
#include "rvemu/vector/vector_configuration.hpp"
#include "rvemu/vector/vector_register_group.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

constexpr std::uint64_t kRamBase = 0x8000'0000ULL;
constexpr std::uint64_t kDataBase = kRamBase + 0x400U;
constexpr std::uint64_t kRamBytes = 0x2000U;

class TestContext final {
public:
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

// 组装 vsetivli；真实 CPU 仍会验证 VS、vtype 和 AVL，而非由测试直接写入 vl/vtype。
[[nodiscard]] constexpr std::uint32_t encode_vsetivli(
    std::uint32_t avl,
    std::uint32_t vtype) noexcept {
    return (0x3U << 30U) | ((vtype & 0x3FFU) << 20U) | ((avl & 0x1FU) << 15U) |
           (0x7U << 12U) | 0x57U;
}

// 只组装本阶段普通、非分段的 vector memory 字段；生产译码器负责拒绝其他组合。
[[nodiscard]] constexpr std::uint32_t encode_vector_memory(
    bool load,
    std::uint32_t width,
    std::uint32_t vector_register,
    std::uint32_t base_register,
    std::uint32_t mode,
    std::uint32_t stride_or_lumop,
    bool unmasked) noexcept {
    return ((mode & 0x3U) << 26U) | ((unmasked ? 1U : 0U) << 25U) |
           ((stride_or_lumop & 0x1FU) << 20U) | ((base_register & 0x1FU) << 15U) |
           ((width & 0x7U) << 12U) | ((vector_register & 0x1FU) << 7U) |
           (load ? 0x07U : 0x27U);
}

class CpuFixture final {
public:
    CpuFixture()
        : ram_(std::make_shared<rvemu::memory::PhysicalMemory>(
              rvemu::bus::PhysicalAddress{kRamBase}, kRamBytes, "rvv-memory-test-ram")),
          cpu_(bus_) {
        if (!bus_.register_region(ram_).ok()) {
            throw std::runtime_error("RVV 访存测试 RAM 注册失败");
        }
        cpu_.state().reset(kRamBase);
        const auto enabled = cpu_.state().csrs().access(rvemu::core::CsrAccessRequest{
            rvemu::core::CsrAddress::Mstatus,
            rvemu::core::PrivilegeMode::Machine,
            false, true, rvemu::core::CsrModifyOperation::Replace, 0x1ULL << 9U});
        if (!enabled.success) {
            throw std::runtime_error("RVV 访存测试无法启用 VS");
        }
    }

    [[nodiscard]] rvemu::core::Cpu& cpu() noexcept { return cpu_; }

    [[nodiscard]] rvemu::core::StepResult execute(std::uint32_t instruction) {
        const auto written = bus_.write(rvemu::bus::PhysicalAddress{kRamBase}, rvemu::bus::AccessWidth::Word,
            instruction, rvemu::bus::AccessType::Initialization);
        if (!written.ok()) {
            throw std::runtime_error("RVV 访存测试机器码写入失败");
        }
        cpu_.state().set_program_counter(kRamBase);
        return cpu_.step();
    }

    void write_byte(std::uint64_t address, std::uint8_t value) {
        if (!bus_.write(rvemu::bus::PhysicalAddress{address}, rvemu::bus::AccessWidth::Byte,
                        value, rvemu::bus::AccessType::Initialization).ok()) {
            throw std::runtime_error("RVV 访存测试 RAM 字节写入失败");
        }
    }
    [[nodiscard]] std::uint8_t read_byte(std::uint64_t address) {
        const auto result = bus_.read(rvemu::bus::PhysicalAddress{address}, rvemu::bus::AccessWidth::Byte,
                                      rvemu::bus::AccessType::Load);
        if (!result.ok()) {
            throw std::runtime_error("RVV 访存测试 RAM 字节读取失败");
        }
        return static_cast<std::uint8_t>(result.value);
    }

    void configure(std::uint32_t avl, std::uint32_t vtype) {
        const auto result = execute(encode_vsetivli(avl, vtype));
        if (!result.retired || result.trap.has_value()) {
            throw std::runtime_error("RVV 访存测试 vsetivli 配置失败");
        }
    }

private:
    rvemu::bus::Bus bus_{};
    std::shared_ptr<rvemu::memory::PhysicalMemory> ram_;
    rvemu::core::Cpu cpu_;
};

void expect_retired(TestContext& context, const rvemu::core::StepResult& result, const std::string& name) {
    context.expect(result.retired && !result.trap.has_value(), name + " 必须成功退休");
}

[[nodiscard]] rvemu::vector::VectorRegisterGroup require_group(
    rvemu::core::Cpu& cpu,
    std::uint8_t register_index,
    std::uint64_t eew) {
    const auto current = rvemu::vector::decode_vector_configuration(
        cpu.state().csrs().peek(rvemu::core::CsrAddress::Vtype));
    const auto memory = rvemu::vector::derive_memory_configuration(current, eew);
    const auto group = memory.has_value() ?
                           rvemu::vector::VectorRegisterGroup::create(*memory, register_index) :
                           std::nullopt;
    if (!group.has_value()) {
        throw std::runtime_error("RVV 访存测试无法构造生产寄存器组");
    }
    return *group;
}

// 验证 e16 unit-stride 的非对齐字节访问不会绕过统一总线，并按小端顺序装配元素。
void test_unit_stride_unaligned_load(TestContext& context) {
    CpuFixture fixture;
    fixture.configure(2U, 0x8U);  // e16,m1
    fixture.write_byte(kDataBase + 1U, 0x22U);
    fixture.write_byte(kDataBase + 2U, 0x11U);
    fixture.write_byte(kDataBase + 3U, 0x44U);
    fixture.write_byte(kDataBase + 4U, 0x33U);
    fixture.cpu().state().set_integer(1U, kDataBase + 1U);
    expect_retired(context, fixture.execute(encode_vector_memory(true, 5U, 2U, 1U, 0U, 0U, true)), "非对齐 vle16");
    const auto group = require_group(fixture.cpu(), 2U, 16U);
    context.expect(fixture.cpu().state().vector_element(group, 0U).value_or(0U) == 0x1122U,
                   "vle16 必须以小端装配元素 0");
    context.expect(fixture.cpu().state().vector_element(group, 1U).value_or(0U) == 0x3344U,
                   "vle16 必须以小端装配元素 1");
}

// 验证负 constant stride 按 XLEN 二补码回绕解释，同时每个活动元素都实际写入 RAM。
void test_strided_store(TestContext& context) {
    CpuFixture fixture;
    fixture.configure(3U, 0U);  // e8,m1
    const auto group = require_group(fixture.cpu(), 4U, 8U);
    context.expect(fixture.cpu().state().set_vector_element(group, 0U, 0x10U),
                   "测试准备必须能够写入 stride 源元素 0");
    context.expect(fixture.cpu().state().set_vector_element(group, 1U, 0x20U),
                   "测试准备必须能够写入 stride 源元素 1");
    context.expect(fixture.cpu().state().set_vector_element(group, 2U, 0x30U),
                   "测试准备必须能够写入 stride 源元素 2");
    fixture.cpu().state().set_integer(1U, kDataBase + 8U);
    fixture.cpu().state().set_integer(2U, ~1ULL);  // -2 字节 stride
    expect_retired(context, fixture.execute(encode_vector_memory(false, 0U, 4U, 1U, 2U, 2U, true)), "负 stride vse8");
    context.expect(fixture.read_byte(kDataBase + 8U) == 0x10U, "元素 0 必须写到基地址");
    context.expect(fixture.read_byte(kDataBase + 6U) == 0x20U, "元素 1 必须按负 stride 写入");
    context.expect(fixture.read_byte(kDataBase + 4U) == 0x30U, "元素 2 必须按负 stride 写入");
}

// 验证掩码元素不访问无效地址；活动元素 fault 后 vstart 指向该元素且之前加载结果保持。
void test_mask_and_precise_fault(TestContext& context) {
    CpuFixture fixture;
    fixture.configure(3U, 0U);  // e8,m1
    rvemu::core::CpuState::VectorRegister mask{};
    mask[0] = 0x5U;  // 仅元素 0、2 活动，元素 1 的无效地址必须不访问。
    fixture.cpu().state().set_vector(0U, mask);
    fixture.write_byte(kRamBase + kRamBytes - 1U, 0xA5U);
    fixture.cpu().state().set_integer(1U, kRamBase + kRamBytes - 1U);
    const auto result = fixture.execute(encode_vector_memory(true, 0U, 8U, 1U, 0U, 0U, false));
    context.expect(!result.retired && result.trap.has_value(), "活动元素越过 RAM 必须产生真实访问异常");
    if (result.trap.has_value()) {
        context.expect(result.trap->cause == rvemu::core::ExceptionCause::LoadAccessFault,
                       "越界 vle8 必须报告 LoadAccessFault");
    }
    context.expect(fixture.cpu().state().csrs().vector_start() == 2U,
                   "fault 必须把当前活动元素 2 写入 vstart");
    const auto group = require_group(fixture.cpu(), 8U, 8U);
    context.expect(fixture.cpu().state().vector_element(group, 0U).value_or(0U) == 0xA5U,
                   "fault 前已完成的元素必须保留");
    context.expect(fixture.cpu().state().vector_element(group, 1U).value_or(0U) == 0U,
                   "掩码关闭元素不得读取或修改目的寄存器");
}

}  // namespace

int main() {
    TestContext context;
    try {
        test_unit_stride_unaligned_load(context);
        test_strided_store(context);
        test_mask_and_precise_fault(context);
    } catch (const std::exception& error) {
        std::cerr << "RVV 向量访存测试出现未处理异常：" << error.what() << '\n';
        return 2;
    }
    if (context.failures() != 0) {
        std::cerr << "RVV 向量访存测试共有 " << context.failures() << " 项断言失败。\n";
        return 1;
    }
    std::cout << "RVV unit-stride/strided 访存与精确异常测试全部通过。\n";
    return 0;
}
