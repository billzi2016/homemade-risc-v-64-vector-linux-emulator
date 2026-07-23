// 文件职责：声明最小 16550A 兼容 UART 的 MMIO 寄存器、FIFO 状态和 PLIC 中断投影接口。
// 边界：UART 只维护来宾可见串口设备状态；宿主终端 Raw 模式、非阻塞 I/O 和主循环调度由运行时层实现。

#pragma once

#include "rvemu/bus/address_region.hpp"

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace rvemu::devices {

class Plic;

/**
 * 单端口 16550A 兼容 UART，提供 Linux 8250 探测和轮询/中断控制台所需寄存器。
 *
 * 所有寄存器和 FIFO 状态由同一把锁保护。设备通过 PLIC source 电平反映 IER/IIR
 * 计算出的中断条件，但不直接访问 CSR，也不负责宿主终端模式切换。
 */
class Uart16550 final : public bus::AddressRegion {
   public:
    static constexpr std::uint32_t kDefaultInterruptSource = 10U;
    static constexpr std::uint64_t kRbrThrDllOffset = 0U;
    static constexpr std::uint64_t kIerDlmOffset = 1U;
    static constexpr std::uint64_t kIirFcrOffset = 2U;
    static constexpr std::uint64_t kLcrOffset = 3U;
    static constexpr std::uint64_t kMcrOffset = 4U;
    static constexpr std::uint64_t kLsrOffset = 5U;
    static constexpr std::uint64_t kMsrOffset = 6U;
    static constexpr std::uint64_t kScrOffset = 7U;

    explicit Uart16550(std::uint32_t interrupt_source = kDefaultInterruptSource);

    Uart16550(const Uart16550&) = delete;
    Uart16550& operator=(const Uart16550&) = delete;
    Uart16550(Uart16550&&) = delete;
    Uart16550& operator=(Uart16550&&) = delete;

    /**
     * 注入一个宿主输入字节到接收 FIFO。
     *
     * 返回值表示字节是否被接受；FIFO 满时设置 overrun 状态并保持已有字节不变。
     */
    [[nodiscard]] bool push_rx(std::uint8_t byte);

    /**
     * 取出来宾已经写入 THR 的字节，用于后续 DEV-004/RUN-002 宿主终端后端消费。
     *
     * 返回 false 表示当前没有待发送字节；本函数不会改变来宾寄存器配置。
     */
    [[nodiscard]] bool pop_tx(std::uint8_t& byte);

    /** 依据当前 IER、FIFO 和 THR 状态更新 PLIC 中对应 source 的电平。 */
    void synchronize(Plic& plic) const noexcept;

    [[nodiscard]] bus::AccessResult read(std::uint64_t offset,
                                         bus::AccessWidth width,
                                         bus::AccessType type) override;
    [[nodiscard]] bus::AccessResult write(std::uint64_t offset,
                                          bus::AccessWidth width,
                                          std::uint64_t value,
                                          bus::AccessType type) override;
    /** UART 不支持 LR/SC/AMO；原子 MMIO 访问必须作为结构化设备错误返回。 */
    [[nodiscard]] bus::AccessResult compare_exchange(std::uint64_t offset,
                                                     bus::AccessWidth width,
                                                     std::uint64_t expected,
                                                     std::uint64_t desired,
                                                     bus::AccessType type) override;

   private:
    enum class InterruptId : std::uint8_t {
        ModemStatus = 0x00U,
        TxHoldingEmpty = 0x02U,
        RxDataAvailable = 0x04U,
        ReceiverLineStatus = 0x06U,
        None = 0x01U,
    };

    /** 16550A 寄存器是字节寻址；更宽或越界访问会拒绝以避免跨寄存器副作用。 */
    [[nodiscard]] bus::AccessResult validate_access(std::uint64_t offset,
                                                    bus::AccessWidth width) const;
    /** 判断 DLAB 是否打开；打开后偏移 0/1 访问除数锁存器而不是 RBR/THR/IER。 */
    [[nodiscard]] bool dlab_enabled() const noexcept;
    /** 按 16550A 优先级计算 IIR 原因，同时考虑 IER 门控。 */
    [[nodiscard]] InterruptId current_interrupt_locked() const noexcept;
    /** 清空接收 FIFO，并按 FCR 复位接收侧错误状态。 */
    void reset_rx_fifo_locked() noexcept;
    /** 清空发送 FIFO；THR/TEMT 会立即恢复为空闲状态。 */
    void reset_tx_fifo_locked() noexcept;

    mutable std::mutex mutex_;
    std::uint32_t interrupt_source_;
    std::deque<std::uint8_t> rx_fifo_{};
    std::deque<std::uint8_t> tx_fifo_{};
    std::uint8_t ier_{0U};
    std::uint8_t fcr_{0U};
    std::uint8_t lcr_{0x03U};
    std::uint8_t mcr_{0U};
    std::uint8_t scr_{0U};
    std::uint8_t dll_{0U};
    std::uint8_t dlm_{0U};
    bool overrun_error_{false};
};

}  // namespace rvemu::devices
