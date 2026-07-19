// 文件职责：声明单 Hart PLIC 的 MMIO 裁决、context claim/complete 与外部中断电平投影。
// 边界：PLIC 不处理设备后端、CSR 委托或 Trap 跳转；设备仅通过 set_source_level 驱动 source 电平。

#pragma once

#include "rvemu/bus/address_region.hpp"

#include <array>
#include <cstdint>
#include <mutex>

namespace rvemu::core {
class CsrFile;
}

namespace rvemu::devices {

/**
 * 单 Hart PLIC，提供 M-mode 与 S-mode context 的外部中断仲裁。
 *
 * Source 为电平触发：claim 会占用 source，complete 释放占用；若设备线仍为高，
 * 该 source 会再次进入候选集。优先级相同的 source 以较小 ID 获胜。
 */
class Plic final : public bus::AddressRegion {
   public:
    static constexpr std::uint32_t kSourceCount = 31U;

    Plic();

    Plic(const Plic&) = delete;
    Plic& operator=(const Plic&) = delete;
    Plic(Plic&&) = delete;
    Plic& operator=(Plic&&) = delete;

    /** 设置一个设备 source 的电平；source 0 与范围外 ID 被安全忽略。 */
    void set_source_level(std::uint32_t source, bool asserted) noexcept;

    /** 将两个 context 的可裁决状态投影为唯一 CSR 中的 MEIP 与 SEIP。 */
    void synchronize(core::CsrFile& csrs) const noexcept;

    [[nodiscard]] bus::AccessResult read(std::uint64_t offset,
                                         bus::AccessWidth width,
                                         bus::AccessType type) override;
    [[nodiscard]] bus::AccessResult write(std::uint64_t offset,
                                          bus::AccessWidth width,
                                          std::uint64_t value,
                                          bus::AccessType type) override;
    /** PLIC 不支持原子 MMIO；调用必须获得结构化设备访问错误。 */
    [[nodiscard]] bus::AccessResult compare_exchange(std::uint64_t offset,
                                                     bus::AccessWidth width,
                                                     std::uint64_t expected,
                                                     std::uint64_t desired,
                                                     bus::AccessType type) override;

   private:
    /** 返回指定 context 的最高可裁决 source；0 表示当前无可交付中断。 */
    [[nodiscard]] std::uint32_t select(std::uint32_t context) const noexcept;
    /** 仅封装 select 的布尔语义，避免 M/S pending 投影复制裁决规则。 */
    [[nodiscard]] bool context_pending(std::uint32_t context) const noexcept;

    mutable std::mutex mutex_;
    std::array<std::uint32_t, kSourceCount + 1U> priority_{};
    std::array<bool, kSourceCount + 1U> level_{};
    // gateway 接受请求后必须锁存 pending，设备提前撤销电平不能取消尚未 claim 的请求。
    std::array<bool, kSourceCount + 1U> pending_{};
    // 0 表示未占用，1/2 分别表示由 M/S context 占用；complete 必须匹配该所有者。
    std::array<std::uint8_t, kSourceCount + 1U> claimed_by_{};
    std::array<std::uint32_t, 2U> enable_{};
    std::array<std::uint32_t, 2U> threshold_{};
};

}  // namespace rvemu::devices
