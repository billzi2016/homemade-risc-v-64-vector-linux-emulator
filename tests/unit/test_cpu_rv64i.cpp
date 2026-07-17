// 文件职责：让生产 CPU 通过真实 Bus/RAM 执行 RV64I 机器码，验证全部基础指令族和精确异常。
// 边界：本文件只组装测试输入，不定义 Mock 总线、替代译码器或第二套执行语义。

#include "rvemu/bus/bus.hpp"
#include "rvemu/core/cpu.hpp"
#include "rvemu/memory/physical_memory.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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

[[nodiscard]] constexpr std::uint32_t encode_r(
    std::uint32_t opcode,
    std::uint32_t destination,
    std::uint32_t function3,
    std::uint32_t source1,
    std::uint32_t source2,
    std::uint32_t function7) noexcept {
    return ((function7 & 0x7FU) << 25U) | ((source2 & 0x1FU) << 20U) |
           ((source1 & 0x1FU) << 15U) | ((function3 & 0x07U) << 12U) |
           ((destination & 0x1FU) << 7U) | (opcode & 0x7FU);
}

[[nodiscard]] constexpr std::uint32_t encode_i(
    std::uint32_t opcode,
    std::uint32_t destination,
    std::uint32_t function3,
    std::uint32_t source1,
    std::uint32_t immediate12) noexcept {
    return ((immediate12 & 0xFFFU) << 20U) | ((source1 & 0x1FU) << 15U) |
           ((function3 & 0x07U) << 12U) | ((destination & 0x1FU) << 7U) |
           (opcode & 0x7FU);
}

[[nodiscard]] constexpr std::uint32_t encode_s(
    std::uint32_t function3,
    std::uint32_t source1,
    std::uint32_t source2,
    std::uint32_t immediate12) noexcept {
    const auto upper = (immediate12 >> 5U) & 0x7FU;
    const auto lower = immediate12 & 0x1FU;
    return (upper << 25U) | ((source2 & 0x1FU) << 20U) | ((source1 & 0x1FU) << 15U) |
           ((function3 & 0x07U) << 12U) | (lower << 7U) | 0x23U;
}

[[nodiscard]] constexpr std::uint32_t encode_b(
    std::uint32_t function3,
    std::uint32_t source1,
    std::uint32_t source2,
    std::uint32_t immediate13) noexcept {
    const auto bit12 = (immediate13 >> 12U) & 0x01U;
    const auto bit11 = (immediate13 >> 11U) & 0x01U;
    const auto bits10_5 = (immediate13 >> 5U) & 0x3FU;
    const auto bits4_1 = (immediate13 >> 1U) & 0x0FU;
    return (bit12 << 31U) | (bits10_5 << 25U) | ((source2 & 0x1FU) << 20U) |
           ((source1 & 0x1FU) << 15U) | ((function3 & 0x07U) << 12U) |
           (bits4_1 << 8U) | (bit11 << 7U) | 0x63U;
}

[[nodiscard]] constexpr std::uint32_t encode_u(
    std::uint32_t opcode,
    std::uint32_t destination,
    std::uint32_t upper20) noexcept {
    return ((upper20 & 0xFFFFFU) << 12U) | ((destination & 0x1FU) << 7U) |
           (opcode & 0x7FU);
}

[[nodiscard]] constexpr std::uint32_t encode_j(
    std::uint32_t destination,
    std::uint32_t immediate21) noexcept {
    const auto bit20 = (immediate21 >> 20U) & 0x01U;
    const auto bits19_12 = (immediate21 >> 12U) & 0xFFU;
    const auto bit11 = (immediate21 >> 11U) & 0x01U;
    const auto bits10_1 = (immediate21 >> 1U) & 0x3FFU;
    return (bit20 << 31U) | (bits10_1 << 21U) | (bit11 << 20U) |
           (bits19_12 << 12U) | ((destination & 0x1FU) << 7U) | 0x6FU;
}

class CpuFixture final {
public:
    explicit CpuFixture(std::size_t ram_size = 0x2000U)
        : ram_(std::make_shared<rvemu::memory::PhysicalMemory>(
              rvemu::bus::PhysicalAddress{kRamBase},
              ram_size,
              "cpu-test-ram")),
          cpu_(bus_) {
        const auto registration = bus_.register_region(ram_);
        if (!registration.ok()) {
            throw std::runtime_error("CPU 测试 RAM 注册失败");
        }
        cpu_.state().reset(kRamBase);
    }

    [[nodiscard]] rvemu::core::Cpu& cpu() noexcept { return cpu_; }
    [[nodiscard]] rvemu::bus::Bus& bus() noexcept { return bus_; }

    void write_instruction(std::uint32_t bits, std::uint64_t address = kRamBase) {
        const auto result = bus_.write(
            rvemu::bus::PhysicalAddress{address},
            rvemu::bus::AccessWidth::Word,
            bits,
            rvemu::bus::AccessType::Initialization);
        if (!result.ok()) {
            throw std::runtime_error("写入 CPU 测试指令失败");
        }
    }

    void write_halfword(std::uint16_t bits, std::uint64_t address = kRamBase) {
        const auto result = bus_.write(
            rvemu::bus::PhysicalAddress{address},
            rvemu::bus::AccessWidth::HalfWord,
            bits,
            rvemu::bus::AccessType::Initialization);
        if (!result.ok()) {
            throw std::runtime_error("写入 CPU 测试半字失败");
        }
    }

    [[nodiscard]] rvemu::core::StepResult execute(
        std::uint32_t bits,
        std::uint64_t address = kRamBase) {
        write_instruction(bits, address);
        cpu_.state().set_program_counter(address);
        return cpu_.step();
    }

private:
    rvemu::bus::Bus bus_{};
    std::shared_ptr<rvemu::memory::PhysicalMemory> ram_;
    rvemu::core::Cpu cpu_;
};

void expect_retired(TestContext& context, const rvemu::core::StepResult& result, const std::string& name) {
    context.expect(result.retired, name + " 应成功退休");
    context.expect(!result.trap.has_value(), name + " 不应产生 Trap");
}

void expect_trap(
    TestContext& context,
    const rvemu::core::StepResult& result,
    rvemu::core::ExceptionCause cause,
    const std::string& name) {
    context.expect(!result.retired, name + " 不得退休");
    context.expect(result.trap.has_value(), name + " 必须产生 Trap");
    if (result.trap.has_value()) {
        context.expect(result.trap->cause == cause, name + " Trap cause 不正确");
    }
}

void test_architectural_state(TestContext& context) {
    rvemu::core::CpuState state;
    state.reset(0x1234U);
    state.set_integer(0U, 0xFFFFU);
    state.set_integer(31U, 0x1122'3344'5566'7788ULL);
    context.expect(state.integer(0U) == 0U, "x0 必须硬编码为零");
    context.expect(state.integer(31U) == 0x1122'3344'5566'7788ULL, "x31 必须保存 64 位值");

    state.set_floating(31U, 0x7FF8'0000'0000'0001ULL);
    context.expect(state.floating(31U) == 0x7FF8'0000'0000'0001ULL, "浮点寄存器必须保存 64 位状态");

    rvemu::core::CpuState::VectorRegister vector{};
    vector[0] = 0xAAU;
    vector[31] = 0x55U;
    state.set_vector(31U, vector);
    context.expect(state.vector(31U)[0] == 0xAAU && state.vector(31U)[31] == 0x55U, "向量寄存器必须保存 256 位状态");
    context.expect(state.program_counter() == 0x1234U, "reset 必须设置指定 PC");
    context.expect(state.privilege() == rvemu::core::PrivilegeMode::Machine, "reset 必须进入 M-mode");
}

void test_fetch_and_precise_faults(TestContext& context) {
    CpuFixture fixture;
    fixture.write_halfword(0x0001U);
    fixture.cpu().state().set_program_counter(kRamBase);
    const auto compressed = fixture.cpu().step();
    expect_trap(context, compressed, rvemu::core::ExceptionCause::IllegalInstruction, "未实现压缩指令");
    context.expect(compressed.instruction_length == 2U, "压缩编码必须识别为 2 字节");
    context.expect(fixture.cpu().state().program_counter() == kRamBase, "非法压缩指令不得推进 PC");

    fixture.cpu().state().set_program_counter(kRamBase + 1U);
    const auto misaligned = fixture.cpu().step();
    expect_trap(context, misaligned, rvemu::core::ExceptionCause::InstructionAddressMisaligned, "奇地址取指");

    fixture.cpu().state().set_program_counter(0x7000'0000U);
    const auto unmapped = fixture.cpu().step();
    expect_trap(context, unmapped, rvemu::core::ExceptionCause::InstructionAccessFault, "未映射取指");

    CpuFixture split_fixture{2U};
    split_fixture.write_halfword(0x0003U);
    const auto split = split_fixture.cpu().step();
    expect_trap(context, split, rvemu::core::ExceptionCause::InstructionAccessFault, "32 位指令第二半字越界");
    if (split.trap.has_value()) {
        context.expect(split.trap->value == kRamBase + 2U, "跨边界取指 tval 必须指向失败半字");
    }
}

void test_upper_and_control_flow(TestContext& context) {
    CpuFixture fixture;
    auto result = fixture.execute(encode_u(0x37U, 1U, 0xFFFFFU));
    expect_retired(context, result, "LUI");
    context.expect(fixture.cpu().state().integer(1U) == 0xFFFF'FFFF'FFFF'F000ULL, "LUI 必须符号扩展 32 位结果");

    result = fixture.execute(encode_u(0x17U, 2U, 0x1U));
    expect_retired(context, result, "AUIPC");
    context.expect(fixture.cpu().state().integer(2U) == kRamBase + 0x1000U, "AUIPC 必须相对原 PC 计算");

    result = fixture.execute(encode_j(3U, 8U));
    expect_retired(context, result, "JAL");
    context.expect(fixture.cpu().state().integer(3U) == kRamBase + 4U, "JAL 链接地址必须为下一指令");
    context.expect(fixture.cpu().state().program_counter() == kRamBase + 8U, "JAL 必须跳转到 PC 相对目标");

    fixture.cpu().state().set_integer(4U, kRamBase + 9U);
    result = fixture.execute(encode_i(0x67U, 5U, 0U, 4U, 0U));
    expect_retired(context, result, "JALR");
    context.expect(fixture.cpu().state().integer(5U) == kRamBase + 4U, "JALR 链接地址必须正确");
    context.expect(fixture.cpu().state().program_counter() == kRamBase + 8U, "JALR 必须清除目标最低位");
}

void test_op_imm(TestContext& context) {
    struct Case final {
        const char* name;
        std::uint32_t bits;
        std::uint64_t source;
        std::uint64_t expected;
    };
    const std::array<Case, 9U> cases{{
        {"ADDI", encode_i(0x13U, 2U, 0U, 1U, 0xFFFU), 1U, 0U},
        {"SLTI", encode_i(0x13U, 2U, 2U, 1U, 1U), 0xFFFF'FFFF'FFFF'FFFFULL, 1U},
        {"SLTIU", encode_i(0x13U, 2U, 3U, 1U, 0xFFFU), 0U, 1U},
        {"XORI", encode_i(0x13U, 2U, 4U, 1U, 0x0FFU), 0xF0U, 0x0FU},
        {"ORI", encode_i(0x13U, 2U, 6U, 1U, 0x00FU), 0xF0U, 0xFFU},
        {"ANDI", encode_i(0x13U, 2U, 7U, 1U, 0x00FU), 0xFFU, 0x0FU},
        {"SLLI", encode_i(0x13U, 2U, 1U, 1U, 63U), 1U, 1ULL << 63U},
        {"SRLI", encode_i(0x13U, 2U, 5U, 1U, 63U), 1ULL << 63U, 1U},
        {"SRAI", encode_i(0x13U, 2U, 5U, 1U, (0x10U << 6U) | 4U), 0x8000'0000'0000'0000ULL, 0xF800'0000'0000'0000ULL},
    }};

    for (const auto& item : cases) {
        CpuFixture fixture;
        fixture.cpu().state().set_integer(1U, item.source);
        const auto result = fixture.execute(item.bits);
        expect_retired(context, result, item.name);
        context.expect(fixture.cpu().state().integer(2U) == item.expected, std::string{item.name} + " 结果错误");
    }

    CpuFixture invalid;
    const auto invalid_shift = invalid.execute(encode_i(0x13U, 2U, 1U, 1U, (1U << 6U) | 1U));
    expect_trap(context, invalid_shift, rvemu::core::ExceptionCause::IllegalInstruction, "保留 SLLI 编码");
}

void test_op_register(TestContext& context) {
    struct Case final {
        const char* name;
        std::uint32_t function3;
        std::uint32_t function7;
        std::uint64_t lhs;
        std::uint64_t rhs;
        std::uint64_t expected;
    };
    const std::array<Case, 10U> cases{{
        {"ADD", 0U, 0U, 5U, 7U, 12U},
        {"SUB", 0U, 0x20U, 5U, 7U, 0xFFFF'FFFF'FFFF'FFFEULL},
        {"SLL", 1U, 0U, 1U, 65U, 2U},
        {"SLT", 2U, 0U, 0xFFFF'FFFF'FFFF'FFFFULL, 1U, 1U},
        {"SLTU", 3U, 0U, 1U, 2U, 1U},
        {"XOR", 4U, 0U, 0xF0U, 0x0FU, 0xFFU},
        {"SRL", 5U, 0U, 0x8000'0000'0000'0000ULL, 63U, 1U},
        {"SRA", 5U, 0x20U, 0x8000'0000'0000'0000ULL, 4U, 0xF800'0000'0000'0000ULL},
        {"OR", 6U, 0U, 0xF0U, 0x0FU, 0xFFU},
        {"AND", 7U, 0U, 0xF0U, 0x3FU, 0x30U},
    }};

    for (const auto& item : cases) {
        CpuFixture fixture;
        fixture.cpu().state().set_integer(1U, item.lhs);
        fixture.cpu().state().set_integer(2U, item.rhs);
        const auto bits = encode_r(0x33U, 3U, item.function3, 1U, 2U, item.function7);
        const auto result = fixture.execute(bits);
        expect_retired(context, result, item.name);
        context.expect(fixture.cpu().state().integer(3U) == item.expected, std::string{item.name} + " 结果错误");
    }
}

void test_word_operations(TestContext& context) {
    struct Case final {
        const char* name;
        std::uint32_t bits;
        std::uint64_t lhs;
        std::uint64_t rhs;
        std::uint64_t expected;
    };
    const std::array<Case, 9U> cases{{
        {"ADDIW", encode_i(0x1BU, 3U, 0U, 1U, 1U), 0x7FFF'FFFFU, 0U, 0xFFFF'FFFF'8000'0000ULL},
        {"SLLIW", encode_i(0x1BU, 3U, 1U, 1U, 31U), 1U, 0U, 0xFFFF'FFFF'8000'0000ULL},
        {"SRLIW", encode_i(0x1BU, 3U, 5U, 1U, 31U), 0x8000'0000U, 0U, 1U},
        {"SRAIW", encode_i(0x1BU, 3U, 5U, 1U, (0x20U << 5U) | 4U), 0x8000'0000U, 0U, 0xFFFF'FFFF'F800'0000ULL},
        {"ADDW", encode_r(0x3BU, 3U, 0U, 1U, 2U, 0U), 0x7FFF'FFFFU, 1U, 0xFFFF'FFFF'8000'0000ULL},
        {"SUBW", encode_r(0x3BU, 3U, 0U, 1U, 2U, 0x20U), 0U, 1U, 0xFFFF'FFFF'FFFF'FFFFULL},
        {"SLLW", encode_r(0x3BU, 3U, 1U, 1U, 2U, 0U), 1U, 31U, 0xFFFF'FFFF'8000'0000ULL},
        {"SRLW", encode_r(0x3BU, 3U, 5U, 1U, 2U, 0U), 0x8000'0000U, 31U, 1U},
        {"SRAW", encode_r(0x3BU, 3U, 5U, 1U, 2U, 0x20U), 0x8000'0000U, 4U, 0xFFFF'FFFF'F800'0000ULL},
    }};

    for (const auto& item : cases) {
        CpuFixture fixture;
        fixture.cpu().state().set_integer(1U, item.lhs);
        fixture.cpu().state().set_integer(2U, item.rhs);
        const auto result = fixture.execute(item.bits);
        expect_retired(context, result, item.name);
        context.expect(fixture.cpu().state().integer(3U) == item.expected, std::string{item.name} + " 结果错误");
    }
}

void test_branches(TestContext& context) {
    struct Case final {
        const char* name;
        std::uint32_t function3;
        std::uint64_t lhs;
        std::uint64_t rhs;
    };
    const std::array<Case, 6U> cases{{
        {"BEQ", 0U, 5U, 5U},
        {"BNE", 1U, 5U, 6U},
        {"BLT", 4U, 0xFFFF'FFFF'FFFF'FFFFULL, 0U},
        {"BGE", 5U, 0U, 0xFFFF'FFFF'FFFF'FFFFULL},
        {"BLTU", 6U, 1U, 2U},
        {"BGEU", 7U, 2U, 1U},
    }};

    for (const auto& item : cases) {
        CpuFixture fixture;
        fixture.cpu().state().set_integer(1U, item.lhs);
        fixture.cpu().state().set_integer(2U, item.rhs);
        const auto result = fixture.execute(encode_b(item.function3, 1U, 2U, 8U));
        expect_retired(context, result, item.name);
        context.expect(fixture.cpu().state().program_counter() == kRamBase + 8U, std::string{item.name} + " 应采用分支目标");
    }

    CpuFixture not_taken;
    not_taken.cpu().state().set_integer(1U, 1U);
    not_taken.cpu().state().set_integer(2U, 2U);
    const auto result = not_taken.execute(encode_b(0U, 1U, 2U, 8U));
    expect_retired(context, result, "未采用 BEQ");
    context.expect(not_taken.cpu().state().program_counter() == kRamBase + 4U, "未采用分支必须顺序推进");
}

void test_loads_and_stores(TestContext& context) {
    CpuFixture fixture;
    constexpr std::uint64_t data_address = kRamBase + 0x400U;
    fixture.cpu().state().set_integer(1U, data_address);

    const auto seed = fixture.bus().write(
        rvemu::bus::PhysicalAddress{data_address},
        rvemu::bus::AccessWidth::DoubleWord,
        0x8877'6655'8001'0080ULL,
        rvemu::bus::AccessType::Initialization);
    context.expect(seed.ok(), "加载测试数据写入必须成功");

    struct LoadCase final {
        const char* name;
        std::uint32_t function3;
        std::uint32_t offset;
        std::uint64_t expected;
    };
    const std::array<LoadCase, 7U> loads{{
        {"LB", 0U, 0U, 0xFFFF'FFFF'FFFF'FF80ULL},
        {"LH", 1U, 2U, 0xFFFF'FFFF'FFFF'8001ULL},
        {"LW", 2U, 0U, 0xFFFF'FFFF'8001'0080ULL},
        {"LD", 3U, 0U, 0x8877'6655'8001'0080ULL},
        {"LBU", 4U, 0U, 0x80U},
        {"LHU", 5U, 2U, 0x8001U},
        {"LWU", 6U, 0U, 0x8001'0080U},
    }};

    for (const auto& item : loads) {
        const auto result = fixture.execute(encode_i(0x03U, 3U, item.function3, 1U, item.offset));
        expect_retired(context, result, item.name);
        context.expect(fixture.cpu().state().integer(3U) == item.expected, std::string{item.name} + " 扩展结果错误");
    }

    struct StoreCase final {
        const char* name;
        std::uint32_t function3;
        rvemu::bus::AccessWidth width;
        std::uint32_t offset;
        std::uint64_t expected;
    };
    const std::array<StoreCase, 4U> stores{{
        {"SB", 0U, rvemu::bus::AccessWidth::Byte, 0x20U, 0x88U},
        {"SH", 1U, rvemu::bus::AccessWidth::HalfWord, 0x22U, 0x7788U},
        {"SW", 2U, rvemu::bus::AccessWidth::Word, 0x24U, 0x5566'7788U},
        {"SD", 3U, rvemu::bus::AccessWidth::DoubleWord, 0x28U, 0x1122'3344'5566'7788ULL},
    }};
    fixture.cpu().state().set_integer(2U, 0x1122'3344'5566'7788ULL);
    for (const auto& item : stores) {
        const auto result = fixture.execute(encode_s(item.function3, 1U, 2U, item.offset));
        expect_retired(context, result, item.name);
        const auto memory = fixture.bus().read(
            rvemu::bus::PhysicalAddress{data_address + item.offset},
            item.width,
            rvemu::bus::AccessType::Load);
        context.expect(memory.ok() && memory.value == item.expected, std::string{item.name} + " 内存结果错误");
    }
}

void test_memory_faults_are_precise(TestContext& context) {
    CpuFixture fixture;
    fixture.cpu().state().set_integer(1U, 0x7000'0000U);
    fixture.cpu().state().set_integer(3U, 0xA5A5U);
    auto result = fixture.execute(encode_i(0x03U, 3U, 3U, 1U, 0U));
    expect_trap(context, result, rvemu::core::ExceptionCause::LoadAccessFault, "未映射 LD");
    context.expect(fixture.cpu().state().integer(3U) == 0xA5A5U, "失败加载不得写目标寄存器");
    context.expect(fixture.cpu().state().program_counter() == kRamBase, "失败加载不得推进 PC");

    fixture.cpu().state().set_integer(2U, 0x1122U);
    result = fixture.execute(encode_s(3U, 1U, 2U, 0U));
    expect_trap(context, result, rvemu::core::ExceptionCause::StoreAccessFault, "未映射 SD");
    context.expect(fixture.cpu().state().program_counter() == kRamBase, "失败存储不得推进 PC");
}

void test_negative_immediates_aliasing_and_wraparound(TestContext& context) {
    CpuFixture fixture;

    fixture.cpu().state().set_integer(1U, std::numeric_limits<std::uint64_t>::max());
    auto result = fixture.execute(encode_i(0x13U, 2U, 0U, 1U, 1U));
    expect_retired(context, result, "ADDI XLEN 回绕");
    context.expect(fixture.cpu().state().integer(2U) == 0U, "ADDI 必须按 64 位无符号位模式回绕");

    result = fixture.execute(encode_i(0x13U, 0U, 0U, 0U, 1U));
    expect_retired(context, result, "ADDI 写 x0");
    context.expect(fixture.cpu().state().integer(0U) == 0U, "指令写 x0 后仍必须为零");

    // 在 PC+4 执行 -4 跳转，验证 J 型负立即数的拆分与符号扩展。
    result = fixture.execute(encode_j(3U, 0x1F'FFFCU), kRamBase + 4U);
    expect_retired(context, result, "后向 JAL");
    context.expect(fixture.cpu().state().program_counter() == kRamBase, "JAL -4 必须跳回前一条地址");
    context.expect(fixture.cpu().state().integer(3U) == kRamBase + 8U, "后向 JAL 链接地址必须正确");

    fixture.cpu().state().set_integer(1U, 5U);
    fixture.cpu().state().set_integer(2U, 5U);
    result = fixture.execute(encode_b(0U, 1U, 2U, 0x1FFCU), kRamBase + 4U);
    expect_retired(context, result, "后向 BEQ");
    context.expect(fixture.cpu().state().program_counter() == kRamBase, "BEQ -4 必须跳回前一条地址");

    // rd 与 rs1 相同时，JALR 必须先使用旧 rs1 计算目标，再写入链接地址。
    fixture.cpu().state().set_integer(4U, kRamBase + 9U);
    result = fixture.execute(encode_i(0x67U, 4U, 0U, 4U, 0U));
    expect_retired(context, result, "JALR 寄存器别名");
    context.expect(fixture.cpu().state().program_counter() == kRamBase + 8U, "JALR 必须使用旧 rs1 目标");
    context.expect(fixture.cpu().state().integer(4U) == kRamBase + 4U, "JALR 随后才写链接地址");

    constexpr std::uint64_t data_address = kRamBase + 0x600U;
    const auto seed = fixture.bus().write(
        rvemu::bus::PhysicalAddress{data_address},
        rvemu::bus::AccessWidth::DoubleWord,
        0x0123'4567'89AB'CDEFULL,
        rvemu::bus::AccessType::Initialization);
    context.expect(seed.ok(), "负偏移访存测试数据写入必须成功");
    fixture.cpu().state().set_integer(5U, data_address + 4U);
    result = fixture.execute(encode_i(0x03U, 6U, 3U, 5U, 0xFFCU));
    expect_retired(context, result, "负偏移 LD");
    context.expect(
        fixture.cpu().state().integer(6U) == 0x0123'4567'89AB'CDEFULL,
        "LD -4 必须访问基址之前的数据");

    fixture.cpu().state().set_integer(7U, 0xFEDC'BA98'7654'3210ULL);
    result = fixture.execute(encode_s(3U, 5U, 7U, 0xFFCU));
    expect_retired(context, result, "负偏移 SD");
    const auto stored = fixture.bus().read(
        rvemu::bus::PhysicalAddress{data_address},
        rvemu::bus::AccessWidth::DoubleWord,
        rvemu::bus::AccessType::Load);
    context.expect(
        stored.ok() && stored.value == 0xFEDC'BA98'7654'3210ULL,
        "SD -4 必须写入基址之前的数据");
}

void test_fences_system_and_illegal(TestContext& context) {
    CpuFixture fixture;
    auto result = fixture.execute(0x0FF0'000FU);
    expect_retired(context, result, "FENCE");
    result = fixture.execute(0x0000'100FU);
    expect_retired(context, result, "FENCE.I");

    const std::array<rvemu::core::PrivilegeMode, 3U> modes{
        rvemu::core::PrivilegeMode::User,
        rvemu::core::PrivilegeMode::Supervisor,
        rvemu::core::PrivilegeMode::Machine,
    };
    const std::array<rvemu::core::ExceptionCause, 3U> causes{
        rvemu::core::ExceptionCause::EnvironmentCallFromUser,
        rvemu::core::ExceptionCause::EnvironmentCallFromSupervisor,
        rvemu::core::ExceptionCause::EnvironmentCallFromMachine,
    };
    for (std::size_t index = 0U; index < modes.size(); ++index) {
        fixture.cpu().state().set_privilege(modes[index]);
        result = fixture.execute(0x0000'0073U);
        expect_trap(context, result, causes[index], "ECALL");
        context.expect(fixture.cpu().state().program_counter() == kRamBase, "ECALL 不得推进 PC");
    }

    result = fixture.execute(0x0010'0073U);
    expect_trap(context, result, rvemu::core::ExceptionCause::Breakpoint, "EBREAK");

    fixture.cpu().state().set_integer(3U, 0x55U);
    result = fixture.execute(encode_r(0x33U, 3U, 0U, 1U, 2U, 1U));
    expect_trap(context, result, rvemu::core::ExceptionCause::IllegalInstruction, "未实现 M 扩展编码");
    context.expect(fixture.cpu().state().integer(3U) == 0x55U, "非法指令不得写目标寄存器");

    result = fixture.execute(encode_i(0x73U, 1U, 1U, 0U, 0U));
    expect_trap(context, result, rvemu::core::ExceptionCause::IllegalInstruction, "未实现 CSR 编码");
    result = fixture.execute(0xFFFF'FFFFU);
    expect_trap(context, result, rvemu::core::ExceptionCause::IllegalInstruction, "未定义 opcode");
}

}  // namespace

int main() {
    TestContext context;
    try {
        test_architectural_state(context);
        test_fetch_and_precise_faults(context);
        test_upper_and_control_flow(context);
        test_op_imm(context);
        test_op_register(context);
        test_word_operations(context);
        test_branches(context);
        test_loads_and_stores(context);
        test_memory_faults_are_precise(context);
        test_negative_immediates_aliasing_and_wraparound(context);
        test_fences_system_and_illegal(context);
    } catch (const std::exception& error) {
        std::cerr << "CPU 测试出现未处理异常：" << error.what() << '\n';
        return 2;
    }

    if (context.failures() != 0) {
        std::cerr << "CPU 测试共有 " << context.failures() << " 项断言失败。\n";
        return 1;
    }

    std::cout << "CPU 架构状态与 RV64I 全部真实机器码测试通过。\n";
    return 0;
}
