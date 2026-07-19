// 文件职责：实现项目声明的 RVV 1.0 配置能力和规范要求的确定性向量长度选择。
// 边界：本文件是纯值计算，不访问 CpuState、CSR、总线或宿主资源，因此不承担指令副作用与 Trap 创建。

#include "rvemu/vector/vector_configuration.hpp"

namespace rvemu::vector {
namespace {

constexpr std::uint64_t kVtypeImplementedMask = 0xFFU;
constexpr std::uint64_t kMaximumElementLengthBits = 64U;

// 只从标准 vlmul 编码导出有理 LMUL。0b100 为保留值，不能被误当作 m1 或分数配置。
[[nodiscard]] bool decode_lmul(std::uint8_t encoding,
                               std::uint64_t& numerator,
                               std::uint64_t& denominator) noexcept {
    switch (encoding) {
        case 0U:
            numerator = 1U;
            denominator = 1U;
            return true;
        case 1U:
            numerator = 2U;
            denominator = 1U;
            return true;
        case 2U:
            numerator = 4U;
            denominator = 1U;
            return true;
        case 3U:
            numerator = 8U;
            denominator = 1U;
            return true;
        case 5U:
            numerator = 1U;
            denominator = 8U;
            return true;
        case 6U:
            numerator = 1U;
            denominator = 4U;
            return true;
        case 7U:
            numerator = 1U;
            denominator = 2U;
            return true;
        default:
            return false;
    }
}

// 非法完整 vtype 的架构可见状态不可保留原始字段，必须严格归一为 vill 和零容量。
[[nodiscard]] VectorConfiguration illegal_configuration() noexcept {
    return VectorConfiguration{};
}

}  // namespace

VectorConfiguration decode_vector_configuration(std::uint64_t vtype) noexcept {
    // vill 本身或 XLEN-2:8 任一保留位均表示软件请求了当前硬件不支持的完整配置。
    if ((vtype & kVtypeVill) != 0U || (vtype & ~kVtypeImplementedMask) != 0U) {
        return illegal_configuration();
    }

    const auto vsew = static_cast<std::uint8_t>((vtype >> 3U) & 0x7U);
    if (vsew > 3U) {
        return illegal_configuration();
    }
    const auto sew_bits = 8ULL << vsew;

    std::uint64_t numerator = 0U;
    std::uint64_t denominator = 1U;
    if (!decode_lmul(static_cast<std::uint8_t>(vtype & 0x7U), numerator, denominator)) {
        return illegal_configuration();
    }

    // RVV 的分数 LMUL 可用 SEW 受 SEW<=LMUL*ELEN 限制；这里 ELEN 固定为 64 位。
    if (sew_bits > (kMaximumElementLengthBits * numerator) / denominator) {
        return illegal_configuration();
    }

    const auto elements_per_register = kVectorLengthBits / sew_bits;
    const auto vlmax = (elements_per_register * numerator) / denominator;
    if (vlmax == 0U) {
        return illegal_configuration();
    }
    return VectorConfiguration{true, vtype, sew_bits, numerator, denominator, vlmax};
}

std::uint64_t select_vector_length(std::uint64_t application_vector_length,
                                   std::uint64_t vlmax) noexcept {
    if (vlmax == 0U) {
        return 0U;
    }
    return application_vector_length < vlmax ? application_vector_length : vlmax;
}

bool can_preserve_vector_length(std::uint64_t previous_vtype,
                                std::uint64_t requested_vtype) noexcept {
    const auto previous = decode_vector_configuration(previous_vtype);
    const auto requested = decode_vector_configuration(requested_vtype);
    return previous.valid && requested.valid && previous.vlmax == requested.vlmax;
}

std::optional<VectorConfiguration> derive_memory_configuration(
    const VectorConfiguration& current, std::uint64_t element_width_bits) noexcept {
    if (!current.valid || element_width_bits < 8U || element_width_bits > 64U
        || (element_width_bits & (element_width_bits - 1U)) != 0U) {
        return std::nullopt;
    }
    // EMUL=(EEW/SEW)*LMUL；所有量都很小，但先约分可避免把合法 mf* 组合错误拒绝。
    auto numerator = current.lmul_numerator * element_width_bits;
    auto denominator = current.lmul_denominator * current.sew_bits;
    while ((numerator & 1U) == 0U && (denominator & 1U) == 0U) {
        numerator >>= 1U;
        denominator >>= 1U;
    }
    std::uint64_t ignored_numerator = 0U;
    std::uint64_t ignored_denominator = 1U;
    bool supported = false;
    for (std::uint8_t encoding = 0U; encoding < 8U; ++encoding) {
        if (decode_lmul(encoding, ignored_numerator, ignored_denominator)
            && ignored_numerator == numerator && ignored_denominator == denominator) {
            supported = true;
            break;
        }
    }
    if (!supported) {
        return std::nullopt;
    }
    return VectorConfiguration{
        true,
        current.vtype,
        element_width_bits,
        numerator,
        denominator,
        current.vlmax,
    };
}

}  // namespace rvemu::vector
