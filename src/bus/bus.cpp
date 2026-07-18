// 文件职责：实现唯一物理总线的区域注册、稳定排序、边界检查和访问分发。
// 边界：本文件不解释区域内部字节或寄存器，也不执行虚拟地址翻译。

#include "rvemu/bus/bus.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace rvemu::bus {
namespace {

constexpr std::uint64_t kReservationGranuleBytes = 8U;

[[nodiscard]] BusError make_error(
    BusErrorCode code,
    PhysicalAddress address,
    std::uint64_t length,
    std::string region,
    std::string detail) {
    return BusError{code, address, length, std::move(region), std::move(detail)};
}

[[nodiscard]] constexpr PhysicalAddress reservation_base(PhysicalAddress address) noexcept {
    return PhysicalAddress{address.value() & ~(kReservationGranuleBytes - 1U)};
}

}  // namespace

AccessResult Bus::register_region(std::shared_ptr<AddressRegion> region) {
    std::lock_guard<std::mutex> transaction_lock{transaction_mutex_};
    if (region == nullptr) {
        return AccessResult::failure(make_error(
            BusErrorCode::InvalidRegion,
            PhysicalAddress{},
            0U,
            {},
            "不能向物理总线注册空区域"));
    }

    for (const auto& registered : regions_) {
        if (region->range().overlaps(registered->range())) {
            return AccessResult::failure(make_error(
                BusErrorCode::RegionOverlap,
                region->range().base(),
                region->range().size(),
                region->name(),
                "物理地址区域与已注册区域 " + registered->name() + " 重叠"));
        }
    }

    regions_.push_back(std::move(region));
    std::sort(
        regions_.begin(),
        regions_.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs->range().base() < rhs->range().base();
        });
    return AccessResult::success();
}

Bus::LocatedRegion Bus::locate(PhysicalAddress address, AccessWidth width) {
    const auto length = width_in_bytes(width);
    if (length == 0U) {
        return LocatedRegion{
            nullptr,
            0U,
            AccessResult::failure(make_error(
                BusErrorCode::InvalidWidth,
                address,
                0U,
                {},
                "物理总线只接受 8、16、32 或 64 位访问"))};
    }

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (address.value() > maximum - (length - 1U)) {
        return LocatedRegion{
            nullptr,
            0U,
            AccessResult::failure(make_error(
                BusErrorCode::AddressOverflow,
                address,
                length,
                {},
                "物理访问末地址超出 64 位地址空间"))};
    }

    for (const auto& region : regions_) {
        if (!region->range().contains(address)) {
            continue;
        }
        if (!region->range().contains(address, length)) {
            return LocatedRegion{
                nullptr,
                0U,
                AccessResult::failure(make_error(
                    BusErrorCode::OutOfBounds,
                    address,
                    length,
                    region->name(),
                    "访问跨越物理区域末边界"))};
        }
        return LocatedRegion{region.get(), region->range().offset_of(address), AccessResult::success()};
    }

    return LocatedRegion{
        nullptr,
        0U,
        AccessResult::failure(make_error(
            BusErrorCode::Unmapped,
            address,
            length,
            {},
            "物理地址没有映射到任何已注册区域"))};
}

AccessResult Bus::read(PhysicalAddress address, AccessWidth width, AccessType type) {
    auto located = locate(address, width);
    if (located.region == nullptr) {
        return located.failure;
    }
    return located.region->read(located.offset, width, type);
}

AccessResult Bus::write(
    PhysicalAddress address,
    AccessWidth width,
    std::uint64_t value,
    AccessType type) {
    std::lock_guard<std::mutex> transaction_lock{transaction_mutex_};
    auto located = locate(address, width);
    if (located.region == nullptr) {
        return located.failure;
    }
    const auto result = located.region->write(located.offset, width, value, type);
    if (result.ok() && type == AccessType::DmaWrite) {
        invalidate_reservation_locked(address, width_in_bytes(width));
    }
    return result;
}

AccessResult Bus::compare_exchange(
    PhysicalAddress address,
    AccessWidth width,
    std::uint64_t expected,
    std::uint64_t desired,
    AccessType type) {
    std::lock_guard<std::mutex> transaction_lock{transaction_mutex_};
    auto located = locate(address, width);
    if (located.region == nullptr) {
        return located.failure;
    }
    const auto result = located.region->compare_exchange(
        located.offset, width, expected, desired, type);
    if (result.ok() && result.exchanged) {
        invalidate_reservation_locked(address, width_in_bytes(width));
    }
    return result;
}

LoadReservedResult Bus::load_reserved(PhysicalAddress address, AccessWidth width) {
    std::lock_guard<std::mutex> transaction_lock{transaction_mutex_};
    auto located = locate(address, width);
    if (located.region == nullptr) {
        return LoadReservedResult{located.failure, ReservationToken{}};
    }

    const auto loaded = located.region->read(located.offset, width, AccessType::Atomic);
    if (!loaded.ok()) {
        return LoadReservedResult{loaded, ReservationToken{}};
    }

    const auto token = next_reservation_token_locked();
    reservation_ = ReservationRecord{
        token,
        address,
        reservation_base(address),
        kReservationGranuleBytes};
    return LoadReservedResult{loaded, token};
}

AccessResult Bus::store_conditional(
    ReservationToken token,
    PhysicalAddress address,
    AccessWidth width,
    std::uint64_t value) {
    std::lock_guard<std::mutex> transaction_lock{transaction_mutex_};

    // SC 无条件消费当前保留。先复制再清除，保证后端错误或条件失败也不能重复使用 token。
    const auto prior_reservation = reservation_;
    reservation_.reset();

    auto located = locate(address, width);
    if (located.region == nullptr) {
        return located.failure;
    }

    const auto length = width_in_bytes(width);
    const auto matches = token.valid() && prior_reservation.has_value() &&
                         prior_reservation->token.value == token.value &&
                         prior_reservation->load_address == address &&
                         reservation_base(address) == prior_reservation->reserved_base &&
                         length <= prior_reservation->length;
    if (!matches) {
        return AccessResult::success(0U, false);
    }

    const auto stored = located.region->write(
        located.offset, width, value, AccessType::Atomic);
    if (!stored.ok()) {
        return stored;
    }
    return AccessResult::success(0U, true);
}

void Bus::invalidate_reservation_locked(
    PhysicalAddress address,
    std::uint64_t length) noexcept {
    if (!reservation_.has_value() || length == 0U) {
        return;
    }

    const auto write_first = address.value();
    const auto write_last = write_first + length - 1U;
    const auto reserved_first = reservation_->reserved_base.value();
    const auto reserved_last = reserved_first + reservation_->length - 1U;
    if (write_first <= reserved_last && reserved_first <= write_last) {
        reservation_.reset();
    }
}

ReservationToken Bus::next_reservation_token_locked() noexcept {
    auto token = ReservationToken{next_reservation_token_++};
    if (!token.valid()) {
        token = ReservationToken{next_reservation_token_++};
    }
    return token;
}

}  // namespace rvemu::bus
