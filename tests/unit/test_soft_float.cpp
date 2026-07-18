// 文件职责：用固定 IEEE 754 位模式验证生产软件浮点核心的算术、舍入、异常与转换边界。
// 边界：期望值均为预先确定的规范结果；测试不调用宿主浮点运算生成动态参考答案。

#include "rvemu/core/soft_float.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

namespace {

using Format = rvemu::core::FloatingFormat;
using Mode = rvemu::core::FloatingRoundingMode;

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

[[nodiscard]] constexpr std::uint8_t flag(rvemu::core::FloatingExceptionFlag value) noexcept {
    return static_cast<std::uint8_t>(value);
}

void test_basic_arithmetic(TestContext& context) {
    const auto single_sum = rvemu::core::floating_add(
        Format::Single, 0x3F80'0000U, 0x4000'0000U, Mode::NearestTiesToEven);
    context.expect(single_sum.bits == 0x4040'0000U && single_sum.flags == 0U,
                   "binary32 1+2 必须精确得到 3");

    const auto double_subtract = rvemu::core::floating_subtract(
        Format::Double,
        0x4018'0000'0000'0000ULL,
        0x4000'0000'0000'0000ULL,
        Mode::NearestTiesToEven);
    context.expect(double_subtract.bits == 0x4010'0000'0000'0000ULL,
                   "binary64 6-2 必须精确得到 4");

    const auto product = rvemu::core::floating_multiply(
        Format::Single, 0xC040'0000U, 0x4000'0000U, Mode::NearestTiesToEven);
    context.expect(product.bits == 0xC0C0'0000U && product.flags == 0U,
                   "binary32 -3*2 必须精确得到 -6");

    const auto quotient_rne = rvemu::core::floating_divide(
        Format::Single, 0x3F80'0000U, 0x4040'0000U, Mode::NearestTiesToEven);
    const auto quotient_rtz = rvemu::core::floating_divide(
        Format::Single, 0x3F80'0000U, 0x4040'0000U, Mode::TowardZero);
    context.expect(quotient_rne.bits == 0x3EAA'AAABU,
                   "binary32 1/3 的 RNE 末位必须向偶数方向进位");
    context.expect(quotient_rtz.bits == 0x3EAA'AAAAU,
                   "binary32 1/3 的 RTZ 必须截去无限尾部");
    context.expect(quotient_rne.flags == flag(rvemu::core::FloatingExceptionFlag::Inexact) &&
                       quotient_rtz.flags == flag(rvemu::core::FloatingExceptionFlag::Inexact),
                   "1/3 必须设置 NX");

    const auto root = rvemu::core::floating_square_root(
        Format::Double, 0x4010'0000'0000'0000ULL, Mode::NearestTiesToEven);
    context.expect(root.bits == 0x4000'0000'0000'0000ULL && root.flags == 0U,
                   "binary64 sqrt(4) 必须精确得到 2");

    const auto irrational_root = rvemu::core::floating_square_root(
        Format::Double, 0x4000'0000'0000'0000ULL, Mode::NearestTiesToEven);
    context.expect(irrational_root.bits == 0x3FF6'A09E'667F'3BCDULL &&
                       irrational_root.flags == flag(rvemu::core::FloatingExceptionFlag::Inexact),
                   "binary64 sqrt(2) 必须得到规范 RNE 位模式并设置 NX");

    const auto double_third = rvemu::core::floating_divide(
        Format::Double,
        0x3FF0'0000'0000'0000ULL,
        0x4008'0000'0000'0000ULL,
        Mode::NearestTiesToEven);
    context.expect(double_third.bits == 0x3FD5'5555'5555'5555ULL &&
                       double_third.flags == flag(rvemu::core::FloatingExceptionFlag::Inexact),
                   "binary64 1/3 必须得到规范 RNE 无限循环商");
}

void test_fused_and_rounding(TestContext& context) {
    // (1+2^-23)^2-(1+2^-22) 的唯一剩余项是 2^-46；若中间乘积先舍入会错误变成零。
    const auto fused = rvemu::core::floating_fused_multiply_add(
        Format::Single,
        0x3F80'0001U,
        0x3F80'0001U,
        0xBF80'0002U,
        false,
        false,
        Mode::NearestTiesToEven);
    context.expect(fused.bits == 0x2880'0000U && fused.flags == 0U,
                   "FMA 必须保留精确乘积并只舍入一次");

    const auto fma_tiny_after_rounding = rvemu::core::floating_fused_multiply_add(
        Format::Single,
        0x0080'0000U,
        0x32C1'1A50U,
        0x007F'FFFFU,
        false,
        false,
        Mode::Up);
    const auto underflow_flags = static_cast<std::uint8_t>(
        flag(rvemu::core::FloatingExceptionFlag::Underflow) |
        flag(rvemu::core::FloatingExceptionFlag::Inexact));
    context.expect(fma_tiny_after_rounding.bits == 0x0080'0000U &&
                       fma_tiny_after_rounding.flags == underflow_flags,
                   "FMA 舍到最小正规数时仍需按无界指数 after-rounding tininess 设置 UF|NX");

    const auto fma_not_tiny_after_rounding = rvemu::core::floating_fused_multiply_add(
        Format::Single,
        0x3F00'FBFFU,
        0x8000'0001U,
        0x807F'FFFFU,
        false,
        false,
        Mode::Down);
    context.expect(fma_not_tiny_after_rounding.bits == 0x8080'0000U &&
                       fma_not_tiny_after_rounding.flags ==
                           flag(rvemu::core::FloatingExceptionFlag::Inexact),
                   "FMA 无界指数舍入已回到最小正规数时不得误报 UF");

    const auto rne = rvemu::core::floating_from_integer(
        Format::Single, 0x0100'0001U, true, 32U, Mode::NearestTiesToEven);
    const auto rmm = rvemu::core::floating_from_integer(
        Format::Single, 0x0100'0001U, true, 32U,
        Mode::NearestTiesToMaximumMagnitude);
    context.expect(rne.bits == 0x4B80'0000U, "RNE 中点必须选择偶数有效数");
    context.expect(rmm.bits == 0x4B80'0001U, "RMM 中点必须远离零");
    context.expect(rne.flags == flag(rvemu::core::FloatingExceptionFlag::Inexact) &&
                       rmm.flags == flag(rvemu::core::FloatingExceptionFlag::Inexact),
                   "整数转浮点丢失精度必须设置 NX");

    const auto negative_down = rvemu::core::floating_add(
        Format::Single, 0xBF80'0000U, 0xB300'0000U, Mode::Down);
    const auto negative_up = rvemu::core::floating_add(
        Format::Single, 0xBF80'0000U, 0xB300'0000U, Mode::Up);
    context.expect(negative_down.bits == 0xBF80'0001U,
                   "RDN 对负数非精确和必须向负无穷增加幅值");
    context.expect(negative_up.bits == 0xBF80'0000U,
                   "RUP 对负数非精确和必须向正无穷取较小幅值");
}

void test_special_values_and_flags(TestContext& context) {
    const auto invalid = rvemu::core::floating_multiply(
        Format::Double, 0x7FF0'0000'0000'0000ULL, 0U, Mode::NearestTiesToEven);
    context.expect(invalid.bits == rvemu::core::kCanonicalDoubleNan,
                   "无穷乘零必须返回 canonical NaN");
    context.expect(invalid.flags == flag(rvemu::core::FloatingExceptionFlag::InvalidOperation),
                   "无穷乘零必须设置 NV");

    const auto divide_zero = rvemu::core::floating_divide(
        Format::Single, 0xBF80'0000U, 0U, Mode::NearestTiesToEven);
    context.expect(divide_zero.bits == 0xFF80'0000U,
                   "负有限数除正零必须得到负无穷");
    context.expect(divide_zero.flags == flag(rvemu::core::FloatingExceptionFlag::DivideByZero),
                   "有限非零除零必须只设置 DZ");

    const auto overflow = rvemu::core::floating_multiply(
        Format::Single, 0x7F7F'FFFFU, 0x4000'0000U, Mode::NearestTiesToEven);
    const auto overflow_rtz = rvemu::core::floating_multiply(
        Format::Single, 0x7F7F'FFFFU, 0x4000'0000U, Mode::TowardZero);
    const auto overflow_flags = static_cast<std::uint8_t>(
        flag(rvemu::core::FloatingExceptionFlag::Overflow) |
        flag(rvemu::core::FloatingExceptionFlag::Inexact));
    context.expect(overflow.bits == 0x7F80'0000U && overflow.flags == overflow_flags,
                   "RNE 正溢出必须得到正无穷并设置 OF|NX");
    context.expect(overflow_rtz.bits == 0x7F7F'FFFFU && overflow_rtz.flags == overflow_flags,
                   "RTZ 正溢出必须饱和到最大有限数并设置 OF|NX");

    const auto underflow = rvemu::core::floating_divide(
        Format::Single, 0x0000'0001U, 0x4000'0000U, Mode::NearestTiesToEven);
    const auto underflow_flags = static_cast<std::uint8_t>(
        flag(rvemu::core::FloatingExceptionFlag::Underflow) |
        flag(rvemu::core::FloatingExceptionFlag::Inexact));
    context.expect(underflow.bits == 0U && underflow.flags == underflow_flags,
                   "最小次正规数除二必须按偶数舍为零并设置 UF|NX");

    const auto negative_root = rvemu::core::floating_square_root(
        Format::Single, 0xBF80'0000U, Mode::NearestTiesToEven);
    context.expect(negative_root.bits == rvemu::core::kCanonicalSingleNan &&
                       negative_root.flags == flag(rvemu::core::FloatingExceptionFlag::InvalidOperation),
                   "负有限数平方根必须返回 canonical NaN 并设置 NV");
}

void test_non_arithmetic_operations(TestContext& context) {
    const auto injected = rvemu::core::floating_sign_inject(
        Format::Single, 0x3F80'0000U, 0xBF00'0000U, 0U);
    context.expect(injected == 0xBF80'0000U, "FSGNJ 必须只复制符号位");

    const auto minimum = rvemu::core::floating_minimum_maximum(
        Format::Single, 0x7FC0'1234U, 0x4000'0000U, false);
    context.expect(minimum.bits == 0x4000'0000U && minimum.flags == 0U,
                   "FMIN 遇到单个 qNaN 必须返回数值操作数");
    const auto signaling_minimum = rvemu::core::floating_minimum_maximum(
        Format::Single, 0x7F80'0001U, 0x4000'0000U, false);
    context.expect(signaling_minimum.bits == 0x4000'0000U &&
                       signaling_minimum.flags == flag(rvemu::core::FloatingExceptionFlag::InvalidOperation),
                   "FMIN 遇到 sNaN 必须返回数值操作数并设置 NV");

    const auto quiet_compare = rvemu::core::floating_compare(
        Format::Double,
        rvemu::core::kCanonicalDoubleNan,
        0x3FF0'0000'0000'0000ULL,
        rvemu::core::FloatingComparison::Equal);
    const auto signaling_compare = rvemu::core::floating_compare(
        Format::Double,
        rvemu::core::kCanonicalDoubleNan,
        0x3FF0'0000'0000'0000ULL,
        rvemu::core::FloatingComparison::LessThan);
    context.expect(!quiet_compare.value && quiet_compare.flags == 0U,
                   "FEQ 对 qNaN 必须安静地返回假");
    context.expect(!signaling_compare.value &&
                       signaling_compare.flags == flag(rvemu::core::FloatingExceptionFlag::InvalidOperation),
                   "FLT 对 qNaN 也必须设置 NV");

    context.expect(rvemu::core::floating_classify(Format::Single, 0xFF80'0000U) == 1U,
                   "FCLASS 必须识别负无穷");
    context.expect(rvemu::core::floating_classify(Format::Single, 0x0000'0001U) == (1U << 5U),
                   "FCLASS 必须识别正次正规数");
    context.expect(rvemu::core::floating_classify(Format::Single, 0x7F80'0001U) == (1U << 8U),
                   "FCLASS 必须区分 signaling NaN");
}

void test_conversions(TestContext& context) {
    const auto narrowed = rvemu::core::floating_convert_format(
        Format::Single,
        Format::Double,
        0x3FF8'0000'0000'0000ULL,
        Mode::NearestTiesToEven);
    const auto widened = rvemu::core::floating_convert_format(
        Format::Double,
        Format::Single,
        0xBFC0'0000U,
        Mode::NearestTiesToEven);
    context.expect(narrowed.bits == 0x3FC0'0000U && narrowed.flags == 0U,
                   "FCVT.S.D 必须精确转换 1.5");
    context.expect(widened.bits == 0xBFF8'0000'0000'0000ULL && widened.flags == 0U,
                   "FCVT.D.S 必须精确拓宽 -1.5");

    const auto toward_zero = rvemu::core::floating_to_integer(
        Format::Single, 0xBFC0'0000U, false, 32U, Mode::TowardZero);
    const auto nearest = rvemu::core::floating_to_integer(
        Format::Single, 0xBFC0'0000U, false, 32U, Mode::NearestTiesToEven);
    context.expect(toward_zero.value == 0xFFFF'FFFFU &&
                       toward_zero.flags == flag(rvemu::core::FloatingExceptionFlag::Inexact),
                   "-1.5 RTZ 转 i32 必须得到 -1 并设置 NX");
    context.expect(nearest.value == 0xFFFF'FFFEU &&
                       nearest.flags == flag(rvemu::core::FloatingExceptionFlag::Inexact),
                   "-1.5 RNE 转 i32 必须得到 -2 并设置 NX");

    const auto nan_to_unsigned = rvemu::core::floating_to_integer(
        Format::Double,
        rvemu::core::kCanonicalDoubleNan,
        true,
        64U,
        Mode::NearestTiesToEven);
    context.expect(nan_to_unsigned.value == 0xFFFF'FFFF'FFFF'FFFFULL &&
                       nan_to_unsigned.flags == flag(rvemu::core::FloatingExceptionFlag::InvalidOperation),
                   "NaN 转 u64 必须返回最大值并设置 NV");
}

}  // namespace

int main() {
    TestContext context;
    try {
        test_basic_arithmetic(context);
        test_fused_and_rounding(context);
        test_special_values_and_flags(context);
        test_non_arithmetic_operations(context);
        test_conversions(context);
    } catch (const std::exception& error) {
        std::cerr << "浮点软件核心测试异常：" << error.what() << '\n';
        return 1;
    }
    if (context.failures() != 0) {
        std::cerr << "浮点软件核心测试失败数：" << context.failures() << '\n';
        return 1;
    }
    std::cout << "浮点软件核心算术、舍入、异常与转换测试通过。\n";
    return 0;
}
