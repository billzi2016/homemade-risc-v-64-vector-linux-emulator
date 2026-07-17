// 文件职责：通过正式 CPU、CSR、Bus 和真实 SYSTEM 机器码验证 RV64 特权架构状态转换。
// 边界：测试只构造输入并观察架构状态，不实现替代 CSR、Trap 路由或中断选择逻辑。

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
constexpr std::uint64_t kTrapBase = kRamBase + 0x400U;
constexpr std::uint64_t kSecondTrapBase = kRamBase + 0x800U;
constexpr std::uint64_t kInterruptFlag = 1ULL << 63U;

// 测试辅助只构造期望位掩码，不包含生产 CSR 写掩码或委托语义。
constexpr std::uint64_t bit(std::uint8_t index) noexcept {
    return 1ULL << index;
}

class TestContext final {
public:
    // 累计断言失败以便一次展示同一状态转换的全部偏差，不吞掉异常或伪造成功。
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

// 编码器只按 Zicsr 固定字段组装输入，合法性仍完全由生产译码和执行器判断。
[[nodiscard]] constexpr std::uint32_t encode_csr(
    std::uint32_t function3,
    std::uint32_t destination,
    std::uint32_t source_or_immediate,
    rvemu::core::CsrAddress address) noexcept {
    return (static_cast<std::uint32_t>(address) << 20U) |
           ((source_or_immediate & 0x1FU) << 15U) | ((function3 & 0x7U) << 12U) |
           ((destination & 0x1FU) << 7U) | 0x73U;
}

// 组装一条基础 ADDI，用来验证 WFI 唤醒后的真实后继指令执行。
[[nodiscard]] constexpr std::uint32_t encode_addi(
    std::uint32_t destination,
    std::uint32_t source,
    std::uint32_t immediate) noexcept {
    return ((immediate & 0xFFFU) << 20U) | ((source & 0x1FU) << 15U) |
           ((destination & 0x1FU) << 7U) | 0x13U;
}

class CpuFixture final {
public:
    // 使用真实 RAM 注册到正式 Bus；构造函数失败表示测试基础设施本身不可用。
    CpuFixture()
        : ram_(std::make_shared<rvemu::memory::PhysicalMemory>(
              rvemu::bus::PhysicalAddress{kRamBase},
              0x2000U,
              "privilege-test-ram")),
          cpu_(bus_) {
        const auto registration = bus_.register_region(ram_);
        if (!registration.ok()) {
            throw std::runtime_error("特权测试 RAM 注册失败");
        }
        cpu_.state().reset(kRamBase);
    }

    [[nodiscard]] rvemu::core::Cpu& cpu() noexcept { return cpu_; }

    // 通过初始化访问写入真实机器码，不绕过 RAM 边界或小端总线语义。
    void write_instruction(std::uint32_t bits, std::uint64_t address) {
        const auto written = bus_.write(
            rvemu::bus::PhysicalAddress{address},
            rvemu::bus::AccessWidth::Word,
            bits,
            rvemu::bus::AccessType::Initialization);
        if (!written.ok()) {
            throw std::runtime_error("特权测试机器码写入失败");
        }
    }

    // 将 PC 指向指定机器码并执行生产 step，返回值由调用测试验证退休或精确异常。
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

// 统一验证正常退休不携带 WFI 停顿或 Trap，避免各测试遗漏结果字段。
void expect_retired(
    TestContext& context,
    const rvemu::core::StepResult& result,
    const std::string& name) {
    context.expect(result.retired, name + " 应成功退休");
    context.expect(!result.stalled, name + " 不应停在 WFI");
    context.expect(!result.trap.has_value(), name + " 不应产生同步异常");
}

// 统一验证非法指令无退休且 cause 精确，测试不吞掉其他类型异常。
void expect_illegal(
    TestContext& context,
    const rvemu::core::StepResult& result,
    const std::string& name) {
    context.expect(!result.retired, name + " 不得退休");
    context.expect(result.trap.has_value(), name + " 必须产生异常");
    if (result.trap.has_value()) {
        context.expect(
            result.trap->cause == rvemu::core::ExceptionCause::IllegalInstruction,
            name + " 必须报告非法指令");
    }
}

// 测试准备通过生产原子访问入口写 CSR；失败直接终止，不能靠私有字段注入状态。
void write_csr(
    rvemu::core::CsrFile& csrs,
    rvemu::core::CsrAddress address,
    std::uint64_t value,
    rvemu::core::PrivilegeMode privilege = rvemu::core::PrivilegeMode::Machine) {
    const auto result = csrs.access(rvemu::core::CsrAccessRequest{
        address,
        privilege,
        false,
        true,
        rvemu::core::CsrModifyOperation::Replace,
        value,
    });
    if (!result.success) {
        throw std::runtime_error("测试准备阶段 CSR 写入失败");
    }
}

// 测试观察同样经过生产权限检查，并以显式 privilege 验证分层可见性。
[[nodiscard]] std::uint64_t read_csr(
    rvemu::core::CsrFile& csrs,
    rvemu::core::CsrAddress address,
    rvemu::core::PrivilegeMode privilege = rvemu::core::PrivilegeMode::Machine) {
    const auto result = csrs.access(rvemu::core::CsrAccessRequest{
        address,
        privilege,
        true,
        false,
        rvemu::core::CsrModifyOperation::Replace,
        0U,
    });
    if (!result.success) {
        throw std::runtime_error("测试观察阶段 CSR 读取失败");
    }
    return result.value;
}

// 验证复位能力声明、地址权限、只读编码以及关键 WARL 字段的合法读回值。
void test_reset_permissions_and_warl(TestContext& context) {
    rvemu::core::CsrFile csrs;
    const auto misa = csrs.peek(rvemu::core::CsrAddress::Misa);
    context.expect((misa >> 62U) == 2U, "misa.MXL 必须声明 RV64");
    context.expect((misa & bit('I' - 'A')) != 0U, "misa 必须声明已实现 I 扩展");
    context.expect((misa & bit('S' - 'A')) != 0U, "misa 必须声明 S-mode");
    context.expect((misa & bit('U' - 'A')) != 0U, "misa 必须声明 U-mode");
    context.expect((misa & bit('M' - 'A')) != 0U, "RV64M 完整实现后 misa 必须声明 M 扩展");

    const auto missing = csrs.access(rvemu::core::CsrAccessRequest{
        static_cast<rvemu::core::CsrAddress>(0x777U),
        rvemu::core::PrivilegeMode::Machine,
        true,
        false,
        rvemu::core::CsrModifyOperation::Replace,
        0U,
    });
    context.expect(!missing.success, "访问不存在 CSR 必须失败");

    const auto low_privilege = csrs.access(rvemu::core::CsrAccessRequest{
        rvemu::core::CsrAddress::Mstatus,
        rvemu::core::PrivilegeMode::Supervisor,
        true,
        false,
        rvemu::core::CsrModifyOperation::Replace,
        0U,
    });
    context.expect(!low_privilege.success, "S-mode 不得读取 M-mode CSR");

    const auto write_read_only = csrs.access(rvemu::core::CsrAccessRequest{
        rvemu::core::CsrAddress::Mvendorid,
        rvemu::core::PrivilegeMode::Machine,
        false,
        true,
        rvemu::core::CsrModifyOperation::Replace,
        0U,
    });
    context.expect(!write_read_only.success, "写只读 mvendorid 必须失败");

    const auto write_warl_misa = csrs.access(rvemu::core::CsrAccessRequest{
        rvemu::core::CsrAddress::Misa,
        rvemu::core::PrivilegeMode::Machine,
        false,
        true,
        rvemu::core::CsrModifyOperation::Replace,
        ~0ULL,
    });
    context.expect(write_warl_misa.success, "misa 是 WARL CSR，写入访问本身必须合法");
    context.expect(csrs.peek(rvemu::core::CsrAddress::Misa) == misa, "misa 必须只读回真实实现扩展集合");

    write_csr(csrs, rvemu::core::CsrAddress::Mstatus, 0x2ULL << 11U);
    context.expect(
        ((csrs.peek(rvemu::core::CsrAddress::Mstatus) >> 11U) & 0x3U) == 0x3U,
        "MPP 保留编码必须归一为已实现特权级");

    write_csr(csrs, rvemu::core::CsrAddress::Mtvec, 0x1237U);
    context.expect(csrs.peek(rvemu::core::CsrAddress::Mtvec) == 0x1234U, "mtvec 保留 MODE 必须归一为 Direct");
    write_csr(csrs, rvemu::core::CsrAddress::Mepc, 0x1003U);
    context.expect(csrs.peek(rvemu::core::CsrAddress::Mepc) == 0x1000U, "未实现 C 时 mepc 必须四字节对齐");

    constexpr std::uint64_t sv39 = (8ULL << 60U) | 0x12345U;
    write_csr(csrs, rvemu::core::CsrAddress::Satp, sv39);
    write_csr(csrs, rvemu::core::CsrAddress::Satp, (9ULL << 60U) | 0xABCDEU);
    context.expect(csrs.peek(rvemu::core::CsrAddress::Satp) == sv39, "satp 不支持 MODE 写入必须整体无效");

    write_csr(csrs, rvemu::core::CsrAddress::Medeleg, ~0ULL);
    const auto expected_exceptions = bit(0U) | bit(1U) | bit(2U) | bit(3U) |
                                     bit(4U) | bit(5U) | bit(6U) | bit(7U) |
                                     bit(8U) | bit(9U) | bit(12U) | bit(13U) | bit(15U);
    context.expect(csrs.peek(rvemu::core::CsrAddress::Medeleg) == expected_exceptions, "medeleg 只允许真实可委托异常位");
    write_csr(csrs, rvemu::core::CsrAddress::Mideleg, ~0ULL);
    context.expect(csrs.peek(rvemu::core::CsrAddress::Mideleg) == (bit(1U) | bit(5U) | bit(9U)), "mideleg 只允许监督级中断位");
}

// 用六种真实 Zicsr 机器码验证旧值返回、条件写、立即数、权限拒绝和无副作用路径。
void test_real_csr_instructions(TestContext& context) {
    CpuFixture fixture;
    fixture.cpu().state().set_integer(1U, 0xAAU);
    auto result = fixture.execute(encode_csr(1U, 2U, 1U, rvemu::core::CsrAddress::Mscratch));
    expect_retired(context, result, "CSRRW");
    context.expect(fixture.cpu().state().integer(2U) == 0U, "CSRRW 必须返回原值");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Mscratch) == 0xAAU, "CSRRW 必须写入新值");

    fixture.cpu().state().set_integer(1U, 0x55U);
    result = fixture.execute(encode_csr(2U, 3U, 1U, rvemu::core::CsrAddress::Mscratch));
    expect_retired(context, result, "CSRRS");
    context.expect(fixture.cpu().state().integer(3U) == 0xAAU, "CSRRS 必须返回修改前值");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Mscratch) == 0xFFU, "CSRRS 必须设置掩码位");

    fixture.cpu().state().set_integer(1U, 0x0FU);
    result = fixture.execute(encode_csr(3U, 4U, 1U, rvemu::core::CsrAddress::Mscratch));
    expect_retired(context, result, "CSRRC");
    context.expect(fixture.cpu().state().integer(4U) == 0xFFU, "CSRRC 必须返回修改前值");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Mscratch) == 0xF0U, "CSRRC 必须清除掩码位");

    result = fixture.execute(encode_csr(5U, 0U, 3U, rvemu::core::CsrAddress::Mscratch));
    expect_retired(context, result, "CSRRWI rd=x0");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Mscratch) == 3U, "CSRRWI 必须使用五位零扩展立即数");
    result = fixture.execute(encode_csr(6U, 5U, 4U, rvemu::core::CsrAddress::Mscratch));
    expect_retired(context, result, "CSRRSI");
    context.expect(fixture.cpu().state().integer(5U) == 3U, "CSRRSI 必须返回原值");
    result = fixture.execute(encode_csr(7U, 6U, 1U, rvemu::core::CsrAddress::Mscratch));
    expect_retired(context, result, "CSRRCI");
    context.expect(fixture.cpu().state().integer(6U) == 7U, "CSRRCI 必须观察 CSRRSI 后状态");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Mscratch) == 6U, "CSRRCI 必须清除指定立即数位");

    result = fixture.execute(encode_csr(2U, 7U, 0U, rvemu::core::CsrAddress::Mvendorid));
    expect_retired(context, result, "CSRRS x0 只读 mvendorid");
    context.expect(fixture.cpu().state().integer(7U) == 0U, "只读 CSRRS 必须返回 mvendorid");

    fixture.cpu().state().set_integer(1U, 0U);
    result = fixture.execute(encode_csr(2U, 7U, 1U, rvemu::core::CsrAddress::Mvendorid));
    expect_illegal(context, result, "CSRRS 非 x0 零值写只读 CSR");
    result = fixture.execute(encode_csr(1U, 0U, 1U, rvemu::core::CsrAddress::Mvendorid));
    expect_illegal(context, result, "CSRRW 写只读 CSR");

    const auto misa_before = fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Misa);
    result = fixture.execute(encode_csr(1U, 8U, 1U, rvemu::core::CsrAddress::Misa));
    expect_retired(context, result, "CSRRW 写 WARL misa");
    context.expect(fixture.cpu().state().integer(8U) == misa_before, "CSRRW misa 必须返回写前值");
    context.expect(fixture.cpu().state().csrs().peek(rvemu::core::CsrAddress::Misa) == misa_before, "misa 写入不得宣称未实现扩展");

    fixture.cpu().state().set_privilege(rvemu::core::PrivilegeMode::User);
    result = fixture.execute(encode_csr(2U, 1U, 0U, rvemu::core::CsrAddress::Sstatus));
    expect_illegal(context, result, "U-mode 读取 sstatus");

    fixture.cpu().state().set_privilege(rvemu::core::PrivilegeMode::Machine);
    write_csr(fixture.cpu().state().csrs(), rvemu::core::CsrAddress::Mstatus, bit(20U));
    fixture.cpu().state().set_privilege(rvemu::core::PrivilegeMode::Supervisor);
    result = fixture.execute(encode_csr(2U, 1U, 0U, rvemu::core::CsrAddress::Satp));
    expect_illegal(context, result, "TVM 拦截 S-mode satp");
}

// 验证 sstatus/sie/sip 只投影共享 Machine 状态，写别名不会破坏隐藏或硬件只读位。
void test_supervisor_aliases(TestContext& context) {
    rvemu::core::CsrFile csrs;
    const auto supervisor_mask = bit(1U) | bit(5U) | bit(9U);
    const auto machine_mask = bit(3U) | bit(7U) | bit(11U);
    write_csr(csrs, rvemu::core::CsrAddress::Mideleg, supervisor_mask);
    write_csr(csrs, rvemu::core::CsrAddress::Mie, supervisor_mask | machine_mask);
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Sie, rvemu::core::PrivilegeMode::Supervisor) == supervisor_mask, "sie 必须是 mie 与 mideleg 的受限视图");

    write_csr(csrs, rvemu::core::CsrAddress::Sie, bit(1U), rvemu::core::PrivilegeMode::Supervisor);
    context.expect(csrs.peek(rvemu::core::CsrAddress::Mie) == (machine_mask | bit(1U)), "写 sie 必须只修改已委托 mie 位并保留机器位");

    csrs.set_interrupt_pending(rvemu::core::InterruptCause::SupervisorSoftware, true);
    csrs.set_interrupt_pending(rvemu::core::InterruptCause::SupervisorTimer, true);
    csrs.set_interrupt_pending(rvemu::core::InterruptCause::SupervisorExternal, true);
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Sip, rvemu::core::PrivilegeMode::Supervisor) == supervisor_mask, "sip 必须投影已委托 mip 状态");
    write_csr(csrs, rvemu::core::CsrAddress::Sip, 0U, rvemu::core::PrivilegeMode::Supervisor);
    context.expect((csrs.peek(rvemu::core::CsrAddress::Mip) & bit(1U)) == 0U, "sip 只允许软件清除 SSIP");
    context.expect((csrs.peek(rvemu::core::CsrAddress::Mip) & (bit(5U) | bit(9U))) == (bit(5U) | bit(9U)), "写 sip 不得清除设备 pending 位");

    write_csr(csrs, rvemu::core::CsrAddress::Mstatus, bit(3U) | (0x3ULL << 11U));
    write_csr(csrs, rvemu::core::CsrAddress::Sstatus, bit(1U) | bit(18U), rvemu::core::PrivilegeMode::Supervisor);
    const auto mstatus = csrs.peek(rvemu::core::CsrAddress::Mstatus);
    context.expect((mstatus & bit(1U)) != 0U && (mstatus & bit(18U)) != 0U, "写 sstatus 必须更新共享 S 字段");
    context.expect((mstatus & bit(3U)) != 0U && ((mstatus >> 11U) & 0x3U) == 3U, "写 sstatus 不得破坏 M-only 字段");
}

// 验证两级 counteren、CLINT time 投影，以及 cycle/instret 显式写覆盖隐式更新。
void test_counters_and_access_control(TestContext& context) {
    rvemu::core::CsrFile csrs;
    csrs.set_time(0x1234'5678U);
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Time) == 0x1234'5678U, "M-mode 必须读取 CLINT 投影的 time");

    const auto supervisor_denied = csrs.access(rvemu::core::CsrAccessRequest{
        rvemu::core::CsrAddress::Cycle,
        rvemu::core::PrivilegeMode::Supervisor,
        true,
        false,
        rvemu::core::CsrModifyOperation::Replace,
        0U,
    });
    context.expect(!supervisor_denied.success, "mcounteren 清零时 S-mode 不得读取 cycle");
    write_csr(csrs, rvemu::core::CsrAddress::Mcounteren, bit(0U) | bit(1U) | bit(2U));
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Cycle, rvemu::core::PrivilegeMode::Supervisor) == 0U, "mcounteren 放行后 S-mode 必须读取 cycle");

    const auto user_denied = csrs.access(rvemu::core::CsrAccessRequest{
        rvemu::core::CsrAddress::Instret,
        rvemu::core::PrivilegeMode::User,
        true,
        false,
        rvemu::core::CsrModifyOperation::Replace,
        0U,
    });
    context.expect(!user_denied.success, "scounteren 清零时 U-mode 不得读取 instret");
    write_csr(csrs, rvemu::core::CsrAddress::Scounteren, bit(2U), rvemu::core::PrivilegeMode::Supervisor);
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Instret, rvemu::core::PrivilegeMode::User) == 0U, "两级 counteren 同时放行后 U-mode 必须读取 instret");

    CpuFixture fixture;
    auto& state = fixture.cpu().state();
    auto result = fixture.execute(encode_addi(1U, 0U, 1U));
    expect_retired(context, result, "计数器普通退休指令");
    context.expect(state.csrs().peek(rvemu::core::CsrAddress::Mcycle) == 1U, "普通步进必须增加 mcycle");
    context.expect(state.csrs().peek(rvemu::core::CsrAddress::Minstret) == 1U, "退休指令必须增加 minstret");

    result = fixture.execute(0xFFFF'FFFFU);
    expect_illegal(context, result, "计数器非法指令");
    context.expect(state.csrs().peek(rvemu::core::CsrAddress::Mcycle) == 2U, "异常指令仍消耗一个模拟 cycle");
    context.expect(state.csrs().peek(rvemu::core::CsrAddress::Minstret) == 1U, "异常指令不得增加 minstret");

    state.set_integer(2U, 100U);
    result = fixture.execute(encode_csr(1U, 0U, 2U, rvemu::core::CsrAddress::Minstret));
    expect_retired(context, result, "显式写 minstret");
    context.expect(state.csrs().peek(rvemu::core::CsrAddress::Minstret) == 100U, "显式 minstret 写必须抑制同指令隐式递增");

    state.set_integer(2U, 200U);
    result = fixture.execute(encode_csr(1U, 0U, 2U, rvemu::core::CsrAddress::Mcycle));
    expect_retired(context, result, "显式写 mcycle");
    context.expect(state.csrs().peek(rvemu::core::CsrAddress::Mcycle) == 200U, "显式 mcycle 写必须抑制同指令隐式递增");
}

// 从 S-mode 制造未委托 ECALL，核对 M Trap 全状态并通过真实 MRET 往返。
void test_machine_trap_and_mret(TestContext& context) {
    CpuFixture fixture;
    auto& state = fixture.cpu().state();
    write_csr(state.csrs(), rvemu::core::CsrAddress::Mtvec, kTrapBase);
    write_csr(state.csrs(), rvemu::core::CsrAddress::Mstatus, bit(3U));
    write_csr(state.csrs(), rvemu::core::CsrAddress::Scause, 0x55U);
    state.set_privilege(rvemu::core::PrivilegeMode::Supervisor);

    const auto ecall = fixture.execute(0x0000'0073U);
    context.expect(ecall.trap.has_value(), "S-mode ECALL 必须先产生精确同步异常");
    const auto delivery = fixture.cpu().take_trap(*ecall.trap);
    context.expect(delivery.target == rvemu::core::PrivilegeMode::Machine, "未委托异常必须进入 M-mode");
    context.expect(state.privilege() == rvemu::core::PrivilegeMode::Machine, "Trap 后当前模式必须为 M");
    context.expect(state.program_counter() == kTrapBase, "Direct mtvec 必须使用 BASE");
    context.expect(state.csrs().peek(rvemu::core::CsrAddress::Mepc) == kRamBase, "mepc 必须保存故障 PC");
    context.expect(state.csrs().peek(rvemu::core::CsrAddress::Mcause) == 9U, "mcause 必须记录 S-mode ECALL");
    context.expect(state.csrs().peek(rvemu::core::CsrAddress::Scause) == 0x55U, "M Trap 不得修改监督级 Trap CSR");
    const auto trapped_status = state.csrs().peek(rvemu::core::CsrAddress::Mstatus);
    context.expect((trapped_status & bit(3U)) == 0U && (trapped_status & bit(7U)) != 0U, "M Trap 必须压栈 MIE 到 MPIE");
    context.expect(((trapped_status >> 11U) & 0x3U) == 1U, "M Trap 必须把来源 S 写入 MPP");

    fixture.write_instruction(0x3020'0073U, kTrapBase);
    const auto returned = fixture.cpu().step();
    expect_retired(context, returned, "MRET");
    context.expect(state.privilege() == rvemu::core::PrivilegeMode::Supervisor, "MRET 必须恢复 S-mode");
    context.expect(state.program_counter() == kRamBase, "MRET 必须恢复 mepc");
    const auto returned_status = state.csrs().peek(rvemu::core::CsrAddress::Mstatus);
    context.expect((returned_status & bit(3U)) != 0U && (returned_status & bit(7U)) != 0U, "MRET 必须恢复 MIE 并置 MPIE");
    context.expect(((returned_status >> 11U) & 0x3U) == 0U, "MRET 必须把 MPP 重置为最低特权级");
}

// 从 U-mode 制造已委托 ECALL，核对 S Trap/xRET，并确认 M-mode Trap 永不向下降权。
void test_delegated_trap_and_sret(TestContext& context) {
    CpuFixture fixture;
    auto& state = fixture.cpu().state();
    write_csr(state.csrs(), rvemu::core::CsrAddress::Medeleg, bit(8U));
    write_csr(state.csrs(), rvemu::core::CsrAddress::Stvec, kTrapBase | 1U);
    write_csr(state.csrs(), rvemu::core::CsrAddress::Mstatus, bit(1U));
    write_csr(state.csrs(), rvemu::core::CsrAddress::Mcause, 0x66U);
    state.set_privilege(rvemu::core::PrivilegeMode::User);

    const auto ecall = fixture.execute(0x0000'0073U);
    const auto delivery = fixture.cpu().take_trap(*ecall.trap);
    context.expect(delivery.target == rvemu::core::PrivilegeMode::Supervisor, "已委托 U ECALL 必须进入 S-mode");
    context.expect(state.program_counter() == kTrapBase, "同步异常在 Vectored 模式仍必须进入 BASE");
    context.expect(state.csrs().peek(rvemu::core::CsrAddress::Sepc) == kRamBase, "sepc 必须保存 U-mode 故障 PC");
    context.expect(state.csrs().peek(rvemu::core::CsrAddress::Scause) == 8U, "scause 必须记录 U-mode ECALL");
    context.expect(state.csrs().peek(rvemu::core::CsrAddress::Mcause) == 0x66U, "S Trap 不得修改机器级 Trap CSR");
    const auto trapped_status = state.csrs().peek(rvemu::core::CsrAddress::Mstatus);
    context.expect((trapped_status & bit(1U)) == 0U && (trapped_status & bit(5U)) != 0U, "S Trap 必须压栈 SIE 到 SPIE");
    context.expect((trapped_status & bit(8U)) == 0U, "来自 U-mode 的 S Trap 必须清除 SPP");

    fixture.write_instruction(0x1020'0073U, kTrapBase);
    const auto returned = fixture.cpu().step();
    expect_retired(context, returned, "SRET");
    context.expect(state.privilege() == rvemu::core::PrivilegeMode::User, "SRET 必须按 SPP 恢复 U-mode");
    context.expect(state.program_counter() == kRamBase, "SRET 必须恢复 sepc");

    // 委托永远不能把 M-mode 自身的 Trap 降级到 S-mode。
    write_csr(state.csrs(), rvemu::core::CsrAddress::Mtvec, kSecondTrapBase);
    state.set_privilege(rvemu::core::PrivilegeMode::Machine);
    const auto machine_ecall = fixture.execute(0x0000'0073U);
    const auto machine_delivery = fixture.cpu().take_trap(*machine_ecall.trap);
    context.expect(machine_delivery.target == rvemu::core::PrivilegeMode::Machine, "M-mode Trap 不得向下委托");
}

// 同时挂起六种标准中断验证固定优先级，并检查 M/S Vectored 入口与 cause 最高位。
void test_interrupt_priority_and_vectors(TestContext& context) {
    rvemu::core::CsrFile priority_csrs;
    constexpr std::array<rvemu::core::InterruptCause, 6U> priority{
        rvemu::core::InterruptCause::MachineExternal,
        rvemu::core::InterruptCause::MachineSoftware,
        rvemu::core::InterruptCause::MachineTimer,
        rvemu::core::InterruptCause::SupervisorExternal,
        rvemu::core::InterruptCause::SupervisorSoftware,
        rvemu::core::InterruptCause::SupervisorTimer,
    };
    write_csr(priority_csrs, rvemu::core::CsrAddress::Mie, bit(1U) | bit(3U) | bit(5U) | bit(7U) | bit(9U) | bit(11U));
    for (const auto cause : priority) {
        priority_csrs.set_interrupt_pending(cause, true);
    }
    for (const auto expected : priority) {
        const auto selected = priority_csrs.select_pending_interrupt(rvemu::core::PrivilegeMode::User);
        context.expect(selected.has_value() && selected->cause == expected, "中断必须遵循 MEI/MSI/MTI/SEI/SSI/STI 优先级");
        priority_csrs.set_interrupt_pending(expected, false);
    }

    CpuFixture machine_fixture;
    auto& machine_state = machine_fixture.cpu().state();
    write_csr(machine_state.csrs(), rvemu::core::CsrAddress::Mtvec, kTrapBase | 1U);
    write_csr(machine_state.csrs(), rvemu::core::CsrAddress::Mie, bit(11U));
    write_csr(machine_state.csrs(), rvemu::core::CsrAddress::Mstatus, bit(3U));
    machine_state.csrs().set_interrupt_pending(rvemu::core::InterruptCause::MachineExternal, true);
    machine_state.set_program_counter(kRamBase + 0x100U);
    const auto machine_interrupt = machine_fixture.cpu().take_pending_interrupt();
    context.expect(machine_interrupt.has_value(), "已使能 MEI 必须被接收");
    context.expect(machine_interrupt->target == rvemu::core::PrivilegeMode::Machine, "MEI 必须进入 M-mode");
    context.expect(machine_state.program_counter() == kTrapBase + 44U, "Vectored MEI 必须进入 BASE+4*11");
    context.expect(machine_state.csrs().peek(rvemu::core::CsrAddress::Mcause) == (kInterruptFlag | 11U), "mcause 必须包含中断最高位和 cause");
    context.expect(machine_state.csrs().peek(rvemu::core::CsrAddress::Mepc) == kRamBase + 0x100U, "中断 mepc 必须保存下一条待执行 PC");

    CpuFixture supervisor_fixture;
    auto& supervisor_state = supervisor_fixture.cpu().state();
    const auto supervisor_mask = bit(1U) | bit(5U) | bit(9U);
    write_csr(supervisor_state.csrs(), rvemu::core::CsrAddress::Mideleg, supervisor_mask);
    write_csr(supervisor_state.csrs(), rvemu::core::CsrAddress::Mie, supervisor_mask);
    write_csr(supervisor_state.csrs(), rvemu::core::CsrAddress::Stvec, kTrapBase | 1U);
    supervisor_state.csrs().set_interrupt_pending(rvemu::core::InterruptCause::SupervisorExternal, true);
    supervisor_state.set_privilege(rvemu::core::PrivilegeMode::User);
    supervisor_state.set_program_counter(kRamBase + 0x200U);
    const auto supervisor_interrupt = supervisor_fixture.cpu().take_pending_interrupt();
    context.expect(supervisor_interrupt.has_value() && supervisor_interrupt->target == rvemu::core::PrivilegeMode::Supervisor, "U-mode 中已委托 SEI 必须进入 S-mode");
    context.expect(supervisor_state.program_counter() == kTrapBase + 36U, "Vectored SEI 必须进入 BASE+4*9");
    context.expect(supervisor_state.csrs().peek(rvemu::core::CsrAddress::Scause) == (kInterruptFlag | 9U), "scause 必须记录监督级外部中断");

    supervisor_state.set_privilege(rvemu::core::PrivilegeMode::Machine);
    supervisor_state.csrs().set_interrupt_pending(rvemu::core::InterruptCause::SupervisorExternal, true);
    context.expect(!supervisor_fixture.cpu().take_pending_interrupt().has_value(), "已委托 S 中断在 M-mode 必须被屏蔽");
}

// 验证 xRET/WFI 特权拦截、TSR/TW，以及 WFI 按局部 enable 唤醒但不伪造 Trap。
void test_privileged_legality_and_wfi(TestContext& context) {
    CpuFixture fixture;
    auto& state = fixture.cpu().state();

    state.set_privilege(rvemu::core::PrivilegeMode::User);
    expect_illegal(context, fixture.execute(0x1020'0073U), "U-mode SRET");
    state.set_privilege(rvemu::core::PrivilegeMode::Supervisor);
    expect_illegal(context, fixture.execute(0x3020'0073U), "S-mode MRET");

    state.set_privilege(rvemu::core::PrivilegeMode::Machine);
    write_csr(state.csrs(), rvemu::core::CsrAddress::Mstatus, bit(22U));
    state.set_privilege(rvemu::core::PrivilegeMode::Supervisor);
    expect_illegal(context, fixture.execute(0x1020'0073U), "TSR 拦截 SRET");

    state.set_privilege(rvemu::core::PrivilegeMode::User);
    expect_illegal(context, fixture.execute(0x1050'0073U), "U-mode WFI");
    state.set_privilege(rvemu::core::PrivilegeMode::Machine);
    write_csr(state.csrs(), rvemu::core::CsrAddress::Mstatus, bit(21U));
    state.set_privilege(rvemu::core::PrivilegeMode::Supervisor);
    expect_illegal(context, fixture.execute(0x1050'0073U), "TW 拦截 S-mode WFI");

    state.set_privilege(rvemu::core::PrivilegeMode::Machine);
    write_csr(state.csrs(), rvemu::core::CsrAddress::Mstatus, 0U);
    auto result = fixture.execute(0x1050'0073U);
    expect_retired(context, result, "M-mode WFI");
    context.expect(state.waiting_for_interrupt(), "WFI 退休后必须进入等待状态");
    result = fixture.cpu().step();
    context.expect(result.stalled && !result.retired && !result.trap.has_value(), "无局部使能中断时 WFI 必须保持等待");

    fixture.write_instruction(encode_addi(1U, 0U, 7U), kRamBase + 4U);
    write_csr(state.csrs(), rvemu::core::CsrAddress::Mie, bit(7U));
    state.csrs().set_interrupt_pending(rvemu::core::InterruptCause::MachineTimer, true);
    result = fixture.cpu().step();
    expect_retired(context, result, "WFI 被局部使能中断唤醒后的下一指令");
    context.expect(!state.waiting_for_interrupt(), "局部使能 pending 必须唤醒 WFI，即使全局 MIE 为零");
    context.expect(state.integer(1U) == 7U, "非可接收中断唤醒后必须从 WFI 下一条继续");
}

}  // namespace

int main() {
    TestContext context;
    try {
        test_reset_permissions_and_warl(context);
        test_real_csr_instructions(context);
        test_supervisor_aliases(context);
        test_counters_and_access_control(context);
        test_machine_trap_and_mret(context);
        test_delegated_trap_and_sret(context);
        test_interrupt_priority_and_vectors(context);
        test_privileged_legality_and_wfi(context);
    } catch (const std::exception& error) {
        std::cerr << "特权架构测试出现未处理异常：" << error.what() << '\n';
        return 2;
    }

    if (context.failures() != 0) {
        std::cerr << "特权架构测试共有 " << context.failures() << " 项断言失败。\n";
        return 1;
    }

    std::cout << "CSR、M/S/U Trap、委托、中断优先级与 xRET 测试全部通过。\n";
    return 0;
}
