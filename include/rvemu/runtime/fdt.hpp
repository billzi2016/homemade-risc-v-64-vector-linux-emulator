// 文件职责：声明与当前机器模型一致的 Flattened Device Tree 生成入口。
// 边界：本模块只生成 DTB 字节，不调用 dtc、不写文件、不注册设备。
// 主要依赖：统一地址图常量；调用方负责把 DTB 放入来宾 RAM。
// 关键不变量：FDT 中 RAM、CLINT、PLIC、UART 和 VirtIO 地址必须来自同一地址图事实来源。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rvemu::runtime {

struct FdtConfig final {
    std::uint64_t ram_base{0x8000'0000ULL};
    std::uint64_t ram_size{0x4000'0000ULL};
    std::uint64_t fdt_address{0xBFE0'0000ULL};
    std::uint64_t fdt_reserved_size{0x0002'0000ULL};
    std::uint32_t timebase_frequency{10'000'000U};
    std::string bootargs{"console=ttyS0,115200 earlycon root=/dev/vda rw rootfstype=ext4"};
    std::string isa{"rv64imafdcv"};
};

struct FdtBuildResult final {
    std::vector<std::uint8_t> blob{};
    std::string error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

/** 生成正式 DTB 二进制；失败仅表示配置范围本身无效，不产生外部副作用。 */
[[nodiscard]] FdtBuildResult build_machine_fdt(const FdtConfig& config);

}  // namespace rvemu::runtime
