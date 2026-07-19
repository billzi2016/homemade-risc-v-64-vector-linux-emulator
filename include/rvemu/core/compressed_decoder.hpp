// 文件职责：声明 RV64C 2.0 的唯一 16 位到 32 位等价指令解压接口。
// 边界：解压器只判断编码合法性和重组字段，不读取寄存器、不访问总线，也不提交 PC。

#pragma once

#include <cstdint>
#include <optional>

namespace rvemu::core {

// 合法指令和 HINT 返回一条现有 RV64I/F/D 编码；reserved/custom/永久非法编码返回空。
[[nodiscard]] std::optional<std::uint32_t> decompress_rv64c(std::uint16_t bits) noexcept;

}  // namespace rvemu::core
