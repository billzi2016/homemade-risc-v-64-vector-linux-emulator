# OpenSBI、Linux 与 FDT 引导规格

## 1. 外部产物

- BIOS：真实 OpenSBI 可执行固件镜像。
- Kernel：与机器 ISA、设备树和启动协议匹配的真实 Linux 内核镜像。
- Disk：包含可启动用户空间和网络工具的真实 ext4 rootfs。

示例相对路径：`artifacts/firmware/opensbi.bin`、`artifacts/kernel/Image`、`artifacts/disk/rootfs.ext4`。这些文件均不得提交 Git。

## 2. 镜像格式和装载

- **BOOT-REQ-001**：CLI 必须明确支持的 BIOS 和 kernel 文件格式；不能仅凭文件名猜测 ELF 或 raw。
- **BOOT-REQ-002**：装载前检查文件大小、目标范围、重叠、RAM 容量和地址溢出。
- **BOOT-REQ-003**：raw 镜像按冻结的固定地址装载；ELF 如被支持，必须验证 ELF64、小端、RISC-V、程序段权限和范围。
- **BOOT-REQ-004**：磁盘不整体载入 RAM，而由 VirtIO-Blk 后端绑定。

BIOS、kernel、FDT、栈/保留区不得互相覆盖。具体地址在标准基线冻结后写入同一机器配置来源。

## 3. 复位与 OpenSBI 入口

- 首版 Hart ID 为 0。
- 复位进入 M 模式，PC 指向 Boot ROM 跳板或 OpenSBI 规定入口。
- 按 RISC-V boot ABI 设置 `a0=hartid`、`a1=FDT` 物理地址。
- 其余寄存器和 CSR 采用已记录复位值。
- 若使用 ROM 跳板，其机器码必须由受控构建或清晰编码生成，不得用打印 Banner 的伪固件替代。

## 4. FDT 内容

- **BOOT-REQ-005**：生成合法 Flattened Device Tree，并与实际机器模型完全一致。
- 根节点包含兼容字符串、address/size cells 和模型信息。
- `/cpus` 描述单 Hart、RV64 ISA 字符串、MMU 类型 `riscv,sv39` 和 `timebase-frequency`。
- memory 节点描述 `0x80000000` 和真实 RAM 大小。
- chosen 节点设置正确 stdout-path 和内核命令行。
- soc 节点描述 CLINT、PLIC、UART、两个 VirtIO MMIO 窗口和中断连接。
- reserved-memory 或 memreserve 保护 FDT/固件需要保留的区域。

ISA 字符串不得声明尚未完整实现的扩展。PLIC context、source ID 和设备中断必须与实现共享配置事实。

## 5. Linux 内核配置要求

构建配置至少包含：

- RV64、MMU、Sv39、SMP 关闭或与单 Hart 相容。
- RISC-V SBI、PLIC、CLINT/定时器。
- 8250/16550 串口和设备树控制台。
- VirtIO MMIO、VirtIO Block、VirtIO Net。
- ext4、devtmpfs、proc、sysfs 和必要网络协议。
- DHCP 客户端所需用户空间与 DNS 配置。
- C/F/D/V 扩展配置必须与实际实现和工具链匹配。

精确 `.config` 作为可复现文本可提交；构建生成的内核二进制不得提交。

## 6. 内核命令行

至少明确：控制台设备、root 设备、rootfstype、读写策略和早期日志选项。设备枚举必须稳定，目标根设备应对应 VirtIO-Blk。调试参数不得掩盖设备失败或跳过正常驱动。

## 7. rootfs 要求

- ext4 文件系统完整且与块设备容量一致。
- 提供 init、Shell、挂载脚本、设备节点管理和网络配置工具。
- 包含 `dhclient` 或需求最终确认的等价命令；由于 PRD 明确命令，默认必须提供 `dhclient`。
- 包含 `ping`、DNS resolver 配置和必要动态库；若使用静态 BusyBox，需确认命令行为满足验收。
- 账户和登录策略适合本地教育环境，不包含真实密钥或凭据。

## 8. 验收条件

- FDT 可被 `dtc` 或等价正式工具反编译验证，无地址/中断冲突。
- OpenSBI 识别 hart、平台、时钟和下一阶段入口。
- Linux 识别 RAM、PLIC、UART、VirtIO-Blk 和 VirtIO-Net。
- rootfs 正常挂载并进入 Shell，不使用 initramfs 假冒 ext4 磁盘验收。
- 所有产物版本、配置、来源和 SHA-256 记录可复现。
