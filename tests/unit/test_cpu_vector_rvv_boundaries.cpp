// 文件职责：通过真实 CPU、CSR、总线与机器码验证 RVV 的关键边界语义。
// 设计原则：测试不复制任何生产执行逻辑；每项断言均从已提交的体系结构状态读取结果。

#include "rvemu/bus/bus.hpp"
#include "rvemu/core/cpu.hpp"
#include "rvemu/memory/physical_memory.hpp"
#include "rvemu/vector/vector_configuration.hpp"
#include "rvemu/vector/vector_register_group.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

constexpr std::uint64_t kRamBase = 0x80000000ULL;
constexpr std::uint64_t kRamSize = 0x1000ULL;
constexpr std::uint64_t kInstructionBase = kRamBase + 0x100U;
constexpr std::uint64_t kMachineVectorStateEnabled = 1ULL << 9U;

/**
 * 以最小硬件集合构造可重复的 RVV 执行环境。
 *
 * RAM、总线及 CPU 均为生产对象；机器态 mstatus.VS 被显式设为 Initial，
 * 避免测试依赖 reset 后的隐式 CSR 初始值。
 */
struct Fixture {
    rvemu::bus::Bus bus{};
    std::shared_ptr<rvemu::memory::PhysicalMemory> ram{
        std::make_shared<rvemu::memory::PhysicalMemory>(
            rvemu::bus::PhysicalAddress{kRamBase}, kRamSize, "rvv-boundaries")};
    rvemu::core::Cpu cpu{bus};

    Fixture() {
        if (!bus.register_region(ram).ok()) {
            throw std::runtime_error("RVV 边界测试 RAM 注册失败");
        }
        cpu.state().reset(kInstructionBase);
        const auto enabled = cpu.state().csrs().access({rvemu::core::CsrAddress::Mstatus,
                                                        rvemu::core::PrivilegeMode::Machine,
                                                        false,
                                                        true,
                                                        rvemu::core::CsrModifyOperation::Replace,
                                                        kMachineVectorStateEnabled});
        if (!enabled.success) {
            throw std::runtime_error("RVV 边界测试无法启用 VS");
        }
    }

    /** 将一条 32 位指令写至复位入口并执行一个 CPU 步进。 */
    rvemu::core::StepResult run(std::uint32_t instruction) {
        if (!bus.write(rvemu::bus::PhysicalAddress{kInstructionBase},
                       rvemu::bus::AccessWidth::Word,
                       instruction,
                       rvemu::bus::AccessType::Initialization)
                 .ok()) {
            throw std::runtime_error("RVV 边界测试机器码写入失败");
        }
        cpu.state().set_program_counter(kInstructionBase);
        return cpu.step();
    }

    /** 直接写入测试 RAM 的一个字节，供向量访存指令读取。 */
    void write_byte(std::uint64_t address, std::uint8_t value) {
        if (!bus.write(rvemu::bus::PhysicalAddress{address},
                       rvemu::bus::AccessWidth::Byte,
                       value,
                       rvemu::bus::AccessType::Initialization)
                 .ok()) {
            throw std::runtime_error("RVV 边界测试 RAM 字节写入失败");
        }
    }
};

/** 编码 vsetivli：测试固定使用立即数 AVL 与 rd=x1 的明确写回路径。 */
constexpr std::uint32_t encode_vsetivli(std::uint32_t avl, std::uint32_t vtype) {
    return (3U << 30U) | ((vtype & 0x3ffU) << 20U) | ((avl & 31U) << 15U) | (7U << 12U) | (1U << 7U)
           | 0x57U;
}

/** 编码 vadd.vv；vm=false 表示使用 v0.t 作为逐元素执行掩码。 */
constexpr std::uint32_t encode_vadd_vv(std::uint32_t vd,
                                       std::uint32_t vs2,
                                       std::uint32_t vs1,
                                       bool vm = true) {
    return ((vm ? 1U : 0U) << 25U) | (vs2 << 20U) | (vs1 << 15U) | (vd << 7U) | 0x57U;
}

/** 编码 vlbu.v：使用 unit-strided 8 位无符号元素加载来测试可重启异常。 */
constexpr std::uint32_t encode_vlbu_v(std::uint32_t vd, std::uint32_t rs1) {
    return (1U << 25U) | (rs1 << 15U) | (vd << 7U) | 0x07U;
}

/** 从当前 vtype 取得某一逻辑向量寄存器的有效寄存器组。 */
rvemu::vector::VectorRegisterGroup group(Fixture& fixture, std::uint8_t index) {
    const auto configuration = rvemu::vector::decode_vector_configuration(
        fixture.cpu.state().csrs().peek(rvemu::core::CsrAddress::Vtype));
    const auto result = rvemu::vector::VectorRegisterGroup::create(configuration, index);
    if (!result.has_value()) {
        throw std::runtime_error("测试请求了非法向量寄存器组");
    }
    return *result;
}

/** 汇总断言失败，保证一个可执行文件可报告多个独立边界。 */
class TestContext {
   public:
    /** 失败不会中断执行，便于定位同一模块内所有不符合项。 */
    void expect(bool condition, const char* message) {
        if (!condition) {
            ++failures_;
            std::cerr << "失败：" << message << '\n';
        }
    }

    /** 返回进程所需的 POSIX 成功/失败退出码。 */
    int exit_code() const {
        return failures_ == 0 ? 0 : 1;
    }

   private:
    int failures_{0};
};

/** vl=0 时，任何向量算术指令都不得修改目的元素。 */
void test_zero_vector_length(TestContext& context) {
    Fixture fixture;
    context.expect(fixture.run(encode_vsetivli(0, 0)).retired, "vsetivli 应配置 vl=0");

    const auto v2 = group(fixture, 2);
    context.expect(fixture.cpu.state().set_vector_element(v2, 0, 7), "测试准备应能写入 v2[0]");
    context.expect(fixture.run(encode_vadd_vv(2, 2, 2)).retired, "vl=0 的 vadd.vv 应正常退休");
    context.expect(fixture.cpu.state().vector_element(v2, 0).value_or(0) == 7,
                   "vl=0 不得修改目的元素");
}

/** vstart 之前的元素必须保持不变，即使目的寄存器与源寄存器别名。 */
void test_vstart_prestart_and_destructive_alias(TestContext& context) {
    Fixture fixture;
    context.expect(fixture.run(encode_vsetivli(4, 0)).retired, "vsetivli 应配置 e8,m1,vl=4");

    const auto v2 = group(fixture, 2);
    for (std::uint64_t element = 0; element < 4; ++element) {
        context.expect(fixture.cpu.state().set_vector_element(v2, element, element + 1),
                       "测试准备应能写入 v2 元素");
    }

    fixture.cpu.state().csrs().set_vector_start_for_trap(2);
    context.expect(fixture.run(encode_vadd_vv(2, 2, 2)).retired,
                   "带 vstart 的别名 vadd.vv 应正常退休");
    context.expect(fixture.cpu.state().vector_element(v2, 0).value_or(0) == 1
                       && fixture.cpu.state().vector_element(v2, 1).value_or(0) == 2
                       && fixture.cpu.state().vector_element(v2, 2).value_or(0) == 6
                       && fixture.cpu.state().vector_element(v2, 3).value_or(0) == 8,
                   "vstart 前元素应保持，后续别名元素应按原值计算");
    context.expect(fixture.cpu.state().csrs().vector_start() == 0,
                   "成功完成向量指令后 vstart 应清零");
}

/** vta=vma=0 时，掩码关闭和 tail 区域均必须保留目的寄存器原值。 */
void test_mask_and_tail_undisturbed(TestContext& context) {
    Fixture fixture;
    context.expect(fixture.run(encode_vsetivli(3, 0)).retired,
                   "vsetivli 应配置 tail/mask undisturbed");

    const auto v0 = group(fixture, 0);
    const auto v2 = group(fixture, 2);
    const auto v4 = group(fixture, 4);
    const auto v6 = group(fixture, 6);
    context.expect(fixture.cpu.state().set_vector_element(v0, 0, 0b101U), "测试准备应能写入掩码位");

    for (std::uint64_t element = 0; element < 4; ++element) {
        context.expect(fixture.cpu.state().set_vector_element(v2, element, 10 + element),
                       "测试准备应能写入 v2 源元素");
        context.expect(fixture.cpu.state().set_vector_element(v4, element, 20 + element),
                       "测试准备应能写入 v4 源元素");
        context.expect(fixture.cpu.state().set_vector_element(v6, element, 0xa0U + element),
                       "测试准备应能写入 v6 目的元素");
    }

    context.expect(fixture.run(encode_vadd_vv(6, 2, 4, false)).retired, "掩码 vadd.vv 应正常退休");
    context.expect(fixture.cpu.state().vector_element(v6, 0).value_or(0) == 30
                       && fixture.cpu.state().vector_element(v6, 1).value_or(0) == 0xa1U
                       && fixture.cpu.state().vector_element(v6, 2).value_or(0) == 34
                       && fixture.cpu.state().vector_element(v6, 3).value_or(0) == 0xa3U,
                   "掩码关闭和 tail 元素应保持 undisturbed 原值");
}

/** 掩码指令以 v0 为目的寄存器必须抛出非法指令异常且不写回 v0。 */
void test_illegal_mask_destination(TestContext& context) {
    Fixture fixture;
    context.expect(fixture.run(encode_vsetivli(1, 0)).retired, "vsetivli 应配置单元素执行环境");

    const auto v0 = group(fixture, 0);
    context.expect(fixture.cpu.state().set_vector_element(v0, 0, 0x5aU), "测试准备应能写入 v0");
    const auto result = fixture.run(encode_vadd_vv(0, 2, 4, false));
    context.expect(!result.retired, "掩码目的 v0 的 vadd.vv 不得退休");
    context.expect(fixture.cpu.state().vector_element(v0, 0).value_or(0) == 0x5aU,
                   "非法指令不得修改 v0");
}

/**
 * 向量加载在第二个元素越界后必须记录 vstart，并能在环境修复后从该元素继续。
 *
 * 首次加载从 RAM 最后一个有效字节开始，元素 0 成功而元素 1 触发访存异常；
 * 第二次仅改变基址至有效范围，以证明元素 0 没有被重复覆盖且后缀能继续执行。
 */
void test_vector_memory_fault_restart(TestContext& context) {
    Fixture fixture;
    context.expect(fixture.run(encode_vsetivli(3, 0)).retired, "vsetivli 应配置三元素加载环境");

    const auto v2 = group(fixture, 2);
    fixture.write_byte(kRamBase + kRamSize - 1, 0x11U);
    fixture.write_byte(kRamBase, 0x20U);
    fixture.write_byte(kRamBase + 1, 0x21U);
    fixture.write_byte(kRamBase + 2, 0x22U);
    fixture.cpu.state().set_integer(1, kRamBase + kRamSize - 1);

    const auto fault_result = fixture.run(encode_vlbu_v(2, 1));
    context.expect(!fault_result.retired, "越界向量加载不得退休");
    context.expect(fixture.cpu.state().csrs().vector_start() == 1,
                   "访存异常应将 vstart 设为失败元素索引");
    context.expect(fixture.cpu.state().vector_element(v2, 0).value_or(0) == 0x11U,
                   "异常前的加载元素应保留");

    fixture.cpu.state().set_integer(1, kRamBase);
    context.expect(fixture.run(encode_vlbu_v(2, 1)).retired,
                   "修复访问环境后向量加载应从 vstart 重启");
    context.expect(fixture.cpu.state().vector_element(v2, 0).value_or(0) == 0x11U
                       && fixture.cpu.state().vector_element(v2, 1).value_or(0) == 0x21U
                       && fixture.cpu.state().vector_element(v2, 2).value_or(0) == 0x22U,
                   "重启不得覆盖 prestart 元素，并应完成失败元素后的加载");
    context.expect(fixture.cpu.state().csrs().vector_start() == 0, "重启成功后 vstart 应清零");
}

}  // namespace

int main() {
    TestContext context;
    test_zero_vector_length(context);
    test_vstart_prestart_and_destructive_alias(context);
    test_mask_and_tail_undisturbed(context);
    test_illegal_mask_destination(context);
    test_vector_memory_fault_restart(context);
    return context.exit_code();
}
