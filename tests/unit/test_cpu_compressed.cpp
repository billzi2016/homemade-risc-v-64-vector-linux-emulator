// 文件职责：通过正式 CPU、Bus、RAM 与固定 16 位机器码验证完整 RV64C 2.0 解压执行语义。
// 边界：测试不实现另一套解压器；机器码为固定规范编码，所有结果均来自生产执行路径。

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

class CpuFixture final {
public:
    CpuFixture()
        : ram_(std::make_shared<rvemu::memory::PhysicalMemory>(
              rvemu::bus::PhysicalAddress{kRamBase}, 0x2000U, "compressed-test-ram")),
          cpu_(bus_) {
        const auto registration = bus_.register_region(ram_);
        if (!registration.ok()) {
            throw std::runtime_error("RV64C 测试 RAM 注册失败");
        }
        cpu_.state().reset(kRamBase);
    }

    [[nodiscard]] rvemu::core::Cpu& cpu() noexcept { return cpu_; }

    void enable_floating_state() {
        const auto current = cpu_.state().csrs().peek(rvemu::core::CsrAddress::Mstatus);
        const auto result = cpu_.state().csrs().access(rvemu::core::CsrAccessRequest{
            rvemu::core::CsrAddress::Mstatus,
            rvemu::core::PrivilegeMode::Machine,
            false,
            true,
            rvemu::core::CsrModifyOperation::Replace,
            current | (1ULL << 13U),
        });
        if (!result.success) {
            throw std::runtime_error("RV64C 测试无法启用 mstatus.FS");
        }
    }

    void write(
        std::uint64_t address,
        rvemu::bus::AccessWidth width,
        std::uint64_t value) {
        const auto result = bus_.write(
            rvemu::bus::PhysicalAddress{address},
            width,
            value,
            rvemu::bus::AccessType::Initialization);
        if (!result.ok()) {
            throw std::runtime_error("RV64C 测试内存写入失败");
        }
    }

    [[nodiscard]] std::uint64_t read(
        std::uint64_t address,
        rvemu::bus::AccessWidth width) {
        const auto result = bus_.read(
            rvemu::bus::PhysicalAddress{address}, width, rvemu::bus::AccessType::Load);
        if (!result.ok()) {
            throw std::runtime_error("RV64C 测试内存读取失败");
        }
        return result.value;
    }

    [[nodiscard]] rvemu::core::StepResult execute(std::uint16_t bits) {
        write(kRamBase, rvemu::bus::AccessWidth::HalfWord, bits);
        cpu_.state().set_program_counter(kRamBase);
        return cpu_.step();
    }

private:
    rvemu::bus::Bus bus_{};
    std::shared_ptr<rvemu::memory::PhysicalMemory> ram_;
    rvemu::core::Cpu cpu_;
};

void expect_retired(
    TestContext& context,
    const rvemu::core::StepResult& result,
    const std::string& name) {
    context.expect(result.retired && !result.stalled && !result.trap.has_value(),
                   name + " 必须正常退休");
    context.expect(result.instruction_length == 2U, name + " 必须报告 2 字节长度");
}

void test_immediate_and_register_arithmetic(TestContext& context) {
    CpuFixture fixture;
    fixture.cpu().state().set_integer(2U, 0x1000U);
    auto result = fixture.execute(0x0040U);  // C.ADDI4SPN x8, x2, 4
    expect_retired(context, result, "C.ADDI4SPN");
    context.expect(fixture.cpu().state().integer(8U) == 0x1004U,
                   "C.ADDI4SPN 必须重组 nzuimm 并使用 x2");

    fixture.cpu().state().set_integer(1U, 9U);
    result = fixture.execute(0x0085U);  // C.ADDI x1, 1
    expect_retired(context, result, "C.ADDI");
    context.expect(fixture.cpu().state().integer(1U) == 10U, "C.ADDI 结果错误");

    fixture.cpu().state().set_integer(1U, 0x0000'0000'8000'0000ULL);
    result = fixture.execute(0x2081U);  // C.ADDIW x1, 0
    expect_retired(context, result, "C.ADDIW");
    context.expect(fixture.cpu().state().integer(1U) == 0xFFFF'FFFF'8000'0000ULL,
                   "C.ADDIW 必须把低 32 位符号扩展至 XLEN");

    result = fixture.execute(0x51FDU);  // C.LI x3, -1
    expect_retired(context, result, "C.LI");
    context.expect(fixture.cpu().state().integer(3U) == 0xFFFF'FFFF'FFFF'FFFFULL,
                   "C.LI 必须符号扩展六位立即数");

    result = fixture.execute(0x6185U);  // C.LUI x3, 1
    expect_retired(context, result, "C.LUI");
    context.expect(fixture.cpu().state().integer(3U) == 0x1000U,
                   "C.LUI 必须把立即数放入位 17:12");

    fixture.cpu().state().set_integer(2U, 0x1000U);
    result = fixture.execute(0x6141U);  // C.ADDI16SP x2, 16
    expect_retired(context, result, "C.ADDI16SP");
    context.expect(fixture.cpu().state().integer(2U) == 0x1010U,
                   "C.ADDI16SP 必须正确重组缩放立即数");

    fixture.cpu().state().set_integer(1U, 3U);
    result = fixture.execute(0x0086U);  // C.SLLI x1, 1
    expect_retired(context, result, "C.SLLI");
    context.expect(fixture.cpu().state().integer(1U) == 6U, "C.SLLI 结果错误");

    fixture.cpu().state().set_integer(1U, 1U);
    result = fixture.execute(0x1082U);  // C.SLLI x1, 32（RV64 shamt[5]=1）
    expect_retired(context, result, "C.SLLI shamt[5]");
    context.expect(fixture.cpu().state().integer(1U) == 0x1'0000'0000ULL,
                   "RV64C 必须接受并执行 shamt[5]=1");

    fixture.cpu().state().set_integer(8U, 0x8000'0000'0000'0000ULL);
    result = fixture.execute(0x8005U);  // C.SRLI x8, 1
    expect_retired(context, result, "C.SRLI");
    context.expect(fixture.cpu().state().integer(8U) == 0x4000'0000'0000'0000ULL,
                   "C.SRLI 必须执行逻辑右移");
    fixture.cpu().state().set_integer(8U, 0x8000'0000'0000'0000ULL);
    result = fixture.execute(0x9001U);  // C.SRLI x8, 32
    expect_retired(context, result, "C.SRLI shamt[5]");
    context.expect(fixture.cpu().state().integer(8U) == 0x8000'0000ULL,
                   "C.SRLI 必须正确拼接六位移位量");
    fixture.cpu().state().set_integer(8U, 0x8000'0000'0000'0000ULL);
    result = fixture.execute(0x8405U);  // C.SRAI x8, 1
    expect_retired(context, result, "C.SRAI");
    context.expect(fixture.cpu().state().integer(8U) == 0xC000'0000'0000'0000ULL,
                   "C.SRAI 必须执行算术右移");

    fixture.cpu().state().set_integer(8U, 0x1234'5678'9ABC'DEF0ULL);
    result = fixture.execute(0x987DU);  // C.ANDI x8, -1
    expect_retired(context, result, "C.ANDI");
    context.expect(fixture.cpu().state().integer(8U) == 0x1234'5678'9ABC'DEF0ULL,
                   "C.ANDI 必须符号扩展立即数");
}

void test_register_register_arithmetic(TestContext& context) {
    struct Case final {
        std::uint16_t bits;
        std::uint64_t lhs;
        std::uint64_t rhs;
        std::uint64_t expected;
        const char* name;
    };
    constexpr std::array<Case, 6U> cases{{
        {0x8C05U, 9U, 4U, 5U, "C.SUB"},
        {0x8C25U, 0xAAU, 0x0FU, 0xA5U, "C.XOR"},
        {0x8C45U, 0xA0U, 0x0FU, 0xAFU, "C.OR"},
        {0x8C65U, 0xAAU, 0x0FU, 0x0AU, "C.AND"},
        {0x9C05U, 0x8000'0000U, 1U, 0x0000'0000'7FFF'FFFFULL, "C.SUBW"},
        {0x9C25U, 0x7FFF'FFFFU, 1U, 0xFFFF'FFFF'8000'0000ULL, "C.ADDW"},
    }};
    for (const auto& test : cases) {
        CpuFixture fixture;
        fixture.cpu().state().set_integer(8U, test.lhs);
        fixture.cpu().state().set_integer(9U, test.rhs);
        const auto result = fixture.execute(test.bits);
        expect_retired(context, result, test.name);
        context.expect(fixture.cpu().state().integer(8U) == test.expected,
                       std::string{test.name} + " 结果错误");
    }

    CpuFixture fixture;
    fixture.cpu().state().set_integer(4U, 0x55U);
    auto result = fixture.execute(0x8192U);  // C.MV x3, x4
    expect_retired(context, result, "C.MV");
    context.expect(fixture.cpu().state().integer(3U) == 0x55U, "C.MV 结果错误");
    fixture.cpu().state().set_integer(3U, 0x10U);
    result = fixture.execute(0x9192U);  // C.ADD x3, x4
    expect_retired(context, result, "C.ADD");
    context.expect(fixture.cpu().state().integer(3U) == 0x65U, "C.ADD 结果错误");
}

void test_integer_loads_and_stores(TestContext& context) {
    CpuFixture fixture;
    const auto data = kRamBase + 0x400U;
    fixture.cpu().state().set_integer(8U, data);
    fixture.write(data, rvemu::bus::AccessWidth::Word, 0x8000'0001U);
    auto result = fixture.execute(0x4004U);  // C.LW x9, 0(x8)
    expect_retired(context, result, "C.LW");
    context.expect(fixture.cpu().state().integer(9U) == 0xFFFF'FFFF'8000'0001ULL,
                   "C.LW 必须符号扩展 32 位结果");

    fixture.write(data + 8U, rvemu::bus::AccessWidth::DoubleWord,
                  0x0123'4567'89AB'CDEFULL);
    result = fixture.execute(0x6408U);  // C.LD x10, 8(x8)
    expect_retired(context, result, "C.LD");
    context.expect(fixture.cpu().state().integer(10U) == 0x0123'4567'89AB'CDEFULL,
                   "C.LD 必须使用缩放后的双字偏移");

    result = fixture.execute(0xC044U);  // C.SW x9, 4(x8)
    expect_retired(context, result, "C.SW");
    context.expect(fixture.read(data + 4U, rvemu::bus::AccessWidth::Word) == 0x8000'0001U,
                   "C.SW 必须写入低 32 位");
    result = fixture.execute(0xE408U);  // C.SD x10, 8(x8)
    expect_retired(context, result, "C.SD");
    context.expect(fixture.read(data + 8U, rvemu::bus::AccessWidth::DoubleWord) ==
                       0x0123'4567'89AB'CDEFULL,
                   "C.SD 必须写入完整双字");

    fixture.cpu().state().set_integer(2U, data);
    fixture.write(data + 4U, rvemu::bus::AccessWidth::Word, 0x7FFF'FFFEU);
    result = fixture.execute(0x4192U);  // C.LWSP x3, 4(x2)
    expect_retired(context, result, "C.LWSP");
    context.expect(fixture.cpu().state().integer(3U) == 0x7FFF'FFFEU,
                   "C.LWSP 必须使用栈指针偏移");
    fixture.write(data + 8U, rvemu::bus::AccessWidth::DoubleWord, 0xFEDC'BA98'7654'3210ULL);
    result = fixture.execute(0x61A2U);  // C.LDSP x3, 8(x2)
    expect_retired(context, result, "C.LDSP");
    context.expect(fixture.cpu().state().integer(3U) == 0xFEDC'BA98'7654'3210ULL,
                   "C.LDSP 必须重组双字栈偏移");
    result = fixture.execute(0xC20EU);  // C.SWSP x3, 4(x2)
    expect_retired(context, result, "C.SWSP");
    context.expect(fixture.read(data + 4U, rvemu::bus::AccessWidth::Word) == 0x7654'3210U,
                   "C.SWSP 必须存储寄存器低字");
    result = fixture.execute(0xE40EU);  // C.SDSP x3, 8(x2)
    expect_retired(context, result, "C.SDSP");
    context.expect(fixture.read(data + 8U, rvemu::bus::AccessWidth::DoubleWord) ==
                       0xFEDC'BA98'7654'3210ULL,
                   "C.SDSP 必须存储完整双字");
}

void test_control_flow(TestContext& context) {
    CpuFixture fixture;
    auto result = fixture.execute(0xA009U);  // C.J +2
    expect_retired(context, result, "C.J");
    context.expect(fixture.cpu().state().program_counter() == kRamBase + 2U,
                   "C.J 目标必须相对原始 PC 且允许半字边界");

    result = fixture.execute(0xBFFDU);  // C.J -2
    expect_retired(context, result, "C.J 负偏移");
    context.expect(fixture.cpu().state().program_counter() == kRamBase - 2U,
                   "C.J 必须从位 12 符号扩展完整负偏移");

    fixture.cpu().state().set_integer(3U, kRamBase + 0x22U);
    result = fixture.execute(0x8182U);  // C.JR x3
    expect_retired(context, result, "C.JR");
    context.expect(fixture.cpu().state().program_counter() == kRamBase + 0x22U,
                   "C.JR 必须允许 2 字节对齐目标");

    fixture.cpu().state().set_integer(3U, kRamBase + 0x42U);
    result = fixture.execute(0x9182U);  // C.JALR x3
    expect_retired(context, result, "C.JALR");
    context.expect(fixture.cpu().state().integer(1U) == kRamBase + 2U,
                   "C.JALR 链接地址必须是 PC+2");
    context.expect(fixture.cpu().state().program_counter() == kRamBase + 0x42U,
                   "C.JALR 跳转目标错误");

    fixture.cpu().state().set_integer(8U, 0U);
    result = fixture.execute(0xC009U);  // C.BEQZ x8, +2
    expect_retired(context, result, "C.BEQZ");
    context.expect(fixture.cpu().state().program_counter() == kRamBase + 2U,
                   "C.BEQZ 必须按零条件跳转");
    fixture.cpu().state().set_integer(8U, 0U);
    result = fixture.execute(0xDC7DU);  // C.BEQZ x8, -2
    expect_retired(context, result, "C.BEQZ 负偏移");
    context.expect(fixture.cpu().state().program_counter() == kRamBase - 2U,
                   "C.BEQZ 必须正确符号扩展九位分支偏移");
    fixture.cpu().state().set_integer(8U, 1U);
    result = fixture.execute(0xE009U);  // C.BNEZ x8, +2
    expect_retired(context, result, "C.BNEZ");
    context.expect(fixture.cpu().state().program_counter() == kRamBase + 2U,
                   "C.BNEZ 必须按非零条件跳转");
}

void test_compressed_double_memory(TestContext& context) {
    CpuFixture fixture;
    const auto data = kRamBase + 0x500U;
    fixture.cpu().state().set_integer(8U, data);
    const auto denied = fixture.execute(0x2000U);  // C.FLD f8, 0(x8)
    context.expect(!denied.retired && denied.trap.has_value() &&
                       denied.trap->cause == rvemu::core::ExceptionCause::IllegalInstruction &&
                       denied.trap->value == 0x2000U && denied.trap->instruction == 0x2000U,
                   "FS=Off 的 C.FLD 必须报告原始 16 位非法指令");

    fixture.enable_floating_state();
    fixture.write(data, rvemu::bus::AccessWidth::DoubleWord, 0x4008'0000'0000'0000ULL);
    auto result = fixture.execute(0x2000U);  // C.FLD f8, 0(x8)
    expect_retired(context, result, "C.FLD");
    context.expect(fixture.cpu().state().floating(8U) == 0x4008'0000'0000'0000ULL,
                   "C.FLD 必须复用双精度加载和 FS 状态提交");
    fixture.cpu().state().set_floating(9U, 0xC010'0000'0000'0000ULL);
    result = fixture.execute(0xA004U);  // C.FSD f9, 0(x8)
    expect_retired(context, result, "C.FSD");
    context.expect(fixture.read(data, rvemu::bus::AccessWidth::DoubleWord) ==
                       0xC010'0000'0000'0000ULL,
                   "C.FSD 必须复用双精度存储路径");

    fixture.cpu().state().set_integer(2U, data);
    fixture.write(data + 8U, rvemu::bus::AccessWidth::DoubleWord,
                  0x3FF8'0000'0000'0000ULL);
    result = fixture.execute(0x21A2U);  // C.FLDSP f3, 8(x2)
    expect_retired(context, result, "C.FLDSP");
    context.expect(fixture.cpu().state().floating(3U) == 0x3FF8'0000'0000'0000ULL,
                   "C.FLDSP 必须正确重组栈偏移");
    fixture.cpu().state().set_floating(3U, 0xBFF8'0000'0000'0000ULL);
    result = fixture.execute(0xA40EU);  // C.FSDSP f3, 8(x2)
    expect_retired(context, result, "C.FSDSP");
    context.expect(fixture.read(data + 8U, rvemu::bus::AccessWidth::DoubleWord) ==
                       0xBFF8'0000'0000'0000ULL,
                   "C.FSDSP 必须存储完整双精度位模式");
}

void test_hints_illegal_and_mixed_fetch(TestContext& context) {
    CpuFixture fixture;
    fixture.cpu().state().set_integer(3U, 0x55U);
    constexpr std::array<std::uint16_t, 5U> hints{
        0x0001U,  // C.NOP
        0x0181U,  // C.ADDI x3, 0
        0x4001U,  // C.LI x0, 0
        0x8001U,  // C.SRLI x8, 0
        0x8012U,  // C.MV x0, x4
    };
    for (const auto bits : hints) {
        const auto result = fixture.execute(bits);
        expect_retired(context, result, "RV64C HINT");
        context.expect(fixture.cpu().state().integer(3U) == 0x55U,
                       "被忽略的 HINT 不得修改架构寄存器");
    }

    constexpr std::array<std::uint16_t, 4U> illegal{
        0x0000U,  // 永久非法全零
        0x8000U,  // quadrant 0 保留主 opcode
        0x2001U,  // C.ADDIW rd=x0 保留
        0x6101U,  // C.ADDI16SP nzimm=0 保留
    };
    for (const auto bits : illegal) {
        const auto result = fixture.execute(bits);
        context.expect(!result.retired && result.trap.has_value() &&
                           result.trap->cause == rvemu::core::ExceptionCause::IllegalInstruction,
                       "保留压缩编码必须触发非法指令");
        context.expect(result.trap.has_value() && result.trap->value == bits &&
                           result.trap->instruction == bits,
                       "压缩非法指令 Trap 必须保存原始 16 位编码");
    }

    // 32 位指令从 PC+2 开始跨越两个半字，验证 IALIGN=16 下不会错误要求四字节对齐。
    fixture.write(kRamBase, rvemu::bus::AccessWidth::HalfWord, 0x0001U);
    fixture.write(kRamBase + 2U, rvemu::bus::AccessWidth::Word, 0x0010'8193U);  // ADDI x3,x1,1
    fixture.cpu().state().set_integer(1U, 9U);
    fixture.cpu().state().set_program_counter(kRamBase);
    auto result = fixture.cpu().step();
    expect_retired(context, result, "混合流 C.NOP");
    result = fixture.cpu().step();
    context.expect(result.retired && result.instruction_length == 4U,
                   "半字边界上的 32 位指令必须正常退休并报告四字节");
    context.expect(fixture.cpu().state().integer(3U) == 10U &&
                       fixture.cpu().state().program_counter() == kRamBase + 6U,
                   "16/32 位混合取指必须保持正确 PC 和执行结果");

    result = fixture.execute(0x9002U);  // C.EBREAK
    context.expect(!result.retired && result.trap.has_value() &&
                       result.trap->cause == rvemu::core::ExceptionCause::Breakpoint,
                   "C.EBREAK 必须复用正式 breakpoint Trap");
    context.expect(result.trap.has_value() && result.trap->instruction == 0x9002U,
                   "C.EBREAK 诊断必须保存原始压缩编码");
}

}  // namespace

int main() {
    TestContext context;
    try {
        test_immediate_and_register_arithmetic(context);
        test_register_register_arithmetic(context);
        test_integer_loads_and_stores(context);
        test_control_flow(context);
        test_compressed_double_memory(context);
        test_hints_illegal_and_mixed_fetch(context);
    } catch (const std::exception& error) {
        std::cerr << "RV64C 测试异常：" << error.what() << '\n';
        return 1;
    }
    if (context.failures() != 0) {
        std::cerr << "RV64C 测试失败数：" << context.failures() << '\n';
        return 1;
    }
    std::cout << "RV64C 全指令族、HINT、非法编码、浮点访存和混合取指测试通过。\n";
    return 0;
}
