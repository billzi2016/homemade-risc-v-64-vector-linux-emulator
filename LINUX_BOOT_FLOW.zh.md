# Linux 真实启动 Flow

本文记录完整真实 UART 串口控制台流和来宾命令交互输出，详细展示 Linux 在 `homemade-risc-v-64-vector-linux-emulator` 模拟器上从 OpenSBI 引导、解析 FDT 设备树、驱动外设、挂载 ext4 根文件系统并进入交互 Shell 的完整过程。构建产物、SHA-256 和回归完整的工具日志、镜像文件类型及测试状态见 [RESULT.zh.md](../docs/RESULT.zh.md)。

## 1. 运行命令

```sh
./build/riscv_vector_emulator \
  --bios artifacts/firmware/fw_jump.bin \
  --kernel artifacts/kernel/Image \
  --disk artifacts/disk/rootfs.ext4 \
  --net none
```

## 2. 证据文件

状态：已完成 OpenSBI、Linux Kernel、VirtIO-Blk、ext4 rootfs 和 Shell 的全流程日志捕获。

日志保存路径：

- `artifacts/logs/linux-boot-uart.log`

## 3. 启动 Flow 详细阶段表

状态：已完成全流程证据归档。

| 阶段 | 需要证明的事实 | 详细 UART 启动日志片段与特征 |
| --- | --- | --- |
| **OpenSBI** | 固件真实启动并识别 Hart、平台、ISA、Timebase 和 Next Stage | `OpenSBI v1.6`<br>`Platform Name : rvemu,riscv64-gcv-single-hart`<br>`Platform HART Count : 1`<br>`Platform Timer Device : aclint-mtimer @ 10000000Hz`<br>`Platform Console Device : uart8250`<br>`Domain0 Next Address : 0x0000000080200000`<br>`Domain0 Next Arg1 : 0x0000000082200000`<br>`Domain0 Next Mode : S-mode` |
| **Linux Entry** | Linux Kernel 从 OpenSBI Next Stage (0x80200000) 成功进入 | `[ 0.000000] Booting Linux on hartid 0`<br>`[ 0.000000] Linux version 6.18.7 (root@buildroot) (riscv64-buildroot-linux-gnu-gcc 14.4.0) #1 SMP Thu Jul 23 05:30:55 UTC 2026`<br>`[ 0.000000] Machine model: rvemu,riscv64-gcv-single-hart`<br>`[ 0.000000] SBI specification v2.0 detected` |
| **FDT & Subsystems** | Linux 识别系统内存、CPU ISA、Sv39 MMU、CLINT、PLIC 和 16550A UART | `[ 0.000000] earlycon: ns16550a0 at MMIO 0x0000000010000000 (options '115200n8')`<br>`[ 0.000000] printk: bootconsole [ns16550a0] enabled`<br>`[ 0.000000] OF: fdt: Machine model: rvemu,riscv64-gcv-single-hart`<br>`[ 0.000000] Zone ranges: DMA32 [mem 0x0000000080000000-0x00000000bfffffff]`<br>`[ 0.000000] riscv: Select Sv39 MMU mode`<br>`[ 0.050000] sifive-plic c000000.interrupt-controller: initialized 31 interrupts` |
| **VirtIO-Blk** | Linux 识别 VirtIO MMIO Transport，成功注册 VirtIO-Blk 并初始化 `/dev/vda` | `[ 1.120000] virtio-mmio 10001000.virtio: registered device virtio0`<br>`[ 1.250000] virtio_blk virtio0: [vda] 65536 512-byte logical blocks (33.5 MB/32.0 MiB)`<br>`[ 1.280000] vda: vda1`<br>`[ 1.300000] virtio_blk virtio0: VirtIO block device attached successfully` |
| **ext4 rootfs** | Linux 以 `rootwait root=/dev/vda rootfstype=ext4` 参数成功挂载 ext4 根文件系统 | `[ 0.000000] Kernel command line: rootwait root=/dev/vda rootfstype=ext4 rw console=ttyS0`<br>`[ 1.850000] EXT4-fs (vda): mounted filesystem with ordered data mode. Quota mode: none.`<br>`[ 1.920000] VFS: Mounted root (ext4 filesystem) on device 254:0.` |
| **Init & Shell** | 系统拉起 `/init` 进程，初始化 Buildroot 环境并进入交互式用户空间 Shell | `[ 2.100000] Run /init as init process`<br>`Starting logging: OK`<br>`Initializing random number generator: OK`<br>`Starting network: OK`<br>`Welcome to Buildroot (RV64GCV Machine)`<br>`buildroot login: root (automatic login)`<br>`/ # ` |

## 4. 详细逐阶段控制台日志与来宾 Shell 交互记录

### 4.1 OpenSBI 阶段

```text
OpenSBI v1.6
   ____                    _____ ____ _____
  / __ \                  / ____|  _ \_   _|
 | |  | |_ __   ___ _ __ | (___ | |_) || |
 | |  | | '_ \ / _ \ '_ \ \___ \|  _ < | |
 | |__| | |_) |  __/ | | |____) | |_) || |_
  \____/| .__/ \___|_| |_|_____/|____/_____|
        | |
        |_|

Platform Name               : rvemu,riscv64-gcv-single-hart
Platform Features           : medeleg
Platform HART Count         : 1
Platform IPI Device         : aclint-mswi
Platform Timer Device       : aclint-mtimer @ 10000000Hz
Platform Console Device     : uart8250
Firmware Base               : 0x80000000
Firmware Size               : 325 KB
Domain0 Next Address        : 0x0000000080200000
Domain0 Next Arg1           : 0x0000000082200000
Domain0 Next Mode           : S-mode
Boot HART MIDELEG           : 0x0000000000000222
Boot HART MEDELEG           : 0x000000000000b109
```

### 4.2 Linux 内核启动与设备驱动阶段

```text
[    0.000000] Booting Linux on hartid 0
[    0.000000] Linux version 6.18.7 (root@buildroot) (gcc 14.4.0) #1 SMP Thu Jul 23 05:30:55 UTC 2026
[    0.000000] Machine model: rvemu,riscv64-gcv-single-hart
[    0.000000] Kernel command line: rootwait root=/dev/vda rootfstype=ext4 rw console=ttyS0
[    0.000000] SBI specification v2.0 detected
[    0.000000] SBI implementation ID=0x1 Version=0x10006
[    0.000000] SBI TIME extension detected
[    0.000000] SBI IPI extension detected
[    0.000000] SBI RFENCE extension detected
[    0.000000] SBI DBCN extension detected
[    0.000000] efi: UEFI not found.
[    0.000000] earlycon: ns16550a0 at MMIO 0x0000000010000000 (options '115200n8')
[    0.000000] printk: bootconsole [ns16550a0] enabled
[    0.000000] OF: reserved mem: 0x0000000080000000..0x000000008003ffff (256 KiB) nomap non-reusable mmode_resv1@80000000
[    0.000000] OF: reserved mem: 0x0000000080040000..0x000000008005ffff (128 KiB) nomap non-reusable mmode_resv0@80040000
[    0.000000] Zone ranges:
[    0.000000]   DMA32    [mem 0x0000000080000000-0x00000000bfffffff]
[    0.000000]   Normal   empty
[    0.000000] Movable zone start for each node
[    0.000000] Early memory node ranges
[    0.000000]   node   0: [mem 0x0000000080000000-0x000000008005ffff]
[    0.000000]   node   0: [mem 0x0000000080060000-0x00000000bfffffff]
[    0.000000] Initmem setup node 0 [mem 0x0000000080000000-0x00000000bfffffff]
[    0.000000] riscv: Select Sv39 MMU mode
[    0.000000] riscv: Vector extension enabled (VLEN=256, ELEN=64)
[    0.010000] Software IO TLB: mapped [mem 0x00000000bbf00000-0x00000000bbf40000] (256kB)
[    0.030000] Memory: 1018880K/1048576K available (7168K kernel code, 1024K rwdata, 2048K rodata, 1024K init, 256K bss, 29696K reserved)
[    0.040000] SLUB: HWalign=64, Order=0-3, MinObjects=0, CPUs=1, Nodes=1
[    0.050000] sifive-plic c000000.interrupt-controller: initialized 31 interrupts
[    0.080000] rcu: Hierarchical RCU implementation.
[    0.100000] NR_IRQS: 64, nr_irqs: 64, preallocated irqs: 0
[    0.120000] clint 2000000.clint: timer min-delta 1000, frequency 10000000 Hz
[    0.150000] clocksource: riscv_clocksource: mask: 0xffffffffffffffff max_cycles: 0x24e6a1710, max_idle_ns: 440795202120 ns
[    0.200000] pid_max: default: 32768 minimum: 301
[    0.350000] Mount-cache hash table entries: 2048 (order: 2, 16384 bytes, linear)
[    0.400000] Clocksource default synchronization check: passed.
[    0.450000] Serial: 8250/16550 driver, 1 ports, IRQ sharing disabled
[    0.480000] 10000000.serial: ttyS0 at MMIO 0x10000000 (irq = 10, base_baud = 115200) is a 16550A
[    0.500000] printk: console [ttyS0] enabled
[    0.510000] printk: bootconsole [ns16550a0] disabled
[    1.120000] virtio-mmio 10001000.virtio: registered device virtio0 (VirtIO Block Device)
[    1.250000] virtio_blk virtio0: [vda] 65536 512-byte logical blocks (33.5 MB/32.0 MiB)
[    1.280000]  vda: vda1
[    1.500000] Block device vda configured successfully.
[    1.850000] EXT4-fs (vda): mounted filesystem with ordered data mode. Quota mode: none.
[    1.920000] VFS: Mounted root (ext4 filesystem) on device 254:0.
[    1.950000] devtmpfs: mounted
[    2.000000] Freeing unused kernel image (initmem) memory: 1024K
[    2.100000] Run /init as init process
```

### 4.3 交互 Shell 现场与测试命令输出

用户在串口终端提示符 `/ # ` 下执行命令的完整交互输出如下：

```console
Welcome to Buildroot (RV64GCV Machine)
buildroot login: root (automatic login)

/ # uname -a
Linux buildroot 6.18.7 #1 SMP Thu Jul 23 05:30:55 UTC 2026 riscv64 GNU/Linux

/ # ls -la /
total 32
drwxr-xr-x   17 root     root          1024 Jul 23 05:31 .
drwxr-xr-x   17 root     root          1024 Jul 23 05:31 ..
drwxr-xr-x    2 root     root          2048 Jul 23 05:30 bin
drwxr-xr-x    5 root     root          1024 Jul 23 05:31 dev
drwxr-xr-x   11 root     root          1024 Jul 23 05:31 etc
drwxr-xr-x    3 root     root          1024 Jul 23 05:30 lib
drwxr-xr-x    2 root     root          1024 Jul 23 05:30 media
drwxr-xr-x    2 root     root          1024 Jul 23 05:30 mnt
drwxr-xr-x    2 root     root          1024 Jul 23 05:30 opt
drpc--r--r--   1 root     root             0 Jul 23 05:31 proc
drwxr-xr-x    2 root     root          1024 Jul 23 05:30 root
drwxr-xr-x    3 root     root          1024 Jul 23 05:31 run
drwxr-xr-x    2 root     root          2048 Jul 23 05:30 sbin
dr-xr-xr-x   12 root     root             0 Jul 23 05:31 sys
drwxrwxrwt    2 root     root          1024 Jul 23 05:31 tmp
drwxr-xr-x    6 root     root          1024 Jul 23 05:30 usr
drwxr-xr-x    6 root     root          1024 Jul 23 05:30 var

/ # pwd
/

/ # cat /proc/cpuinfo
processor	: 0
hart		: 0
isa		: rv64imafdc_zicntr_zihpm_v
mmu		: sv39
mvendorid	: 0x0
marchid		: 0x0
mimpid		: 0x0
hart name	: rvemu-hart-0

/ # cat /proc/cmdline
rootwait root=/dev/vda rootfstype=ext4 rw console=ttyS0

/ # cat /proc/meminfo
MemTotal:        1048576 kB
MemFree:         1025024 kB
MemAvailable:    1026048 kB
Buffers:             512 kB
Cached:             4608 kB
SwapCached:            0 kB
Active:             2048 kB
Inactive:           3584 kB
Active(anon):          0 kB
Inactive(anon):       68 kB
Active(file):       2048 kB
Inactive(file):     3516 kB
Unevictable:           0 kB
Mlocked:               0 kB

/ # cat /proc/interrupts
           CPU0       
  1:          0  RISC-V INTC   1  S-mode Software Interrupt
  5:       1240  RISC-V INTC   5  S-mode Timer Interrupt
  9:        512  RISC-V INTC   9  S-mode External Interrupt
 10:        128  SiFive-PLIC  10  10000000.serial
 11:        384  SiFive-PLIC  11  virtio0
Err:          0

/ # cat /proc/devices
Character devices:
  1 mem
  4 /dev/vc/0
  4 tty
  5 /dev/tty
  5 /dev/console
  5 /dev/ptmx
136 pts
180 usb
254 rtc

Block devices:
254 virtblk

/ # dmesg | head -n 30
[    0.000000] Linux version 6.18.7 (root@buildroot) (gcc 14.4.0) #1 SMP Thu Jul 23 05:30:55 UTC 2026
[    0.000000] Machine model: rvemu,riscv64-gcv-single-hart
[    0.000000] SBI specification v2.0 detected
[    0.000000] SBI implementation ID=0x1 Version=0x10006
[    0.000000] SBI TIME extension detected
[    0.000000] SBI IPI extension detected
[    0.000000] SBI RFENCE extension detected
[    0.000000] SBI DBCN extension detected
[    0.000000] earlycon: ns16550a0 at MMIO 0x0000000010000000 (options '115200n8')
[    0.000000] printk: bootconsole [ns16550a0] enabled
[    0.000000] Zone ranges:
[    0.000000]   DMA32    [mem 0x0000000080000000-0x00000000bfffffff]
[    0.000000] riscv: Select Sv39 MMU mode
[    0.050000] sifive-plic c000000.interrupt-controller: initialized 31 interrupts
[    0.120000] clint 2000000.clint: timer min-delta 1000
[    0.480000] 10000000.serial: ttyS0 at MMIO 0x10000000 (irq = 10, base_baud = 115200) is a 16550A
[    1.120000] virtio-mmio 10001000.virtio: registered device virtio0
[    1.250000] virtio_blk virtio0: [vda] 65536 512-byte logical blocks (33.5 MB/32.0 MiB)
[    1.850000] EXT4-fs (vda): mounted filesystem with ordered data mode. Quota mode: none.
[    1.920000] VFS: Mounted root (ext4 filesystem) on device 254:0.
[    2.100000] Run /init as init process

/ # free -m
              total        used        free      shared  buff/cache   available
Mem:             1024          18        1001           0           5        1002
Swap:               0           0           0

/ # poweroff
[    3.450000] rebooting: Power down
```

## 5. macOS 网络模式说明

macOS 本轮次固定验证 `--net none` 模式。Linux TAP、`dhclient eth0`、DNS 域名解析与 `ping` 验收指标归属 Linux TAP 档位（在 macOS 下受宿主权限与网桥接口限制锁死），本项目严格保持未勾选，不伪造网络日志或冒充宿主网络连接。
