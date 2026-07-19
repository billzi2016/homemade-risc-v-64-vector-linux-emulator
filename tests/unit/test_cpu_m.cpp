// 文件职责：通过正式 CPU、Bus、RAM 和真实机器码验证 RV64M 2.0 的全部指令与边界结果。
// 边界：测试只构造编码和固定数学期望值，不复制生产高半乘法或有符号除法算法。

#include "rvemu/bus/bus.hpp"
#include "rvemu/core/cpu.hpp"
#include "rvemu/core/csr.hpp"
#include "rvemu/memory/physical_memory.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

constexpr std::uint64_t kRamBase = 0x8000'0000ULL;

class TestContext final {
   public:
    // 累计同一批机器码的全部断言偏差；未处理异常仍由 main 明确报告为基础设施失败。
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

// M 扩展使用标准 R 型字段，funct7 固定为 1；opcode 选择 RV64 或 OP-32 执行宽度。
[[nodiscard]] constexpr std::uint32_t encode_m(bool word_operation,
                                               std::uint32_t destination,
                                               std::uint32_t function3,
                                               std::uint32_t source1,
                                               std::uint32_t source2) noexcept {
    const auto opcode = word_operation ? 0x3BU : 0x33U;
    return (0x01U << 25U) | ((source2 & 0x1FU) << 20U) | ((source1 & 0x1FU) << 15U)
           | ((function3 & 0x7U) << 12U) | ((destination & 0x1FU) << 7U) | opcode;
}

class CpuFixture final {
   public:
    // 测试使用正式物理内存和总线注册路径，不提供替代 CPU 或算术执行器。
    CpuFixture()
        : ram_(std::make_shared<rvemu::memory::PhysicalMemory>(
              rvemu::bus::PhysicalAddress{kRamBase}, 0x1000U, "m-extension-test-ram")),
          cpu_(bus_) {
        const auto registration = bus_.register_region(ram_);
        if (!registration.ok()) {
            throw std::runtime_error("M 扩展测试 RAM 注册失败");
        }
        cpu_.state().reset(kRamBase);
    }

    [[nodiscard]] rvemu::core::Cpu& cpu() noexcept {
        return cpu_;
    }

    // 写入一条真实机器码并执行一个生产步进；失败写入立即终止而非跳过该用例。
    [[nodiscard]] rvemu::core::StepResult execute(std::uint32_t bits) {
        const auto written = bus_.write(rvemu::bus::PhysicalAddress{kRamBase},
                                        rvemu::bus::AccessWidth::Word,
                                        bits,
                                        rvemu::bus::AccessType::Initialization);
        if (!written.ok()) {
            throw std::runtime_error("M 扩展测试机器码写入失败");
        }
        cpu_.state().set_program_counter(kRamBase);
        return cpu_.step();
    }

   private:
    rvemu::bus::Bus bus_{};
    std::shared_ptr<rvemu::memory::PhysicalMemory> ram_;
    rvemu::core::Cpu cpu_;
};

// 每条 M 指令都必须正常退休且不携带结构化 Trap。
void expect_retired(TestContext& context,
                    const rvemu::core::StepResult& result,
                    const std::string& name) {
    context.expect(result.retired, name + " 应成功退休");
    context.expect(!result.stalled, name + " 不应停顿");
    context.expect(!result.trap.has_value(), name + " 不应产生 Trap");
}

// 执行一个寄存器型 M 指令并比较 rd；所有数学期望都是固定常量而非第二套动态算法。
void execute_case(TestContext& context,
                  const char* name,
                  bool word_operation,
                  std::uint32_t function3,
                  std::uint64_t lhs,
                  std::uint64_t rhs,
                  std::uint64_t expected) {
    CpuFixture fixture;
    fixture.cpu().state().set_integer(1U, lhs);
    fixture.cpu().state().set_integer(2U, rhs);
    const auto result = fixture.execute(encode_m(word_operation, 3U, function3, 1U, 2U));
    expect_retired(context, result, name);
    context.expect(fixture.cpu().state().integer(3U) == expected, std::string{name} + " 结果错误");
}

// 覆盖低半和三种高半符号组合，并用跨 32 位 limb 的固定积验证进位合成。
void test_multiplication(TestContext& context) {
    execute_case(context,
                 "MUL",
                 false,
                 0U,
                 0x1234'5678'9ABC'DEF0ULL,
                 0x0FED'CBA9'8765'4321ULL,
                 0x2236'D88F'E561'8CF0ULL);
    execute_case(
        context, "MULH 正负", false, 1U, 0xFFFF'FFFF'FFFF'FFFEULL, 3U, 0xFFFF'FFFF'FFFF'FFFFULL);
    execute_case(
        context, "MULH 双负", false, 1U, 0xFFFF'FFFF'FFFF'FFFFULL, 0xFFFF'FFFF'FFFF'FFFFULL, 0U);
    execute_case(context,
                 "MULHSU",
                 false,
                 2U,
                 0xFFFF'FFFF'FFFF'FFFFULL,
                 0xFFFF'FFFF'FFFF'FFFFULL,
                 0xFFFF'FFFF'FFFF'FFFFULL);
    execute_case(context,
                 "MULHSU 最小负数",
                 false,
                 2U,
                 0x8000'0000'0000'0000ULL,
                 2U,
                 0xFFFF'FFFF'FFFF'FFFFULL);
    execute_case(context,
                 "MULHU",
                 false,
                 3U,
                 0x1234'5678'9ABC'DEF0ULL,
                 0x0FED'CBA9'8765'4321ULL,
                 0x0121'FA00'AD77'D742ULL);
    execute_case(context, "MULW", true, 0U, 0xDEAD'BEEF'8000'0000ULL, 1U, 0xFFFF'FFFF'8000'0000ULL);
}

// 覆盖有符号向零舍入、无符号结果、余数跟随被除数符号和全部 .W 基本形式。
void test_division_and_remainder(TestContext& context) {
    constexpr std::array<std::uint64_t, 2U> negative_seven_and_three{
        0xFFFF'FFFF'FFFF'FFF9ULL,
        3U,
    };
    execute_case(context,
                 "DIV",
                 false,
                 4U,
                 negative_seven_and_three[0],
                 negative_seven_and_three[1],
                 0xFFFF'FFFF'FFFF'FFFEULL);
    execute_case(
        context, "DIVU", false, 5U, 0xFFFF'FFFF'FFFF'FFFFULL, 2U, 0x7FFF'FFFF'FFFF'FFFFULL);
    execute_case(context,
                 "REM",
                 false,
                 6U,
                 negative_seven_and_three[0],
                 negative_seven_and_three[1],
                 0xFFFF'FFFF'FFFF'FFFFULL);
    execute_case(context, "REM 正除负", false, 6U, 7U, 0xFFFF'FFFF'FFFF'FFFDU, 1U);
    execute_case(context, "REMU", false, 7U, 0xFFFF'FFFF'FFFF'FFFFULL, 2U, 1U);

    execute_case(context, "DIVW", true, 4U, 0xAAAA'AAAA'FFFF'FFF9ULL, 3U, 0xFFFF'FFFF'FFFF'FFFEULL);
    execute_case(
        context, "DIVUW", true, 5U, 0xFFFF'FFFF'FFFF'FFFFULL, 2U, 0x0000'0000'7FFF'FFFFULL);
    execute_case(context, "REMW", true, 6U, 0xAAAA'AAAA'FFFF'FFF9ULL, 3U, 0xFFFF'FFFF'FFFF'FFFFULL);
    execute_case(context, "REMUW", true, 7U, 0xFFFF'FFFF'FFFF'FFFFULL, 2U, 1U);
}

// 除零和最小负数/-1 均为架构定义结果，必须退休且绝不能触发宿主算术异常。
void test_division_edges(TestContext& context) {
    constexpr std::uint64_t minimum64 = 0x8000'0000'0000'0000ULL;
    constexpr std::uint64_t negative_one64 = 0xFFFF'FFFF'FFFF'FFFFULL;
    execute_case(context, "DIV 除零", false, 4U, 0x1234U, 0U, negative_one64);
    execute_case(context, "DIVU 除零", false, 5U, 0x1234U, 0U, negative_one64);
    execute_case(
        context, "REM 除零", false, 6U, 0x8000'0000'0000'1234ULL, 0U, 0x8000'0000'0000'1234ULL);
    execute_case(
        context, "REMU 除零", false, 7U, 0xFEDC'BA98'7654'3210ULL, 0U, 0xFEDC'BA98'7654'3210ULL);
    execute_case(context, "DIV 溢出", false, 4U, minimum64, negative_one64, minimum64);
    execute_case(context, "REM 溢出", false, 6U, minimum64, negative_one64, 0U);

    constexpr std::uint64_t minimum32_box = 0xCAFE'BABE'8000'0000ULL;
    execute_case(context, "DIVW 除零", true, 4U, minimum32_box, 0U, negative_one64);
    execute_case(context, "DIVUW 除零", true, 5U, 0x1234'5678U, 0U, negative_one64);
    execute_case(context, "REMW 除零", true, 6U, minimum32_box, 0U, 0xFFFF'FFFF'8000'0000ULL);
    execute_case(context, "REMUW 除零", true, 7U, 0xFFFF'FFFFU, 0U, negative_one64);
    execute_case(
        context, "DIVW 溢出", true, 4U, minimum32_box, 0xFFFF'FFFFU, 0xFFFF'FFFF'8000'0000ULL);
    execute_case(context, "REMW 溢出", true, 6U, minimum32_box, 0xFFFF'FFFFU, 0U);
}

// 验证 CPU 在提交 rd 前已读取两个源寄存器，并继续由 CpuState 的唯一入口保护 x0。
void test_aliasing_x0_and_reserved_encoding(TestContext& context) {
    CpuFixture alias;
    alias.cpu().state().set_integer(1U, 9U);
    alias.cpu().state().set_integer(2U, 7U);
    auto result = alias.execute(encode_m(false, 1U, 0U, 1U, 2U));
    expect_retired(context, result, "MUL rd=rs1");
    context.expect(alias.cpu().state().integer(1U) == 63U, "MUL 别名必须使用写回前的 rs1");

    CpuFixture zero;
    zero.cpu().state().set_integer(1U, 9U);
    zero.cpu().state().set_integer(2U, 7U);
    result = zero.execute(encode_m(false, 0U, 0U, 1U, 2U));
    expect_retired(context, result, "MUL 写 x0");
    context.expect(zero.cpu().state().integer(0U) == 0U, "M 扩展写 x0 必须被丢弃");

    CpuFixture reserved;
    result = reserved.execute(encode_m(true, 3U, 1U, 1U, 2U));
    context.expect(!result.retired && result.trap.has_value(), "OP-32 funct3=1 必须非法");
    if (result.trap.has_value()) {
        context.expect(result.trap->cause == rvemu::core::ExceptionCause::IllegalInstruction,
                       "保留 M 编码必须报告非法指令");
    }
}

// misa 只能在本模块全部语义和测试落地后声明 M；读取仍经过正式 CSR 原子访问入口。
void test_misa_declares_m(TestContext& context) {
    CpuFixture fixture;
    const auto misa = fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Misa);
    constexpr auto m_bit = 1ULL << static_cast<std::uint8_t>('M' - 'A');
    context.expect((misa & m_bit) != 0U, "完整实现后 misa 必须声明 M 扩展");
}

}  // namespace

int main() {
    TestContext context;
    try {
        test_multiplication(context);
        test_division_and_remainder(context);
        test_division_edges(context);
        test_aliasing_x0_and_reserved_encoding(context);
        test_misa_declares_m(context);
    } catch (const std::exception& error) {
        std::cerr << "RV64M 测试出现未处理异常：" << error.what() << '\n';
        return 2;
    }

    if (context.failures() != 0) {
        std::cerr << "RV64M 测试共有 " << context.failures() << " 项断言失败。\n";
        return 1;
    }

    std::cout << "RV64M 全部 13 条指令及除零、溢出边界测试通过。\n";
    return 0;
}
