# 产品总览

## 1. 产品定位

`homemade-risc-v-64-vector-linux-emulator` 是一个纯命令行、单机全系统模拟器。它在宿主机软件中实现一台支持 RV64GCV 的精简虚拟硬件平台，能够启动 OpenSBI 和 Linux，挂载 ext4 根文件系统并通过 UART 提供交互式 Shell；Linux 宿主还可经 VirtIO-Net 与 TAP 接入真实网络。

## 2. 核心目标

- **PRD-REQ-001**：提供 RV64GCV CPU、M/S/U 特权态、关键 CSR、异常和中断语义。
- **PRD-REQ-002**：提供 VLEN=256 的 RVV 1.0 向量执行能力。
- **PRD-REQ-003**：提供 Sv39、TLB 和 `SFENCE.VMA`，足以正确运行目标 Linux。
- **PRD-REQ-004**：提供 Boot ROM、RAM、CLINT、PLIC、UART、VirtIO-Blk 和 VirtIO-Net 机器模型。
- **PRD-REQ-005**：通过单条 CLI 命令装载 BIOS、内核和磁盘，并明确选择关闭网络或绑定 Linux TAP。
- **PRD-REQ-006**：启动真实 OpenSBI 和 Linux，进入可交互 Shell。
- **PRD-REQ-007**：Linux 网络档位中，来宾经真实 DHCP/DNS/ICMP 链路访问公网，最终 `ping` 丢包率为 0%。
- **PRD-REQ-008**：macOS 无网络档位中，来宾必须真实进入 Shell，并成功执行 `ls /`、`pwd` 和 `cat /proc/cpuinfo`。

## 3. 质量目标

- 语义正确性优先于执行速度。
- 核心规则具备单一实现，遵循 SOLID 和 DRY。
- 所有代码具有完整且准确的中文维护注释。
- 错误可诊断，来宾异常与宿主故障明确区分。
- 构建和外部产物获取可复现，二进制大文件不入库。

## 4. 明确范围

### 4.1 范围内

- 单 Hart RV64 全系统模拟；机器模型为后续多 Hart 扩展保留边界，但首版不承诺 SMP。
- RV64I、M、A、F、D、C 和满足 Linux/验收要求的 RVV 1.0 指令范围。
- M/S/U 特权态及 Linux 使用的关键 CSR 和委托机制。
- Sv39 虚拟内存与至少 64 项 TLB。
- 规定地址上的精简 MMIO 平台。
- VirtIO MMIO split virtqueue、块设备与网卡。
- Linux 宿主上的 `/dev/net/tun` TAP 后端。
- macOS 宿主上的无网络完整启动档位；它复用同一 CPU、MMU、块设备、UART 和运行循环。
- POSIX 终端 Raw 模式和 CLI 生命周期。

### 4.2 范围外

- GUI、网页控制台或桌面管理器。
- JIT、动态二进制翻译或性能优先优化；除非后续规格单独批准。
- PCI/PCIe、USB、GPU、音频和完整真实主板外设。
- 硬件虚拟化加速或直接调用宿主 RISC-V 执行。
- 通过用户态网络模拟替代 PRD 指定的 TAP/网桥最终链路。
- 把外部固件、内核和 rootfs 二进制提交到仓库。

## 5. 目标运行命令

Linux 网络档位：

```text
./riscv_vector_emulator \
  --bios opensbi.bin \
  --kernel vmlinux.bin \
  --disk rootfs.ext4 \
  --net tap0
```

macOS 无网络档位把最后一项改为 `--net none`；省略 `--net` 与显式 `none` 等价。

实际装载地址、默认 RAM 大小、FDT 地址和可选诊断参数由后续规格固定。缺少必需参数或文件不可访问时，程序必须在改变终端或网络状态前给出清晰错误并退出。

## 6. 用户可观察生命周期

1. CLI 校验参数和宿主资源。
2. 分配并初始化 RAM、ROM 与设备。
3. 装载 OpenSBI、Linux 和 FDT，绑定磁盘，并按配置选择是否绑定 TAP。
4. 安全切换终端 Raw 模式。
5. 执行 CPU 主循环并处理设备事件。
6. UART 输出 OpenSBI 与 Linux 日志。
7. 用户进入 Shell；macOS 档位执行基础命令，Linux 网络档位继续配置 `eth0` 并访问公网。
8. 正常或异常退出时恢复终端并释放全部资源。

## 7. 最终验收场景

两个宿主档位都必须在记录的环境中使用真实产物完成前三项：

1. UART 出现真实 OpenSBI Banner。
2. Linux 内核完成启动且无 Kernel Panic。
3. 根文件系统成功挂载并出现可输入 Shell。
4. macOS 无网络档位执行 `ls /`、`pwd` 和 `cat /proc/cpuinfo` 成功。
5. Linux 网络档位执行 `dhclient eth0`、域名解析和 `ping -c 4 google.com`，收到 4 个响应且丢包率为 0%。

任何人为打印、预录日志、宿主机代执行或 Mock 网络结果均无效。

## 8. 待用户冻结的版本决策

在开始实现前必须在 `SDD-004` 中确认：

- RISC-V 非特权、特权和 RVV 规范的精确版本。
- VirtIO 规范版本和 MMIO transport 版本。
- OpenSBI、Linux LTS、交叉工具链和 rootfs 版本。
- 首版是否严格只支持单 Hart。
- 宿主档位已冻结为 macOS 无网络启动与 Linux TAP 完整网络两类；二者共享来宾硬件实现。
