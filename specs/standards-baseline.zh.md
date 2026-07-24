# 标准版本基线

## 1. 目的

本文锁定项目实现时引用的正式标准，防止从不同版本拼接编码、CSR、页表或设备语义。实现中的标准注释和测试必须引用本文版本；升级标准必须先做影响分析并获得用户确认。

## 2. RISC-V 非特权 ISA

采用 RISC-V International 的 [Unprivileged ISA 20260120 官方发布版](https://docs.riscv.org/reference/isa/unpriv/unpriv-index.html)，并锁定以下已批准模块：

| 模块 | 版本 | 项目用途 |
| --- | --- | --- |
| RV64I | 2.1 | 64 位基础整数指令 |
| Zicsr | 2.0 | CSR 指令 |
| Zifencei | 2.0 | `FENCE.I` |
| M | 2.0 | 整数乘除 |
| A | 2.1 | LR/SC 与 AMO |
| F | 2.2 | 单精度浮点 |
| D | 2.2 | 双精度浮点 |
| C | 2.0 | 压缩指令 |
| V | 1.0 | 基础向量扩展 |

不在本表中的可选扩展默认不声明、不译码。若 Linux 或 OpenSBI 的冻结配置需要额外扩展，必须先更新本文和对应任务。

## 3. RISC-V 特权架构

采用 RISC-V International 的 [Privileged ISA 20260120 官方发布版](https://docs.riscv.org/reference/isa/priv/priv-index.html)，Machine ISA 与 Supervisor ISA 均以 1.13 的已批准语义为准。

首版实现 M/S/U、Sv39 和 PRD 所需中断/CSR，不实现 Hypervisor 扩展。A/D 位选择硬件更新路径；是否正式声明 `Svadu`、PMP 范围以及其他监督级扩展，必须在 CPU/MMU 模块开始前单独冻结，不能仅凭部分行为宣称支持。

## 4. RVV

向量语义采用官方 [`V` Standard Extension 1.0](https://docs.riscv.org/reference/isa/unpriv/v-st-ext)。硬件参数固定为 `VLEN=256`、`ELEN=64`、`vlenb=32`，包含 32 个向量寄存器。

## 5. VirtIO

采用 OASIS 的 [Virtual I/O Device (VIRTIO) Version 1.2](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)。项目使用 modern MMIO transport、Version 1 合规设备模型和 split virtqueue；不混用 legacy transport。可选 feature 只有在实现与测试完成后才公布。

## 6. 外部软件版本状态

OpenSBI、Linux LTS、交叉工具链和最小 rootfs 的精确版本仍属于外部产物决策，当前未冻结，也未下载。它们必须在引导模块开始前从官方来源选择、记录许可证与 SHA-256，并由用户确认。因此 `SDD-004` 当前不能标记完成。

## 7. 本模块适用条款

物理总线与 RAM/ROM 模块直接遵循：

- RV64 的 64 位物理访问值表示和小端内存字节序。
- 只允许 8、16、32、64 位显式访问宽度。
- 物理范围使用半开区间并对所有加法做溢出检查。
- 原子 compare-exchange 是总线正式事务，为后续 A 扩展与 PTE A/D 更新提供唯一基础能力。

本文仅冻结标准，不表示任何对应 ISA 或设备模块已经实现。
