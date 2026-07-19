// 文件职责：通过正式 CPU、Bus、RAM 和真实机器码验证 RV64F/D 2.2 译码、提交与异常边界。
// 边界：测试只编码指令和断言固定结果，不复制生产软件浮点算法或建立替代执行路径。

#include "rvemu/bus/bus.hpp"
#include "rvemu/core/cpu.hpp"
#include "rvemu/core/csr.hpp"
#include "rvemu/core/floating_state.hpp"
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

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

   private:
    int failures_{0};
};

[[nodiscard]] constexpr std::uint32_t encode_op_fp(std::uint32_t function7,
                                                   std::uint32_t destination,
                                                   std::uint32_t function3,
                                                   std::uint32_t source1,
                                                   std::uint32_t source2) noexcept {
    return ((function7 & 0x7FU) << 25U) | ((source2 & 0x1FU) << 20U) | ((source1 & 0x1FU) << 15U)
           | ((function3 & 0x7U) << 12U) | ((destination & 0x1FU) << 7U) | 0x53U;
}

[[nodiscard]] constexpr std::uint32_t encode_fma(std::uint32_t opcode,
                                                 bool double_precision,
                                                 std::uint32_t destination,
                                                 std::uint32_t rounding,
                                                 std::uint32_t source1,
                                                 std::uint32_t source2,
                                                 std::uint32_t source3) noexcept {
    return ((source3 & 0x1FU) << 27U) | ((double_precision ? 1U : 0U) << 25U)
           | ((source2 & 0x1FU) << 20U) | ((source1 & 0x1FU) << 15U) | ((rounding & 0x7U) << 12U)
           | ((destination & 0x1FU) << 7U) | (opcode & 0x7FU);
}

[[nodiscard]] constexpr std::uint32_t encode_fp_load(std::uint32_t destination,
                                                     std::uint32_t base,
                                                     std::uint32_t function3,
                                                     std::uint32_t immediate) noexcept {
    return ((immediate & 0xFFFU) << 20U) | ((base & 0x1FU) << 15U) | ((function3 & 0x7U) << 12U)
           | ((destination & 0x1FU) << 7U) | 0x07U;
}

[[nodiscard]] constexpr std::uint32_t encode_fp_store(std::uint32_t source,
                                                      std::uint32_t base,
                                                      std::uint32_t function3,
                                                      std::uint32_t immediate) noexcept {
    return (((immediate >> 5U) & 0x7FU) << 25U) | ((source & 0x1FU) << 20U)
           | ((base & 0x1FU) << 15U) | ((function3 & 0x7U) << 12U) | ((immediate & 0x1FU) << 7U)
           | 0x27U;
}

class CpuFixture final {
   public:
    CpuFixture()
        : ram_(std::make_shared<rvemu::memory::PhysicalMemory>(
              rvemu::bus::PhysicalAddress{kRamBase}, 0x2000U, "floating-test-ram")),
          cpu_(bus_) {
        const auto registration = bus_.register_region(ram_);
        if (!registration.ok()) {
            throw std::runtime_error("浮点测试 RAM 注册失败");
        }
        cpu_.state().reset(kRamBase);
    }

    [[nodiscard]] rvemu::core::Cpu& cpu() noexcept {
        return cpu_;
    }

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
            throw std::runtime_error("浮点测试无法启用 mstatus.FS");
        }
    }

    [[nodiscard]] rvemu::core::StepResult execute(std::uint32_t bits) {
        const auto written = bus_.write(rvemu::bus::PhysicalAddress{kRamBase},
                                        rvemu::bus::AccessWidth::Word,
                                        bits,
                                        rvemu::bus::AccessType::Initialization);
        if (!written.ok()) {
            throw std::runtime_error("浮点测试机器码写入失败");
        }
        cpu_.state().set_program_counter(kRamBase);
        return cpu_.step();
    }

    void write_data(std::uint64_t offset, rvemu::bus::AccessWidth width, std::uint64_t value) {
        const auto written = bus_.write(rvemu::bus::PhysicalAddress{kRamBase + offset},
                                        width,
                                        value,
                                        rvemu::bus::AccessType::Initialization);
        if (!written.ok()) {
            throw std::runtime_error("浮点测试数据写入失败");
        }
    }

    [[nodiscard]] std::uint64_t read_data(std::uint64_t offset, rvemu::bus::AccessWidth width) {
        const auto read = bus_.read(
            rvemu::bus::PhysicalAddress{kRamBase + offset}, width, rvemu::bus::AccessType::Load);
        if (!read.ok()) {
            throw std::runtime_error("浮点测试数据读取失败");
        }
        return read.value;
    }

   private:
    rvemu::bus::Bus bus_{};
    std::shared_ptr<rvemu::memory::PhysicalMemory> ram_;
    rvemu::core::Cpu cpu_;
};

void expect_retired(TestContext& context,
                    const rvemu::core::StepResult& result,
                    const std::string& name) {
    context.expect(result.retired && !result.stalled && !result.trap.has_value(),
                   name + " 必须正常退休");
}

void test_state_gate_and_memory(TestContext& context) {
    CpuFixture fixture;
    fixture.cpu().state().set_integer(1U, kRamBase + 0x100U);
    const auto denied = fixture.execute(encode_fp_load(2U, 1U, 2U, 0U));
    context.expect(!denied.retired && denied.trap.has_value()
                       && denied.trap->cause == rvemu::core::ExceptionCause::IllegalInstruction,
                   "FS=Off 必须在浮点加载访问总线前触发非法指令");

    fixture.enable_floating_state();
    fixture.write_data(0x100U, rvemu::bus::AccessWidth::Word, 0x7FC0'1234U);
    auto result = fixture.execute(encode_fp_load(2U, 1U, 2U, 0U));
    expect_retired(context, result, "FLW");
    context.expect(fixture.cpu().state().floating(2U) == 0xFFFF'FFFF'7FC0'1234ULL,
                   "FLW 必须保留 NaN payload 并执行 NaN boxing");

    fixture.write_data(0x108U, rvemu::bus::AccessWidth::DoubleWord, 0x4008'0000'0000'0000ULL);
    result = fixture.execute(encode_fp_load(3U, 1U, 3U, 8U));
    expect_retired(context, result, "FLD");
    context.expect(fixture.cpu().state().floating(3U) == 0x4008'0000'0000'0000ULL,
                   "FLD 必须原样传输 64 位位模式");

    fixture.cpu().state().set_floating(4U, 0x1234'5678'89AB'CDEFULL);
    result = fixture.execute(encode_fp_store(4U, 1U, 2U, 0x10U));
    expect_retired(context, result, "FSW");
    context.expect(fixture.read_data(0x110U, rvemu::bus::AccessWidth::Word) == 0x89AB'CDEFU,
                   "FSW 不得检查 NaN box，必须存储原始低 32 位");
    result = fixture.execute(encode_fp_store(4U, 1U, 3U, 0x18U));
    expect_retired(context, result, "FSD");
    context.expect(
        fixture.read_data(0x118U, rvemu::bus::AccessWidth::DoubleWord) == 0x1234'5678'89AB'CDEFULL,
        "FSD 必须存储完整 64 位位模式");
}

void test_arithmetic_and_fma_encodings(TestContext& context) {
    CpuFixture fixture;
    fixture.enable_floating_state();
    fixture.cpu().state().set_floating_single(1U, 0x40C0'0000U);  // 6
    fixture.cpu().state().set_floating_single(2U, 0x4000'0000U);  // 2

    struct ArithmeticCase final {
        std::uint8_t function7;
        std::uint64_t expected;
        const char* name;
    };
    constexpr std::array<ArithmeticCase, 5U> cases{{
        {0x00U, 0x4100'0000U, "FADD.S"},
        {0x04U, 0x4080'0000U, "FSUB.S"},
        {0x08U, 0x4140'0000U, "FMUL.S"},
        {0x0CU, 0x4040'0000U, "FDIV.S"},
        {0x2CU, 0x4000'0000U, "FSQRT.S"},
    }};
    for (const auto& test : cases) {
        const auto source2 = test.function7 == 0x2CU ? 0U : 2U;
        const auto source1 = test.function7 == 0x2CU ? 0x4080'0000U : 0x40C0'0000U;
        fixture.cpu().state().set_floating_single(1U, static_cast<std::uint32_t>(source1));
        const auto result = fixture.execute(encode_op_fp(test.function7, 5U, 0U, 1U, source2));
        expect_retired(context, result, test.name);
        context.expect(fixture.cpu().state().floating_single(5U) == test.expected,
                       std::string{test.name} + " 结果错误");
    }

    struct DoubleArithmeticCase final {
        std::uint8_t function7;
        std::uint64_t lhs;
        std::uint64_t expected;
        const char* name;
    };
    constexpr std::array<DoubleArithmeticCase, 5U> double_cases{{
        {0x01U, 0x4018'0000'0000'0000ULL, 0x4020'0000'0000'0000ULL, "FADD.D"},
        {0x05U, 0x4018'0000'0000'0000ULL, 0x4010'0000'0000'0000ULL, "FSUB.D"},
        {0x09U, 0x4018'0000'0000'0000ULL, 0x4028'0000'0000'0000ULL, "FMUL.D"},
        {0x0DU, 0x4018'0000'0000'0000ULL, 0x4008'0000'0000'0000ULL, "FDIV.D"},
        {0x2DU, 0x4010'0000'0000'0000ULL, 0x4000'0000'0000'0000ULL, "FSQRT.D"},
    }};
    fixture.cpu().state().set_floating(2U, 0x4000'0000'0000'0000ULL);  // 2
    for (const auto& test : double_cases) {
        fixture.cpu().state().set_floating(1U, test.lhs);
        const auto source2 = test.function7 == 0x2DU ? 0U : 2U;
        const auto result = fixture.execute(encode_op_fp(test.function7, 6U, 0U, 1U, source2));
        expect_retired(context, result, test.name);
        context.expect(fixture.cpu().state().floating(6U) == test.expected,
                       std::string{test.name} + " 结果错误");
    }

    fixture.cpu().state().set_floating_single(1U, 0x4000'0000U);
    fixture.cpu().state().set_floating_single(2U, 0x4040'0000U);
    fixture.cpu().state().set_floating_single(3U, 0x4080'0000U);
    constexpr std::array<std::uint32_t, 4U> opcodes{0x43U, 0x47U, 0x4BU, 0x4FU};
    constexpr std::array<std::uint32_t, 4U> expected{
        0x4120'0000U, 0x4000'0000U, 0xC000'0000U, 0xC120'0000U};
    for (std::size_t index = 0U; index < opcodes.size(); ++index) {
        const auto result = fixture.execute(encode_fma(opcodes[index], false, 7U, 0U, 1U, 2U, 3U));
        expect_retired(context, result, "FMA 编码族");
        context.expect(fixture.cpu().state().floating_single(7U) == expected[index],
                       "四种 FMA opcode 的符号组合必须正确");
    }

    fixture.cpu().state().set_floating(1U, 0x4000'0000'0000'0000ULL);
    fixture.cpu().state().set_floating(2U, 0x4008'0000'0000'0000ULL);
    fixture.cpu().state().set_floating(3U, 0x4010'0000'0000'0000ULL);
    constexpr std::array<std::uint64_t, 4U> expected_double{
        0x4024'0000'0000'0000ULL,
        0x4000'0000'0000'0000ULL,
        0xC000'0000'0000'0000ULL,
        0xC024'0000'0000'0000ULL,
    };
    for (std::size_t index = 0U; index < opcodes.size(); ++index) {
        const auto result = fixture.execute(encode_fma(opcodes[index], true, 7U, 0U, 1U, 2U, 3U));
        expect_retired(context, result, "双精度 FMA 编码族");
        context.expect(fixture.cpu().state().floating(7U) == expected_double[index],
                       "四种双精度 FMA opcode 的符号组合必须正确");
    }
}

void test_miscellaneous_and_conversion_encodings(TestContext& context) {
    CpuFixture fixture;
    fixture.enable_floating_state();
    fixture.cpu().state().set_floating_single(1U, 0x3F80'0000U);
    fixture.cpu().state().set_floating_single(2U, 0xC000'0000U);

    constexpr std::array<std::uint32_t, 3U> sign_expected{0xBF80'0000U, 0x3F80'0000U, 0xBF80'0000U};
    for (std::uint32_t operation = 0U; operation < 3U; ++operation) {
        const auto result = fixture.execute(encode_op_fp(0x10U, 3U, operation, 1U, 2U));
        expect_retired(context, result, "FSGNJ 编码族");
        context.expect(fixture.cpu().state().floating_single(3U) == sign_expected[operation],
                       "FSGNJ/N/X 结果错误");
    }

    auto result = fixture.execute(encode_op_fp(0x14U, 3U, 0U, 1U, 2U));
    expect_retired(context, result, "FMIN.S");
    context.expect(fixture.cpu().state().floating_single(3U) == 0xC000'0000U, "FMIN.S 必须选出 -2");
    result = fixture.execute(encode_op_fp(0x14U, 3U, 1U, 1U, 2U));
    expect_retired(context, result, "FMAX.S");
    context.expect(fixture.cpu().state().floating_single(3U) == 0x3F80'0000U, "FMAX.S 必须选出 1");

    constexpr std::array<std::uint64_t, 3U> compare_expected{0U, 0U, 0U};
    for (std::uint32_t function3 = 0U; function3 < 3U; ++function3) {
        result = fixture.execute(encode_op_fp(0x50U, 4U, function3, 1U, 2U));
        expect_retired(context, result, "FLE/FLT/FEQ.S");
        context.expect(fixture.cpu().state().integer(4U) == compare_expected[function3],
                       "比较编码结果错误");
    }
    result = fixture.execute(encode_op_fp(0x70U, 4U, 1U, 1U, 0U));
    expect_retired(context, result, "FCLASS.S");
    context.expect(fixture.cpu().state().integer(4U) == (1U << 6U), "FCLASS.S 必须识别正正规数");

    fixture.cpu().state().set_floating_single(1U, 0x3FC0'0000U);
    result = fixture.execute(encode_op_fp(0x60U, 5U, 0U, 1U, 0U));
    expect_retired(context, result, "FCVT.W.S");
    context.expect(fixture.cpu().state().integer(5U) == 2U, "FCVT.W.S RNE 必须把 1.5 舍为 2");
    fixture.cpu().state().set_integer(6U, 0xFFFF'FFFF'FFFF'FFFEULL);
    result = fixture.execute(encode_op_fp(0x68U, 7U, 0U, 6U, 2U));
    expect_retired(context, result, "FCVT.S.L");
    context.expect(fixture.cpu().state().floating_single(7U) == 0xC000'0000U,
                   "FCVT.S.L 必须转换 -2");

    result = fixture.execute(encode_op_fp(0x21U, 8U, 0U, 1U, 0U));
    expect_retired(context, result, "FCVT.D.S");
    context.expect(fixture.cpu().state().floating(8U) == 0x3FF8'0000'0000'0000ULL,
                   "FCVT.D.S 必须精确拓宽 1.5");
    result = fixture.execute(encode_op_fp(0x20U, 9U, 0U, 8U, 1U));
    expect_retired(context, result, "FCVT.S.D");
    context.expect(fixture.cpu().state().floating_single(9U) == 0x3FC0'0000U,
                   "FCVT.S.D 必须还原 1.5");

    // rs2=0..3 分别选择 W/WU/L/LU；使用正数 2 可在同一固定期望下验证八种源整数编码。
    fixture.cpu().state().set_integer(6U, 2U);
    for (std::uint32_t source_kind = 0U; source_kind < 4U; ++source_kind) {
        result = fixture.execute(encode_op_fp(0x68U, 7U, 0U, 6U, source_kind));
        expect_retired(context, result, "FCVT.S.[W/WU/L/LU]");
        context.expect(fixture.cpu().state().floating_single(7U) == 0x4000'0000U,
                       "四种整数来源转单精度编码必须都被接受");
        result = fixture.execute(encode_op_fp(0x69U, 8U, 0U, 6U, source_kind));
        expect_retired(context, result, "FCVT.D.[W/WU/L/LU]");
        context.expect(fixture.cpu().state().floating(8U) == 0x4000'0000'0000'0000ULL,
                       "四种整数来源转双精度编码必须都被接受");
    }

    fixture.cpu().state().set_floating_single(1U, 0x4000'0000U);
    fixture.cpu().state().set_floating(2U, 0x4000'0000'0000'0000ULL);
    for (std::uint32_t destination_kind = 0U; destination_kind < 4U; ++destination_kind) {
        result = fixture.execute(encode_op_fp(0x60U, 5U, 0U, 1U, destination_kind));
        expect_retired(context, result, "FCVT.[W/WU/L/LU].S");
        context.expect(fixture.cpu().state().integer(5U) == 2U,
                       "单精度转四种整数目标编码必须都被接受");
        result = fixture.execute(encode_op_fp(0x61U, 5U, 0U, 2U, destination_kind));
        expect_retired(context, result, "FCVT.[W/WU/L/LU].D");
        context.expect(fixture.cpu().state().integer(5U) == 2U,
                       "双精度转四种整数目标编码必须都被接受");
    }

    fixture.cpu().state().set_integer(10U, 0x89AB'CDEFU);
    result = fixture.execute(encode_op_fp(0x78U, 11U, 0U, 10U, 0U));
    expect_retired(context, result, "FMV.W.X");
    context.expect(fixture.cpu().state().floating(11U) == 0xFFFF'FFFF'89AB'CDEFULL,
                   "FMV.W.X 必须写入合法 NaN box");
    result = fixture.execute(encode_op_fp(0x70U, 12U, 0U, 11U, 0U));
    expect_retired(context, result, "FMV.X.W");
    context.expect(fixture.cpu().state().integer(12U) == 0xFFFF'FFFF'89AB'CDEFULL,
                   "FMV.X.W 必须符号扩展原始低 32 位");

    fixture.cpu().state().set_integer(10U, 0x0123'4567'89AB'CDEFULL);
    result = fixture.execute(encode_op_fp(0x79U, 11U, 0U, 10U, 0U));
    expect_retired(context, result, "FMV.D.X");
    context.expect(fixture.cpu().state().floating(11U) == 0x0123'4567'89AB'CDEFULL,
                   "FMV.D.X 必须传输完整 XLEN 位模式");
    result = fixture.execute(encode_op_fp(0x71U, 12U, 0U, 11U, 0U));
    expect_retired(context, result, "FMV.X.D");
    context.expect(fixture.cpu().state().integer(12U) == 0x0123'4567'89AB'CDEFULL,
                   "FMV.X.D 必须传输完整 64 位位模式");
}

void test_illegal_and_nan_box_paths(TestContext& context) {
    CpuFixture fixture;
    fixture.enable_floating_state();
    fixture.cpu().state().set_floating_single(1U, 0x3F80'0000U);
    fixture.cpu().state().set_floating_single(2U, 0x3F80'0000U);
    fixture.cpu().state().set_floating_single(3U, 0xDEAD'BEEFU);
    const auto flags_before = fixture.cpu().state().csrs().floating_exception_flags();
    const auto invalid_rm = fixture.execute(encode_op_fp(0x00U, 3U, 5U, 1U, 2U));
    context.expect(!invalid_rm.retired && invalid_rm.trap.has_value()
                       && invalid_rm.trap->cause == rvemu::core::ExceptionCause::IllegalInstruction,
                   "保留静态 rm 必须触发非法指令");
    context.expect(fixture.cpu().state().floating_single(3U) == 0xDEAD'BEEFU
                       && fixture.cpu().state().csrs().floating_exception_flags() == flags_before,
                   "非法 rm 不得修改目标寄存器或 fflags");

    fixture.cpu().state().set_floating(1U, 0x0000'0000'3F80'0000ULL);
    const auto invalid_box = fixture.execute(encode_op_fp(0x00U, 3U, 0U, 1U, 2U));
    expect_retired(context, invalid_box, "非法 NaN box 计算输入");
    context.expect(fixture.cpu().state().floating(3U) == 0xFFFF'FFFF'7FC0'0000ULL,
                   "非法单精度 NaN box 必须作为 canonical qNaN 参与计算");

    const auto reserved_format =
        fixture.execute(encode_fma(0x43U, false, 3U, 0U, 1U, 2U, 3U) | (2U << 25U));
    context.expect(
        !reserved_format.retired && reserved_format.trap.has_value()
            && reserved_format.trap->cause == rvemu::core::ExceptionCause::IllegalInstruction,
        "FMA 保留 fmt 必须触发非法指令");
}

}  // namespace

int main() {
    TestContext context;
    try {
        test_state_gate_and_memory(context);
        test_arithmetic_and_fma_encodings(context);
        test_miscellaneous_and_conversion_encodings(context);
        test_illegal_and_nan_box_paths(context);
    } catch (const std::exception& error) {
        std::cerr << "CPU F/D 测试异常：" << error.what() << '\n';
        return 1;
    }
    if (context.failures() != 0) {
        std::cerr << "CPU F/D 测试失败数：" << context.failures() << '\n';
        return 1;
    }
    std::cout << "RV64F/D 机器码译码、执行、访存与非法路径测试通过。\n";
    return 0;
}
