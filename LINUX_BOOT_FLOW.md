# Real Linux Boot Flow

This document records only real UART streams and guest command outputs, proving Linux boots from OpenSBI, detects devices, mounts ext4 rootfs, and enters Shell. Build artifacts, SHA-256 hashes, and regression test results are documented in [RESULT.md](../docs/RESULT.md).

## 1. Execution Command

```sh
./build/riscv_vector_emulator \
  --bios artifacts/firmware/fw_jump.bin \
  --kernel artifacts/kernel/Image \
  --disk artifacts/disk/rootfs.ext4 \
  --net none
```

## 2. Evidence Files

Status: OpenSBI real boot evidence generated; Linux, VirtIO-Blk, ext4, and Shell evidence pending.

Currently saved to:

- `artifacts/logs/linux-boot-uart.log`

## 3. Boot Flow

Status: Partially Complete.

| Stage | Fact to Prove | UART Evidence |
| --- | --- | --- |
| OpenSBI | Firmware boots and identifies hart, platform, ISA, timebase, and next stage | `OpenSBI v1.6`; `Platform Name : rvemu,riscv64-gcv-single-hart`; `Platform HART Count : 1`; `Platform Timer Device : aclint-mtimer @ 10000000Hz`; `Domain0 Next Address : 0x0000000080200000`; `Domain0 Next Arg1 : 0x0000000082200000`; `Domain0 Next Mode : S-mode` |
| Linux entry | Linux kernel enters from OpenSBI next stage | Pending |
| FDT | Linux detects RAM, CPU ISA, Sv39, CLINT, PLIC, and UART | Pending |
| VirtIO-Blk | Linux detects VirtIO MMIO transport and block device | Pending |
| ext4 rootfs | Linux mounts real ext4 root filesystem via `rootwait root=/dev/vda rootfstype=ext4` | Pending |
| Shell | Enters real guest interactive shell | Pending |

## 4. Guest Command Output

Status: Pending execution.

```sh
ls /
pwd
cat /proc/cpuinfo
```

## 5. macOS Networking Note

macOS only validates `--net none` in this phase. Linux TAP, `dhclient eth0`, DNS, and `ping` acceptance criteria are marked as unsupported on macOS and will not be faked using host network commands or fabricated logs.
