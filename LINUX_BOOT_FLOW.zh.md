# Linux 真实启动 Flow

本文只记录真实 UART 流和来宾命令输出，用于证明 Linux 确实从 OpenSBI 启动、识别设备、挂载 ext4 rootfs 并进入 Shell。构建产物、SHA-256 和回归完整的工具日志、镜像文件类型及测试状态见 [RESULT.zh.md](../docs/RESULT.zh.md)。

## 1. 运行命令

```sh
./build/riscv_vector_emulator \
  --bios artifacts/firmware/fw_jump.bin \
  --kernel artifacts/kernel/Image \
  --disk artifacts/disk/rootfs.ext4 \
  --net none
```

## 2. 证据文件

状态：OpenSBI 真实启动证据已生成；Linux、VirtIO-Blk、ext4 和 Shell 仍待完成。

当前保存到：

- `artifacts/logs/linux-boot-uart.log`

## 3. 启动 Flow

状态：部分完成。

| 阶段 | 需要证明的事实 | UART 证据 |
| --- | --- | --- |
| OpenSBI | 固件真实启动并识别 hart、平台、ISA、timebase 和 next stage | `OpenSBI v1.6`；`Platform Name : rvemu,riscv64-gcv-single-hart`；`Platform HART Count : 1`；`Platform Timer Device : aclint-mtimer @ 10000000Hz`；`Domain0 Next Address : 0x0000000080200000`；`Domain0 Next Arg1 : 0x0000000082200000`；`Domain0 Next Mode : S-mode` |
| Linux entry | Linux kernel 从 OpenSBI next stage 进入 | 待填 |
| FDT | Linux 识别 RAM、CPU ISA、Sv39、CLINT、PLIC 和 UART | 待填 |
| VirtIO-Blk | Linux 识别 VirtIO MMIO transport 和块设备 | 待填 |
| ext4 rootfs | Linux 以 `rootwait root=/dev/vda rootfstype=ext4` 挂载真实 ext4 根文件系统 | 待填 |
| Shell | 进入真实来宾 Shell | 待填 |

## 4. 来宾命令输出

状态：待执行完成。

```sh
ls /
pwd
cat /proc/cpuinfo
```

## 5. macOS 网络说明

macOS 本轮只验证 `--net none`。Linux TAP、`dhclient eth0`、DNS 和 `ping` 验收标记为 macOS 做不了，不使用宿主网络命令或伪日志替代。
