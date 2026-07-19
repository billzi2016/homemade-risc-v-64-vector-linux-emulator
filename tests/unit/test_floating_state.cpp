// 文件职责：验证正式 CsrFile/CpuState 的浮点 CSR、FS/SD、舍入解析、异常标志和 NaN boxing。
// 边界：本文件不实现浮点算术、不调用宿主 fenv，也不把参考模型接入生产执行路径。

#include "rvemu/core/cpu_state.hpp"
#include "rvemu/core/csr.hpp"
#include "rvemu/core/floating_state.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

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

[[nodiscard]] constexpr std::uint64_t bit(std::uint8_t index) noexcept {
    return 1ULL << index;
}

// 测试准备也走正式 CSR 原子访问入口；失败代表生产权限或 WARL 逻辑错误，不能跳过。
void write_csr(rvemu::core::CsrFile& csrs,
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
        throw std::runtime_error("浮点状态测试 CSR 写入失败");
    }
}

[[nodiscard]] std::uint64_t read_csr(
    rvemu::core::CsrFile& csrs,
    rvemu::core::CsrAddress address,
    rvemu::core::PrivilegeMode privilege = rvemu::core::PrivilegeMode::User) {
    const auto result = csrs.access(rvemu::core::CsrAccessRequest{
        address,
        privilege,
        true,
        false,
        rvemu::core::CsrModifyOperation::Replace,
        0U,
    });
    if (!result.success) {
        throw std::runtime_error("浮点状态测试 CSR 读取失败");
    }
    return result.value;
}

// 软件必须先通过 mstatus 启用上下文；状态层内部写入口不允许绕过 FS=Off 门控。
void set_fs(rvemu::core::CsrFile& csrs, std::uint8_t state) {
    const auto current = csrs.peek(rvemu::core::CsrAddress::Mstatus);
    const auto desired =
        (current & ~(0x3ULL << 13U)) | (static_cast<std::uint64_t>(state & 0x3U) << 13U);
    write_csr(csrs, rvemu::core::CsrAddress::Mstatus, desired);
}

void test_reset_gate_and_dirty_state(TestContext& context) {
    rvemu::core::CsrFile csrs;
    context.expect(!csrs.floating_state_enabled(), "复位 FS 必须为 Off");
    context.expect(csrs.peek(rvemu::core::CsrAddress::Fcsr) == 0U, "fcsr 复位值必须为零");

    const auto denied = csrs.access(rvemu::core::CsrAccessRequest{
        rvemu::core::CsrAddress::Fcsr,
        rvemu::core::PrivilegeMode::Machine,
        true,
        false,
        rvemu::core::CsrModifyOperation::Replace,
        0U,
    });
    context.expect(!denied.success, "FS=Off 时任何特权级访问 fcsr 都必须失败");
    csrs.accrue_floating_exception_flags(0x1FU);
    context.expect(csrs.peek(rvemu::core::CsrAddress::Fflags) == 0U, "FS=Off 不得偷偷累积异常标志");

    set_fs(csrs, 1U);
    context.expect(csrs.floating_state_enabled(), "FS=Initial 必须启用浮点状态");
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Fcsr) == 0U,
                   "U-mode 应能读取已启用 fcsr");
    write_csr(csrs,
              rvemu::core::CsrAddress::Fflags,
              static_cast<std::uint8_t>(rvemu::core::FloatingExceptionFlag::Inexact),
              rvemu::core::PrivilegeMode::User);

    const auto mstatus = csrs.peek(rvemu::core::CsrAddress::Mstatus);
    const auto sstatus = csrs.peek(rvemu::core::CsrAddress::Sstatus);
    context.expect(((mstatus >> 13U) & 0x3U) == 0x3U, "浮点 CSR 写入必须把 FS 标记 Dirty");
    context.expect((mstatus & bit(63U)) != 0U, "FS Dirty 必须派生 mstatus.SD");
    context.expect(((sstatus >> 13U) & 0x3U) == 0x3U, "sstatus 必须投影同一 FS 状态");
    context.expect((sstatus & bit(63U)) != 0U, "sstatus.SD 必须投影派生脏状态");
}

void test_fcsr_aliases(TestContext& context) {
    rvemu::core::CsrFile csrs;
    set_fs(csrs, 1U);

    write_csr(csrs, rvemu::core::CsrAddress::Fcsr, 0x1A5U, rvemu::core::PrivilegeMode::User);
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Fcsr) == 0xA5U, "fcsr 只保留低 8 位");
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Fflags) == 0x05U,
                   "fflags 必须投影 fcsr 低五位");
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Frm) == 0x05U,
                   "frm 必须投影 fcsr 位 7:5");

    write_csr(csrs, rvemu::core::CsrAddress::Fflags, 0x3FU, rvemu::core::PrivilegeMode::User);
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Fflags) == 0x1FU,
                   "fflags 写入必须屏蔽高位");
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Frm) == 0x05U, "写 fflags 不得改变 frm");

    write_csr(csrs, rvemu::core::CsrAddress::Frm, 0x0BU, rvemu::core::PrivilegeMode::User);
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Frm) == 0x03U, "frm 只保存三位编码");
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Fflags) == 0x1FU,
                   "写 frm 不得改变 fflags");
    context.expect(read_csr(csrs, rvemu::core::CsrAddress::Fcsr) == 0x7FU,
                   "fcsr 必须立即反映两个别名视图");
}

void test_rounding_mode_resolution(TestContext& context) {
    using Mode = rvemu::core::FloatingRoundingMode;
    constexpr std::array<Mode, 5U> expected{
        Mode::NearestTiesToEven,
        Mode::TowardZero,
        Mode::Down,
        Mode::Up,
        Mode::NearestTiesToMaximumMagnitude,
    };

    for (std::uint8_t encoded = 0U; encoded < expected.size(); ++encoded) {
        const auto direct = rvemu::core::resolve_floating_rounding_mode(encoded, 6U);
        context.expect(direct.has_value() && *direct == expected[encoded],
                       "静态 rm=0..4 必须解析为对应舍入模式");
        const auto dynamic = rvemu::core::resolve_floating_rounding_mode(7U, encoded);
        context.expect(dynamic.has_value() && *dynamic == expected[encoded],
                       "动态 rm 必须从合法 frm 解析对应舍入模式");
    }

    context.expect(!rvemu::core::resolve_floating_rounding_mode(5U, 0U).has_value(),
                   "指令 rm=5 必须保留非法");
    context.expect(!rvemu::core::resolve_floating_rounding_mode(6U, 0U).has_value(),
                   "指令 rm=6 必须保留非法");
    for (std::uint8_t dynamic = 5U; dynamic <= 7U; ++dynamic) {
        context.expect(!rvemu::core::resolve_floating_rounding_mode(7U, dynamic).has_value(),
                       "动态 frm=5..7 必须解析失败");
    }
}

void test_exception_flag_accrual(TestContext& context) {
    rvemu::core::CsrFile csrs;
    set_fs(csrs, 1U);
    csrs.accrue_floating_exception_flags(
        static_cast<std::uint8_t>(rvemu::core::FloatingExceptionFlag::Inexact));
    csrs.accrue_floating_exception_flags(static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(rvemu::core::FloatingExceptionFlag::Overflow)
        | static_cast<std::uint8_t>(rvemu::core::FloatingExceptionFlag::InvalidOperation)));
    context.expect(csrs.floating_exception_flags() == 0x15U,
                   "fflags 必须跨运算 OR 累积 NX、OF 与 NV");
    csrs.accrue_floating_exception_flags(0U);
    context.expect(csrs.floating_exception_flags() == 0x15U, "无异常运算不得清除历史 fflags");
    csrs.accrue_floating_exception_flags(0xE0U);
    context.expect(csrs.floating_exception_flags() == 0x15U, "fflags 不得接受高于五位的标志");

    write_csr(csrs, rvemu::core::CsrAddress::Fflags, 0U, rvemu::core::PrivilegeMode::User);
    context.expect(csrs.floating_exception_flags() == 0U, "软件显式写 fflags 必须能够清除标志");
}

void test_nan_boxing_and_cpu_state(TestContext& context) {
    constexpr std::uint32_t single_value = 0x3F80'0000U;
    context.expect(rvemu::core::box_single_precision(single_value) == 0xFFFF'FFFF'3F80'0000ULL,
                   "单精度写入必须把上 32 位全部置一");
    context.expect(rvemu::core::unbox_single_precision(0xFFFF'FFFF'FF80'0000ULL) == 0xFF80'0000U,
                   "合法 NaN box 必须原样返回低 32 位，包括负无穷位模式");
    context.expect(rvemu::core::unbox_single_precision(0x0000'0000'7F80'0001ULL)
                       == rvemu::core::kCanonicalSingleNan,
                   "非法 NaN box 必须作为 canonical quiet NaN");

    rvemu::core::CpuState state;
    state.reset(0x1000U);
    state.set_floating_single(1U, single_value);
    context.expect(state.floating(1U) == 0xFFFF'FFFF'3F80'0000ULL,
                   "CpuState 单精度写入口必须保存合法 box");
    context.expect(!state.csrs().floating_state_enabled(),
                   "内部寄存器写入不得从 FS=Off 偷偷启用状态");

    set_fs(state.csrs(), 1U);
    state.set_floating_single(2U, 0x7FC1'2345U);
    context.expect(state.floating_single(2U) == 0x7FC1'2345U, "合法单精度 box 必须保留 payload");
    context.expect(((state.csrs().peek(rvemu::core::CsrAddress::Mstatus) >> 13U) & 0x3U) == 0x3U,
                   "已启用状态的浮点寄存器写入必须标记 FS Dirty");

    state.set_floating(3U, 0x0000'0000'7F80'0001ULL);
    context.expect(state.floating_single(3U) == rvemu::core::kCanonicalSingleNan,
                   "CpuState 单精度读取必须统一处理非法 box");
    state.set_floating(4U, 0x0123'4567'89AB'CDEFULL);
    context.expect(state.floating(4U) == 0x0123'4567'89AB'CDEFULL,
                   "双精度寄存器位模式不得经过单精度规范化");

    state.reset();
    context.expect(state.floating(1U) == 0U && state.floating(4U) == 0U, "复位必须清空浮点寄存器");
    context.expect(state.csrs().peek(rvemu::core::CsrAddress::Fcsr) == 0U, "复位必须清空 fcsr");
    context.expect(!state.csrs().floating_state_enabled(), "复位必须恢复 FS=Off");
}

}  // namespace

int main() {
    TestContext context;
    try {
        test_reset_gate_and_dirty_state(context);
        test_fcsr_aliases(context);
        test_rounding_mode_resolution(context);
        test_exception_flag_accrual(context);
        test_nan_boxing_and_cpu_state(context);
    } catch (const std::exception& error) {
        std::cerr << "测试运行出现未处理异常：" << error.what() << '\n';
        return 2;
    }

    if (context.failures() != 0) {
        std::cerr << "共有 " << context.failures() << " 项断言失败。\n";
        return 1;
    }

    std::cout << "浮点 CSR、FS/SD、舍入、异常标志与 NaN boxing 测试通过。\n";
    return 0;
}
