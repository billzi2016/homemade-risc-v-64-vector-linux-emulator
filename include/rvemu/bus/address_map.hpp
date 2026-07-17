// 文件职责：集中定义 PRD 冻结的物理地址图，作为总线注册、FDT 和边界测试的单一事实来源。
// 边界：这里只保存地址与大小常量，不实例化设备或分配物理内存。

#pragma once

#include <array>
#include <cstdint>

namespace rvemu::bus::address_map {

struct Entry final {
    const char* name;
    std::uint64_t base;
    std::uint64_t size;
};

inline constexpr Entry kBootRom{"boot-rom", 0x0000'1000ULL, 0x0000'B000ULL};
inline constexpr Entry kClint{"clint", 0x0200'0000ULL, 0x0000'C000ULL};
inline constexpr Entry kPlic{"plic", 0x0C00'0000ULL, 0x0400'0000ULL};
inline constexpr Entry kUart{"uart16550", 0x1000'0000ULL, 0x0000'0100ULL};
inline constexpr Entry kVirtioBlock{"virtio-block", 0x1000'1000ULL, 0x0000'1000ULL};
inline constexpr Entry kVirtioNetwork{"virtio-network", 0x1000'2000ULL, 0x0000'1000ULL};
inline constexpr Entry kDefaultRam{"ram", 0x8000'0000ULL, 0x4000'0000ULL};

inline constexpr std::array<Entry, 7U> kEntries{
    kBootRom,
    kClint,
    kPlic,
    kUart,
    kVirtioBlock,
    kVirtioNetwork,
    kDefaultRam,
};

[[nodiscard]] constexpr std::uint64_t end_exclusive(const Entry& entry) noexcept {
    return entry.base + entry.size;
}

static_assert(end_exclusive(kBootRom) == 0x0000'C000ULL);
static_assert(end_exclusive(kClint) == 0x0200'C000ULL);
static_assert(end_exclusive(kPlic) == 0x1000'0000ULL);
static_assert(end_exclusive(kUart) == 0x1000'0100ULL);
static_assert(end_exclusive(kVirtioBlock) == 0x1000'2000ULL);
static_assert(end_exclusive(kVirtioNetwork) == 0x1000'3000ULL);
static_assert(end_exclusive(kDefaultRam) == 0xC000'0000ULL);

}  // namespace rvemu::bus::address_map
