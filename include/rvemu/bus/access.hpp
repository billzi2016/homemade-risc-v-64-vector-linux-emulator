// 文件职责：定义物理总线访问宽度、访问来源、结构化错误和统一访问结果。
// 边界：这里只描述访问契约，不负责定位地址区域或读写任何来宾状态。

#pragma once

#include "rvemu/bus/physical_address.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace rvemu::bus {

enum class AccessWidth : std::uint8_t {
    Byte = 1U,
    HalfWord = 2U,
    Word = 4U,
    DoubleWord = 8U,
};

[[nodiscard]] constexpr std::uint64_t width_in_bytes(AccessWidth width) noexcept {
    switch (width) {
    case AccessWidth::Byte:
        return 1U;
    case AccessWidth::HalfWord:
        return 2U;
    case AccessWidth::Word:
        return 4U;
    case AccessWidth::DoubleWord:
        return 8U;
    }
    return 0U;
}

enum class AccessType : std::uint8_t {
    InstructionFetch,
    Load,
    Store,
    Atomic,
    DmaRead,
    DmaWrite,
    Initialization,
};

enum class BusErrorCode : std::uint8_t {
    None,
    InvalidWidth,
    AddressOverflow,
    Unmapped,
    OutOfBounds,
    ReadOnly,
    RegionOverlap,
    InvalidRegion,
    BackendFailure,
};

struct BusError final {
    BusErrorCode code{BusErrorCode::None};
    PhysicalAddress address{};
    std::uint64_t length{0U};
    std::string region{};
    std::string detail{};
};

// 统一结果同时服务读、写和 compare-exchange：读返回 value，原子操作还返回 exchanged。
// 调用方必须先检查 ok()，失败时 value/exchanged 不具有架构含义。
struct AccessResult final {
    BusError error{};
    std::uint64_t value{0U};
    bool exchanged{false};

    [[nodiscard]] bool ok() const noexcept { return error.code == BusErrorCode::None; }

    [[nodiscard]] static AccessResult success(
        std::uint64_t result_value = 0U,
        bool did_exchange = false) noexcept {
        AccessResult result{};
        result.value = result_value;
        result.exchanged = did_exchange;
        return result;
    }

    [[nodiscard]] static AccessResult failure(BusError failure_error) {
        AccessResult result{};
        result.error = std::move(failure_error);
        return result;
    }
};

// 保留 token 只用于把某次 LR 与后续 SC 关联起来；零值永久表示“当前没有保留”。
// 地址范围与失效状态由总线私有监视器保存，CPU 不得根据 token 猜测物理地址。
struct ReservationToken final {
    std::uint64_t value{0U};

    [[nodiscard]] bool valid() const noexcept { return value != 0U; }
};

// LR 必须把读取值与保留建立作为一个不可分割事务，因此不能用两个独立返回值调用完成。
struct LoadReservedResult final {
    AccessResult access{};
    ReservationToken token{};
};

}  // namespace rvemu::bus
