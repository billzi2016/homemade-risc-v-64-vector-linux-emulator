// 文件职责：声明单 Hart CLINT 的真实 MMIO 寄存器、单调计时和中断电平投影接口。
// 边界：CLINT 不决定中断委托、全局使能、优先级或 Trap 入口；这些仍由唯一 CSR/CPU 路径处理。

#pragma once

#include "rvemu/bus/address_region.hpp"

#include <cstdint>
#include <mutex>

namespace rvemu::core {
class CsrFile;
}

namespace rvemu::devices {

/**
 * 单 Hart Core Local Interruptor，提供 OpenSBI/Linux 使用的标准兼容寄存器布局。
 *
 * 所有 MMIO 状态由同一把锁保护；主循环在指令边界调用 advance 与 synchronize，
 * 因而设备不保存 CSR 副本，也不会自行转移 CPU 控制流。
 */
class Clint final : public bus::AddressRegion {
   public:
    static constexpr std::uint64_t kMsipOffset = 0x0000U;
    static constexpr std::uint64_t kMtimecmpOffset = 0x4000U;
    static constexpr std::uint64_t kMtimeOffset = 0xBFF8U;

    Clint();

    Clint(const Clint&) = delete;
    Clint& operator=(const Clint&) = delete;
    Clint(Clint&&) = delete;
    Clint& operator=(Clint&&) = delete;

    /**
     * 推进确定性时钟的 tick 数；无符号回绕遵循寄存器位模式，调用方负责频率换算。
     *
     * 参数：ticks 为自上次设备同步后的非负离散 tick。
     */
    void advance(std::uint64_t ticks) noexcept;

    /**
     * 将当前 msip 与 mtime/mtimecmp 比较结果投影到唯一的 CSR pending 位。
     *
     * 本函数只更新 MSIP/MTIP 电平，不解释 mie、mideleg 或特权模式，且清除本设备
     * 的电平不会影响 PLIC 等其他中断源。
     */
    void synchronize(core::CsrFile& csrs) const noexcept;

    [[nodiscard]] bus::AccessResult read(std::uint64_t offset,
                                         bus::AccessWidth width,
                                         bus::AccessType type) override;

    [[nodiscard]] bus::AccessResult write(std::uint64_t offset,
                                          bus::AccessWidth width,
                                          std::uint64_t value,
                                          bus::AccessType type) override;

    /** CLINT 没有原子 MMIO 语义；LR/SC/AMO 必须作为非法设备访问被拒绝。 */
    [[nodiscard]] bus::AccessResult compare_exchange(std::uint64_t offset,
                                                     bus::AccessWidth width,
                                                     std::uint64_t expected,
                                                     std::uint64_t desired,
                                                     bus::AccessType type) override;

   private:
    /** 验证寄存器窗口、允许宽度和自然对齐，失败时返回可诊断的总线错误。 */
    [[nodiscard]] bus::AccessResult validate_access(std::uint64_t offset,
                                                    bus::AccessWidth width) const;
    /** 判断访问是否完整落在某个 64 位定时寄存器的低/高半字或完整字范围内。 */
    [[nodiscard]] static bool is_timer_register_access(std::uint64_t offset,
                                                       bus::AccessWidth width,
                                                       std::uint64_t register_offset) noexcept;
    /** 根据 32 或 64 位写入定位更新寄存器的低/高部分，保留未覆盖的位。 */
    static void write_timer_register(std::uint64_t& destination,
                                     std::uint64_t offset,
                                     std::uint64_t register_offset,
                                     bus::AccessWidth width,
                                     std::uint64_t value) noexcept;
    /** 读取完整字或指定半字，所有来宾可见值均以小端位布局返回。 */
    [[nodiscard]] static std::uint64_t read_timer_register(std::uint64_t source,
                                                           std::uint64_t offset,
                                                           std::uint64_t register_offset,
                                                           bus::AccessWidth width) noexcept;

    mutable std::mutex mutex_;
    std::uint64_t mtime_{0U};
    std::uint64_t mtimecmp_{~0ULL};
    bool msip_{false};
};

}  // namespace rvemu::devices
