# 快速启动指南

## 1. 适用范围

本文描述项目达到最终交付状态后的最短完整启动路径：构建真实模拟器、加载真实 OpenSBI/Linux/ext4 镜像、连接真实 TAP 接口，并在来宾 Shell 中验证公网连通性。这里没有 mock、替代设备或伪造启动输出。

README 描述最终成品，当前仓库是否已经具备某一步能力，应以 `specs/tasks.md` 的验收状态为准。如果当前提交尚未生成 `riscv_vector_emulator`，先继续完成任务清单，而不是创建同名脚本冒充可执行文件。

## 2. 前置条件

- 64 位 Linux 宿主机。
- CMake 3.20+ 和支持 C++17 的编译器。
- 已安装 RISC-V 交叉工具链、DTC、e2fsprogs、iproute2 等工具。
- 已按本项目机器布局构建 OpenSBI、Linux 和 ext4 rootfs。
- 用户有权创建或使用 TAP 接口。

第三方组件的作用、官方来源与安装命令见 `docs/third-party.md`。

## 3. 获取源码

```bash
git clone https://github.com/billzi2016/homemade-risc-v-64-vector-linux-emulator.git
cd homemade-risc-v-64-vector-linux-emulator
```

后续命令都从项目根目录执行。文档不会要求进入或写入项目目录之外的任意用户路径。

## 4. 构建与测试模拟器

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

只有配置、编译和全部必需测试均成功后，才进入系统启动步骤。最终可执行文件应为：

```text
build/riscv_vector_emulator
```

如果没有安装 Ninja，删除 `-G Ninja`，让 CMake 使用宿主机默认生成器即可；这不会改变模拟器功能。

## 5. 放置启动资源

准备以下目录和文件：

```text
artifacts/
├── firmware/
│   └── opensbi.bin
├── kernel/
│   └── vmlinux.bin
├── disk/
│   └── rootfs.ext4
└── logs/
```

三项资源必须与本项目虚拟硬件匹配：

- `opensbi.bin` 的入口地址和下一阶段跳转协议与 Boot ROM 一致。
- `vmlinux.bin` 面向 RV64，启用串口、PLIC、CLINT、VirtIO MMIO、块设备、网卡和 ext4。
- `rootfs.ext4` 包含 `init`、Shell、设备节点管理、DHCP 客户端、DNS 配置和 `ping`。

确认文件存在且非空：

```bash
test -s artifacts/firmware/opensbi.bin
test -s artifacts/kernel/vmlinux.bin
test -s artifacts/disk/rootfs.ext4
```

对 ext4 镜像执行只读检查：

```bash
e2fsck -fn artifacts/disk/rootfs.ext4
```

不要把这些文件执行 `git add -f`；它们属于本地外部产物，并已被 `.gitignore` 排除。

## 6. 准备 TAP 网络

模拟器需要一个已存在且处于 UP 状态的 TAP 接口，例如 `tap0`。首先检查 Linux TUN/TAP 能力：

```bash
test -c /dev/net/tun
ip tuntap help
```

创建 TAP、将它加入网桥或配置 NAT 会改变宿主机网络和防火墙。应由管理员依据 `specs/14-host-network-setup.md`、实际上行接口及局域网策略执行，不能盲目复制固定接口名或地址段。

准备完成后，只读确认接口状态：

```bash
ip -details link show tap0
```

输出中应包含 TAP 信息且接口状态为 UP。模拟器进程还必须拥有打开 `/dev/net/tun` 和绑定该接口的权限。

## 7. 启动完整虚拟机

```bash
./build/riscv_vector_emulator \
  --bios artifacts/firmware/opensbi.bin \
  --kernel artifacts/kernel/vmlinux.bin \
  --disk artifacts/disk/rootfs.ext4 \
  --net tap0
```

启动顺序应依次出现：

1. OpenSBI Banner 和平台信息。
2. Linux 早期启动日志、内存与中断控制器初始化。
3. UART、VirtIO-Blk 和 VirtIO-Net 驱动探测信息。
4. ext4 根文件系统挂载成功。
5. `init` 启动并显示可输入命令的 Shell。

宿主终端会进入 Raw 模式。键盘输入直接送到来宾 UART；`Ctrl+C` 应由来宾处理，而不是粗暴终止模拟器。模拟器正常退出或发生错误时必须恢复宿主终端属性。

## 8. 来宾网络验收

在来宾 Shell 中执行 PRD 要求的 DHCP 和公网测试：

```bash
dhclient eth0
ip address show dev eth0
ip route show
ping -c 4 google.com
```

通过条件：

- `eth0` 获得预期网段中的独立 IPv4 地址。
- 默认路由指向正确网关。
- `google.com` 能够通过来宾 DNS 配置解析。
- 收到 4 个 ICMP Echo Reply，统计结果为 0% 丢包。

公网可能对 ICMP 限流或阻断，因此出现丢包时还必须分别排查 TAP 收发、ARP、DHCP、DNS、路由、防火墙和上游网络，不能仅凭一次 `ping` 失败认定 VirtIO-Net 实现错误。

## 9. 保存验收信息

需要留存证据时，从宿主机重新启动并使用项目内的日志目录：

```bash
./build/riscv_vector_emulator \
  --bios artifacts/firmware/opensbi.bin \
  --kernel artifacts/kernel/vmlinux.bin \
  --disk artifacts/disk/rootfs.ext4 \
  --net tap0 \
  2>artifacts/logs/emulator-stderr.log
```

UART 使用交互终端时，不应简单用普通管道替代 TTY；否则 Raw 模式、控制字符和输入时序的验收条件会改变。日志中不得包含主机密钥、token、密码或其他敏感数据。

## 10. 常见失败定位

| 现象 | 优先检查 |
| --- | --- |
| 找不到可执行文件 | 当前任务状态、CMake 配置与链接目标是否完整 |
| OpenSBI 无输出 | Boot ROM 跳板、固件装载地址、PC、UART 地址和 FDT 指针 |
| OpenSBI 后卡住 | CSR、`medeleg`/`mideleg`、`MRET`、定时器和 S-mode 入口 |
| Linux 页错误循环 | `satp`、Sv39 canonical VA、PTE 权限、超级页、A/D 位与 TLB |
| 找不到根文件系统 | VirtIO-MMIO 描述、队列布局、磁盘镜像、内核 ext4 配置 |
| 块设备运行后死锁 | Descriptor Chain、Available/Used Ring 索引回绕和内存发布顺序 |
| `eth0` 不存在 | FDT VirtIO 节点、VirtIO-Net feature negotiation 和 PLIC 中断 |
| DHCP 超时 | TAP 状态、网桥/NAT、队列接收缓冲和外部中断 |
| 能 ping IP 不能解析域名 | rootfs 中的 DNS 配置和 DHCP 下发内容 |
| 退出后终端异常 | termios 保存/恢复和所有错误退出路径 |

调试时必须修复正式执行链，不得增加只为绕过启动节点的第二套地址翻译、设备实现或伪造返回值。

## 11. 清理说明

删除 `build/` 可以清理本机构建结果；`artifacts/` 保存的是可重新下载或生成的本地产物。清理 TAP、网桥、路由或 nftables 规则会改变宿主网络状态，必须使用创建它们时对应的受控流程，不能用范围不明的批量删除命令。
