// 文件职责：通过正式 CPU、Bus、RAM 和真实机器码验证 RV64A 2.1 的全部 LR/SC 与 AMO 语义。
// 边界：测试只构造标准编码和固定预期结果，不复制生产 AMO 算法或保留监视器。

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
constexpr std::uint64_t kDataAddress = kRamBase + 0x100U;

class TestContext final {
   public:
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

// A 扩展使用 opcode=0101111；funct5、aq、rl 与宽度字段显式传入以覆盖真实编码。
[[nodiscard]] constexpr std::uint32_t encode_atomic(std::uint32_t function5,
                                                    bool word_operation,
                                                    std::uint32_t destination,
                                                    std::uint32_t source1,
                                                    std::uint32_t source2,
                                                    bool acquire = false,
                                                    bool release = false) noexcept {
    return ((function5 & 0x1FU) << 27U) | (static_cast<std::uint32_t>(acquire) << 26U)
           | (static_cast<std::uint32_t>(release) << 25U) | ((source2 & 0x1FU) << 20U)
           | ((source1 & 0x1FU) << 15U) | ((word_operation ? 0x2U : 0x3U) << 12U)
           | ((destination & 0x1FU) << 7U) | 0x2FU;
}

// 普通 store 用于验证同 Hart LR/SC 之间的非原子写入不会被误当成外部失效事件。
[[nodiscard]] constexpr std::uint32_t encode_store(std::uint32_t function3,
                                                   std::uint32_t source1,
                                                   std::uint32_t source2,
                                                   std::uint32_t immediate12) noexcept {
    const auto upper = (immediate12 >> 5U) & 0x7FU;
    const auto lower = immediate12 & 0x1FU;
    return (upper << 25U) | ((source2 & 0x1FU) << 20U) | ((source1 & 0x1FU) << 15U)
           | ((function3 & 0x07U) << 12U) | (lower << 7U) | 0x23U;
}

class CpuFixture final {
   public:
    CpuFixture()
        : ram_(std::make_shared<rvemu::memory::PhysicalMemory>(
              rvemu::bus::PhysicalAddress{kRamBase}, 0x1000U, "a-extension-test-ram")),
          cpu_(bus_) {
        if (!bus_.register_region(ram_).ok()) {
            throw std::runtime_error("A 扩展测试 RAM 注册失败");
        }
        cpu_.state().reset(kRamBase);
    }

    [[nodiscard]] rvemu::core::Cpu& cpu() noexcept {
        return cpu_;
    }
    [[nodiscard]] rvemu::bus::Bus& bus() noexcept {
        return bus_;
    }

    // 指令区与保留数据区分离，测试装载下一条机器码不会意外使数据保留失效。
    [[nodiscard]] rvemu::core::StepResult execute(std::uint32_t bits) {
        const auto written = bus_.write(rvemu::bus::PhysicalAddress{kRamBase},
                                        rvemu::bus::AccessWidth::Word,
                                        bits,
                                        rvemu::bus::AccessType::Initialization);
        if (!written.ok()) {
            throw std::runtime_error("A 扩展测试机器码写入失败");
        }
        cpu_.state().set_program_counter(kRamBase);
        return cpu_.step();
    }

    void write_data(std::uint64_t address, bool word_operation, std::uint64_t value) {
        const auto width =
            word_operation ? rvemu::bus::AccessWidth::Word : rvemu::bus::AccessWidth::DoubleWord;
        if (!bus_.write(rvemu::bus::PhysicalAddress{address},
                        width,
                        value,
                        rvemu::bus::AccessType::Store)
                 .ok()) {
            throw std::runtime_error("A 扩展测试数据写入失败");
        }
    }

    [[nodiscard]] std::uint64_t read_data(std::uint64_t address, bool word_operation) {
        const auto width =
            word_operation ? rvemu::bus::AccessWidth::Word : rvemu::bus::AccessWidth::DoubleWord;
        const auto loaded =
            bus_.read(rvemu::bus::PhysicalAddress{address}, width, rvemu::bus::AccessType::Load);
        if (!loaded.ok()) {
            throw std::runtime_error("A 扩展测试数据读取失败");
        }
        return loaded.value;
    }

   private:
    rvemu::bus::Bus bus_{};
    std::shared_ptr<rvemu::memory::PhysicalMemory> ram_;
    rvemu::core::Cpu cpu_;
};

void expect_retired(TestContext& context,
                    const rvemu::core::StepResult& result,
                    const std::string& name) {
    context.expect(result.retired, name + " 应成功退休");
    context.expect(!result.stalled, name + " 不应停顿");
    context.expect(!result.trap.has_value(), name + " 不应产生 Trap");
}

void expect_trap(TestContext& context,
                 const rvemu::core::StepResult& result,
                 rvemu::core::ExceptionCause cause,
                 std::uint64_t value,
                 const std::string& name) {
    context.expect(!result.retired && result.trap.has_value(), name + " 必须产生精确异常");
    if (result.trap.has_value()) {
        context.expect(result.trap->cause == cause, name + " cause 错误");
        context.expect(result.trap->value == value, name + " tval 错误");
    }
}

// 覆盖 .W 符号扩展、.D 原值、SC 成功码以及一次性消费语义。
void test_lr_sc_basic(TestContext& context) {
    CpuFixture word;
    word.write_data(kDataAddress, true, 0x8000'0001U);
    word.cpu().state().set_integer(1U, kDataAddress);
    auto result = word.execute(encode_atomic(0x02U, true, 3U, 1U, 0U, true, true));
    expect_retired(context, result, "LR.W aqrl");
    context.expect(word.cpu().state().integer(3U) == 0xFFFF'FFFF'8000'0001ULL,
                   "LR.W 必须符号扩展旧值");

    word.cpu().state().set_integer(2U, 0xCAFE'BABEU);
    result = word.execute(encode_atomic(0x03U, true, 4U, 1U, 2U, true, true));
    expect_retired(context, result, "SC.W aqrl");
    context.expect(word.cpu().state().integer(4U) == 0U, "有效 SC.W 必须返回成功码 0");
    context.expect(word.read_data(kDataAddress, true) == 0xCAFE'BABEU,
                   "成功 SC.W 必须提交低 32 位");

    word.cpu().state().set_integer(2U, 0x1234'5678U);
    result = word.execute(encode_atomic(0x03U, true, 4U, 1U, 2U));
    expect_retired(context, result, "重复 SC.W");
    context.expect(word.cpu().state().integer(4U) == 1U, "无保留 SC.W 必须返回非零失败码");
    context.expect(word.read_data(kDataAddress, true) == 0xCAFE'BABEU, "失败 SC.W 不得修改内存");

    CpuFixture double_word;
    double_word.write_data(kDataAddress, false, 0x0123'4567'89AB'CDEFULL);
    double_word.cpu().state().set_integer(1U, kDataAddress);
    result = double_word.execute(encode_atomic(0x02U, false, 5U, 1U, 0U));
    expect_retired(context, result, "LR.D");
    context.expect(double_word.cpu().state().integer(5U) == 0x0123'4567'89AB'CDEFULL,
                   "LR.D 必须返回完整 64 位旧值");
    double_word.cpu().state().set_integer(2U, 0xFEDC'BA98'7654'3210ULL);
    result = double_word.execute(encode_atomic(0x03U, false, 6U, 1U, 2U));
    expect_retired(context, result, "SC.D");
    context.expect(double_word.cpu().state().integer(6U) == 0U, "有效 SC.D 必须成功");
    context.expect(double_word.read_data(kDataAddress, false) == 0xFEDC'BA98'7654'3210ULL,
                   "SC.D 必须提交完整双字");

    CpuFixture lr_word_sc_double;
    lr_word_sc_double.write_data(kDataAddress, false, 0x1111'2222'3333'4444ULL);
    lr_word_sc_double.cpu().state().set_integer(1U, kDataAddress);
    result = lr_word_sc_double.execute(encode_atomic(0x02U, true, 5U, 1U, 0U));
    expect_retired(context, result, "LR.W 后接 SC.D 前置 LR");
    lr_word_sc_double.cpu().state().set_integer(2U, 0xAAAA'BBBB'CCCC'DDDDULL);
    result = lr_word_sc_double.execute(encode_atomic(0x03U, false, 6U, 1U, 2U));
    expect_retired(context, result, "LR.W 后接 SC.D");
    context.expect(lr_word_sc_double.cpu().state().integer(6U) == 0U,
                   "同地址 LR.W 后 SC.D 必须成功");
    context.expect(lr_word_sc_double.read_data(kDataAddress, false) == 0xAAAA'BBBB'CCCC'DDDDULL,
                   "LR.W 后 SC.D 必须提交完整双字");

    CpuFixture lr_double_sc_word;
    lr_double_sc_word.write_data(kDataAddress, false, 0x9999'8888'7777'6666ULL);
    lr_double_sc_word.cpu().state().set_integer(1U, kDataAddress);
    result = lr_double_sc_word.execute(encode_atomic(0x02U, false, 5U, 1U, 0U));
    expect_retired(context, result, "LR.D 后接 SC.W 前置 LR");
    lr_double_sc_word.cpu().state().set_integer(2U, 0x1234'5678U);
    result = lr_double_sc_word.execute(encode_atomic(0x03U, true, 6U, 1U, 2U));
    expect_retired(context, result, "LR.D 后接 SC.W");
    context.expect(lr_double_sc_word.cpu().state().integer(6U) == 0U,
                   "同地址 LR.D 后 SC.W 必须成功");
    context.expect(lr_double_sc_word.read_data(kDataAddress, false) == 0x9999'8888'1234'5678ULL,
                   "LR.D 后 SC.W 只提交低 32 位并保留高 32 位");

    CpuFixture store_between;
    store_between.write_data(kDataAddress, false, 0x0102'0304'0506'0708ULL);
    store_between.cpu().state().set_integer(1U, kDataAddress);
    result = store_between.execute(encode_atomic(0x02U, false, 5U, 1U, 0U));
    expect_retired(context, result, "LR.D 后接同 Hart SB 前置 LR");
    store_between.cpu().state().set_integer(2U, 0xAAU);
    result = store_between.execute(encode_store(0U, 1U, 2U, 0U));
    expect_retired(context, result, "LR.D 后同 Hart SB");
    store_between.cpu().state().set_integer(2U, 0xDEAD'BEEF'CAFE'BABEULL);
    result = store_between.execute(encode_atomic(0x03U, false, 6U, 1U, 2U));
    expect_retired(context, result, "同 Hart SB 后 SC.D");
    context.expect(store_between.cpu().state().integer(6U) == 0U,
                   "同 Hart 普通 store 不得使 SC.D 失败");
    context.expect(store_between.read_data(kDataAddress, false) == 0xDEAD'BEEF'CAFE'BABEULL,
                   "同 Hart SB 后成功 SC.D 必须提交最终双字");
}

// 精确保留范围允许不重叠写；普通写、DMA 和 AMO 对重叠字节必须使 SC 失败。
void test_reservation_invalidation(TestContext& context) {
    CpuFixture fixture;
    fixture.write_data(kDataAddress, false, 0x10U);
    fixture.cpu().state().set_integer(1U, kDataAddress);
    auto result = fixture.execute(encode_atomic(0x02U, false, 3U, 1U, 0U));
    expect_retired(context, result, "失效测试 LR.D");

    context.expect(fixture.bus()
                       .write(rvemu::bus::PhysicalAddress{kDataAddress + 0x20U},
                              rvemu::bus::AccessWidth::Word,
                              0xAAU,
                              rvemu::bus::AccessType::DmaWrite)
                       .ok(),
                   "不重叠 DMA 写必须成功");
    fixture.cpu().state().set_integer(2U, 0x20U);
    result = fixture.execute(encode_atomic(0x03U, false, 4U, 1U, 2U));
    expect_retired(context, result, "不重叠写后的 SC.D");
    context.expect(fixture.cpu().state().integer(4U) == 0U, "不重叠写不得使保留失效");

    result = fixture.execute(encode_atomic(0x02U, false, 3U, 1U, 0U));
    expect_retired(context, result, "DMA 失效前 LR.D");
    context.expect(fixture.bus()
                       .write(rvemu::bus::PhysicalAddress{kDataAddress + 3U},
                              rvemu::bus::AccessWidth::Byte,
                              0xCCU,
                              rvemu::bus::AccessType::DmaWrite)
                       .ok(),
                   "重叠 DMA 写必须成功");
    result = fixture.execute(encode_atomic(0x03U, false, 4U, 1U, 2U));
    expect_retired(context, result, "DMA 失效后的 SC.D");
    context.expect(fixture.cpu().state().integer(4U) == 1U, "重叠 DMA 写必须使 SC 失败");

    result = fixture.execute(encode_atomic(0x02U, false, 3U, 1U, 0U));
    expect_retired(context, result, "AMO 失效前 LR.D");
    fixture.cpu().state().set_integer(2U, 1U);
    result = fixture.execute(encode_atomic(0x00U, false, 5U, 1U, 2U));
    expect_retired(context, result, "重叠 AMOADD.D");
    fixture.cpu().state().set_integer(2U, 0x55U);
    result = fixture.execute(encode_atomic(0x03U, false, 4U, 1U, 2U));
    expect_retired(context, result, "AMO 失效后的 SC.D");
    context.expect(fixture.cpu().state().integer(4U) == 1U, "重叠 AMO 必须使旧保留失效");
}

struct AmoCase final {
    const char* name;
    std::uint32_t function5;
    std::uint64_t observed;
    std::uint64_t operand;
    std::uint64_t expected_memory;
};

// 表中预期值均为固定常量，覆盖每个 AMO funct5，而不是在测试中重写计算器。
constexpr std::array<AmoCase, 9U> kWordCases{{
    {"AMOSWAP.W", 0x01U, 0x8000'0005U, 7U, 7U},
    {"AMOADD.W", 0x00U, 0xFFFF'FFFEU, 3U, 1U},
    {"AMOXOR.W", 0x04U, 0xA5A5'0000U, 0x00FF'00FFU, 0xA55A'00FFU},
    {"AMOAND.W", 0x0CU, 0xF0F0'0F0FU, 0x0FF0'FFF0U, 0x00F0'0F00U},
    {"AMOOR.W", 0x08U, 0xF000'000FU, 0x0FF0'00F0U, 0xFFF0'00FFU},
    {"AMOMIN.W", 0x10U, 0x8000'0005U, 7U, 0x8000'0005U},
    {"AMOMAX.W", 0x14U, 0x8000'0005U, 7U, 7U},
    {"AMOMINU.W", 0x18U, 0x8000'0005U, 7U, 7U},
    {"AMOMAXU.W", 0x1CU, 0x8000'0005U, 7U, 0x8000'0005U},
}};

constexpr std::array<AmoCase, 9U> kDoubleCases{{
    {"AMOSWAP.D", 0x01U, 0x8000'0000'0000'0005ULL, 7U, 7U},
    {"AMOADD.D", 0x00U, 0xFFFF'FFFF'FFFF'FFFEULL, 3U, 1U},
    {"AMOXOR.D",
     0x04U,
     0xA5A5'0000'FFFF'0000ULL,
     0x00FF'00FF'00FF'00FFULL,
     0xA55A'00FF'FF00'00FFULL},
    {"AMOAND.D",
     0x0CU,
     0xF0F0'0F0F'F0F0'0F0FULL,
     0x0FF0'FFF0'0FF0'FFF0ULL,
     0x00F0'0F00'00F0'0F00ULL},
    {"AMOOR.D",
     0x08U,
     0xF000'000F'F000'000FULL,
     0x0FF0'00F0'0FF0'00F0ULL,
     0xFFF0'00FF'FFF0'00FFULL},
    {"AMOMIN.D", 0x10U, 0x8000'0000'0000'0005ULL, 7U, 0x8000'0000'0000'0005ULL},
    {"AMOMAX.D", 0x14U, 0x8000'0000'0000'0005ULL, 7U, 7U},
    {"AMOMINU.D", 0x18U, 0x8000'0000'0000'0005ULL, 7U, 7U},
    {"AMOMAXU.D", 0x1CU, 0x8000'0000'0000'0005ULL, 7U, 0x8000'0000'0000'0005ULL},
}};

void execute_amo_cases(TestContext& context,
                       const std::array<AmoCase, 9U>& cases,
                       bool word_operation) {
    for (const auto& test_case : cases) {
        CpuFixture fixture;
        fixture.write_data(kDataAddress, word_operation, test_case.observed);
        fixture.cpu().state().set_integer(1U, kDataAddress);
        fixture.cpu().state().set_integer(2U, test_case.operand);
        const auto result = fixture.execute(
            encode_atomic(test_case.function5, word_operation, 3U, 1U, 2U, true, true));
        expect_retired(context, result, test_case.name);
        const auto expected_old = word_operation && (test_case.observed & 0x8000'0000U) != 0U
                                      ? (test_case.observed | 0xFFFF'FFFF'0000'0000ULL)
                                      : test_case.observed;
        context.expect(fixture.cpu().state().integer(3U) == expected_old,
                       std::string{test_case.name} + " rd 旧值错误");
        context.expect(fixture.read_data(kDataAddress, word_operation) == test_case.expected_memory,
                       std::string{test_case.name} + " 内存新值错误");
    }
}

void test_amo_aliases(TestContext& context) {
    CpuFixture alias;
    alias.write_data(kDataAddress, false, 5U);
    alias.cpu().state().set_integer(1U, kDataAddress);
    alias.cpu().state().set_integer(2U, 3U);
    auto result = alias.execute(encode_atomic(0x00U, false, 1U, 1U, 2U));
    expect_retired(context, result, "AMOADD.D rd=rs1");
    context.expect(alias.cpu().state().integer(1U) == 5U, "rd=rs1 必须写回提交前旧值");
    context.expect(alias.read_data(kDataAddress, false) == 8U, "rd=rs1 不得破坏地址操作数取值");

    CpuFixture zero;
    zero.write_data(kDataAddress, false, 5U);
    zero.cpu().state().set_integer(1U, kDataAddress);
    zero.cpu().state().set_integer(2U, 3U);
    result = zero.execute(encode_atomic(0x00U, false, 0U, 1U, 2U));
    expect_retired(context, result, "AMOADD.D rd=x0");
    context.expect(zero.cpu().state().integer(0U) == 0U, "AMO 写 x0 必须被丢弃");
    context.expect(zero.read_data(kDataAddress, false) == 8U, "rd=x0 仍必须提交原子内存更新");
}

void test_illegal_fault_and_alignment(TestContext& context) {
    CpuFixture fixture;
    fixture.write_data(kDataAddress, false, 0x1122'3344'5566'7788ULL);
    fixture.cpu().state().set_integer(1U, kDataAddress + 1U);
    auto result = fixture.execute(encode_atomic(0x02U, false, 3U, 1U, 0U));
    expect_trap(context,
                result,
                rvemu::core::ExceptionCause::LoadAddressMisaligned,
                kDataAddress + 1U,
                "错位 LR.D");
    result = fixture.execute(encode_atomic(0x03U, false, 3U, 1U, 2U));
    expect_trap(context,
                result,
                rvemu::core::ExceptionCause::StoreAddressMisaligned,
                kDataAddress + 1U,
                "错位 SC.D");
    result = fixture.execute(encode_atomic(0x00U, false, 3U, 1U, 2U));
    expect_trap(context,
                result,
                rvemu::core::ExceptionCause::StoreAddressMisaligned,
                kDataAddress + 1U,
                "错位 AMOADD.D");
    context.expect(fixture.read_data(kDataAddress, false) == 0x1122'3344'5566'7788ULL,
                   "错位原子指令不得产生部分内存副作用");

    CpuFixture unmapped;
    unmapped.cpu().state().set_integer(1U, 0x7000'0000U);
    result = unmapped.execute(encode_atomic(0x02U, false, 3U, 1U, 0U));
    expect_trap(
        context, result, rvemu::core::ExceptionCause::LoadAccessFault, 0x7000'0000U, "未映射 LR.D");
    result = unmapped.execute(encode_atomic(0x00U, false, 3U, 1U, 2U));
    expect_trap(context,
                result,
                rvemu::core::ExceptionCause::StoreAccessFault,
                0x7000'0000U,
                "未映射 AMOADD.D");

    CpuFixture illegal;
    illegal.cpu().state().set_integer(1U, kDataAddress);
    result = illegal.execute(encode_atomic(0x02U, false, 3U, 1U, 2U));
    expect_trap(context,
                result,
                rvemu::core::ExceptionCause::IllegalInstruction,
                encode_atomic(0x02U, false, 3U, 1U, 2U),
                "LR rs2 非零");
    result = illegal.execute(encode_atomic(0x05U, false, 3U, 1U, 2U));
    expect_trap(context,
                result,
                rvemu::core::ExceptionCause::IllegalInstruction,
                encode_atomic(0x05U, false, 3U, 1U, 2U),
                "保留 AMO funct5");

    // funct3=001 不是 A 扩展合法宽度；直接修改真实编码以验证非法分支。
    const auto invalid_width =
        (encode_atomic(0x00U, true, 3U, 1U, 2U) & ~(0x7U << 12U)) | (0x1U << 12U);
    result = illegal.execute(invalid_width);
    expect_trap(context,
                result,
                rvemu::core::ExceptionCause::IllegalInstruction,
                invalid_width,
                "保留 AMO 宽度");
}

void test_misa_declares_a(TestContext& context) {
    CpuFixture fixture;
    const auto misa = fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Misa);
    context.expect((misa & 1U) != 0U, "完整实现后 misa 必须声明 A 扩展");
}

}  // namespace

int main() {
    TestContext context;
    try {
        test_lr_sc_basic(context);
        test_reservation_invalidation(context);
        execute_amo_cases(context, kWordCases, true);
        execute_amo_cases(context, kDoubleCases, false);
        test_amo_aliases(context);
        test_illegal_fault_and_alignment(context);
        test_misa_declares_a(context);
    } catch (const std::exception& error) {
        std::cerr << "测试运行出现未处理异常：" << error.what() << '\n';
        return 2;
    }

    if (context.failures() != 0) {
        std::cerr << "共有 " << context.failures() << " 项断言失败。\n";
        return 1;
    }

    std::cout << "RV64A 全部 LR/SC、AMO、失效、异常与边界测试通过。\n";
    return 0;
}
