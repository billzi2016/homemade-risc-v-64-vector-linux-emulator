// 文件职责：实现最小 16550A 兼容 UART 的寄存器复用、FIFO、LSR/IIR 状态和 PLIC 电平输出。
// 边界：本文件不读写宿主终端、不阻塞 CPU 主循环，也不生成 FDT 或解释中断委托。

#include "rvemu/devices/uart16550.hpp"

#include "rvemu/bus/address_map.hpp"
#include "rvemu/devices/plic.hpp"

#include <stdexcept>

namespace rvemu::devices {
namespace {

constexpr std::size_t kFifoCapacity = 16U;
constexpr std::uint8_t kIerRxDataAvailable = 1U << 0U;
constexpr std::uint8_t kIerTxHoldingEmpty = 1U << 1U;
constexpr std::uint8_t kIerReceiverLineStatus = 1U << 2U;
constexpr std::uint8_t kFcrEnableFifo = 1U << 0U;
constexpr std::uint8_t kFcrClearRx = 1U << 1U;
constexpr std::uint8_t kFcrClearTx = 1U << 2U;
constexpr std::uint8_t kLcrDlab = 1U << 7U;
constexpr std::uint8_t kLsrDataReady = 1U << 0U;
constexpr std::uint8_t kLsrOverrunError = 1U << 1U;
constexpr std::uint8_t kLsrThrEmpty = 1U << 5U;
constexpr std::uint8_t kLsrTransmitterEmpty = 1U << 6U;
constexpr std::uint8_t kIirFifosEnabled = 0xC0U;
constexpr std::uint8_t kWritableIerMask = kIerRxDataAvailable | kIerTxHoldingEmpty
                                          | kIerReceiverLineStatus | (1U << 3U);

[[nodiscard]] bus::AddressRange require_uart_range() {
    const auto range = bus::AddressRange::create(
        bus::PhysicalAddress{bus::address_map::kUart.base}, bus::address_map::kUart.size);
    if (!range.has_value()) {
        throw std::logic_error("UART 固定物理地址范围无效");
    }
    return *range;
}

[[nodiscard]] bus::AccessResult invalid_access(std::uint64_t offset,
                                               bus::AccessWidth width,
                                               const char* detail) {
    return bus::AccessResult::failure(
        bus::BusError{bus::BusErrorCode::InvalidWidth,
                      bus::PhysicalAddress{bus::address_map::kUart.base + offset},
                      bus::width_in_bytes(width),
                      bus::address_map::kUart.name,
                      detail});
}

}  // namespace

Uart16550::Uart16550(std::uint32_t interrupt_source)
    : AddressRegion(bus::address_map::kUart.name, require_uart_range()),
      interrupt_source_(interrupt_source) {
}

bool Uart16550::push_rx(std::uint8_t byte) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (rx_fifo_.size() >= kFifoCapacity) {
        overrun_error_ = true;
        return false;
    }
    rx_fifo_.push_back(byte);
    return true;
}

bool Uart16550::pop_tx(std::uint8_t& byte) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (tx_fifo_.empty()) {
        return false;
    }
    byte = tx_fifo_.front();
    tx_fifo_.pop_front();
    return true;
}

void Uart16550::synchronize(Plic& plic) const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    plic.set_source_level(interrupt_source_, current_interrupt_locked() != InterruptId::None);
}

bus::AccessResult Uart16550::validate_access(std::uint64_t offset,
                                             bus::AccessWidth width) const {
    if (width != bus::AccessWidth::Byte) {
        return invalid_access(offset, width, "UART 16550A 寄存器仅支持 8 位 MMIO 访问");
    }
    if (offset > kScrOffset) {
        return invalid_access(offset, width, "UART 未实现该寄存器偏移");
    }
    return bus::AccessResult::success();
}

bool Uart16550::dlab_enabled() const noexcept {
    return (lcr_ & kLcrDlab) != 0U;
}

Uart16550::InterruptId Uart16550::current_interrupt_locked() const noexcept {
    if ((ier_ & kIerReceiverLineStatus) != 0U && overrun_error_) {
        return InterruptId::ReceiverLineStatus;
    }
    if ((ier_ & kIerRxDataAvailable) != 0U && !rx_fifo_.empty()) {
        return InterruptId::RxDataAvailable;
    }
    if ((ier_ & kIerTxHoldingEmpty) != 0U && tx_fifo_.empty()) {
        return InterruptId::TxHoldingEmpty;
    }
    return InterruptId::None;
}

void Uart16550::reset_rx_fifo_locked() noexcept {
    rx_fifo_.clear();
    overrun_error_ = false;
}

void Uart16550::reset_tx_fifo_locked() noexcept {
    tx_fifo_.clear();
}

bus::AccessResult Uart16550::read(std::uint64_t offset,
                                  bus::AccessWidth width,
                                  bus::AccessType type) {
    static_cast<void>(type);
    const auto validation = validate_access(offset, width);
    if (!validation.ok()) {
        return validation;
    }

    std::lock_guard<std::mutex> lock{mutex_};
    switch (offset) {
        case kRbrThrDllOffset:
            if (dlab_enabled()) {
                return bus::AccessResult::success(dll_);
            }
            if (rx_fifo_.empty()) {
                return bus::AccessResult::success(0U);
            }
            {
                const auto byte = rx_fifo_.front();
                rx_fifo_.pop_front();
                return bus::AccessResult::success(byte);
            }
        case kIerDlmOffset:
            return bus::AccessResult::success(dlab_enabled() ? dlm_ : ier_);
        case kIirFcrOffset:
            return bus::AccessResult::success(kIirFifosEnabled
                                              | static_cast<std::uint8_t>(
                                                    current_interrupt_locked()));
        case kLcrOffset:
            return bus::AccessResult::success(lcr_);
        case kMcrOffset:
            return bus::AccessResult::success(mcr_);
        case kLsrOffset: {
            const auto value = static_cast<std::uint8_t>(
                (!rx_fifo_.empty() ? kLsrDataReady : 0U)
                | (overrun_error_ ? kLsrOverrunError : 0U) | kLsrThrEmpty
                | kLsrTransmitterEmpty);
            overrun_error_ = false;
            return bus::AccessResult::success(value);
        }
        case kMsrOffset:
            return bus::AccessResult::success(0U);
        case kScrOffset:
            return bus::AccessResult::success(scr_);
        default:
            break;
    }
    return bus::AccessResult::success();
}

bus::AccessResult Uart16550::write(std::uint64_t offset,
                                   bus::AccessWidth width,
                                   std::uint64_t value,
                                   bus::AccessType type) {
    static_cast<void>(type);
    const auto validation = validate_access(offset, width);
    if (!validation.ok()) {
        return validation;
    }

    const auto byte = static_cast<std::uint8_t>(value);
    std::lock_guard<std::mutex> lock{mutex_};
    switch (offset) {
        case kRbrThrDllOffset:
            if (dlab_enabled()) {
                dll_ = byte;
            } else if (tx_fifo_.size() < kFifoCapacity) {
                tx_fifo_.push_back(byte);
            }
            return bus::AccessResult::success();
        case kIerDlmOffset:
            if (dlab_enabled()) {
                dlm_ = byte;
            } else {
                ier_ = byte & kWritableIerMask;
            }
            return bus::AccessResult::success();
        case kIirFcrOffset:
            fcr_ = byte & 0xC9U;
            if ((byte & kFcrClearRx) != 0U) {
                reset_rx_fifo_locked();
            }
            if ((byte & kFcrClearTx) != 0U) {
                reset_tx_fifo_locked();
            }
            if ((byte & kFcrEnableFifo) == 0U) {
                fcr_ = 0U;
            }
            return bus::AccessResult::success();
        case kLcrOffset:
            lcr_ = byte;
            return bus::AccessResult::success();
        case kMcrOffset:
            mcr_ = byte & 0x1FU;
            return bus::AccessResult::success();
        case kLsrOffset:
        case kMsrOffset:
            return bus::AccessResult::success();
        case kScrOffset:
            scr_ = byte;
            return bus::AccessResult::success();
        default:
            break;
    }
    return bus::AccessResult::success();
}

bus::AccessResult Uart16550::compare_exchange(std::uint64_t offset,
                                              bus::AccessWidth width,
                                              std::uint64_t expected,
                                              std::uint64_t desired,
                                              bus::AccessType type) {
    static_cast<void>(expected);
    static_cast<void>(desired);
    static_cast<void>(type);
    return invalid_access(offset, width, "UART 不支持原子 MMIO 访问");
}

}  // namespace rvemu::devices
