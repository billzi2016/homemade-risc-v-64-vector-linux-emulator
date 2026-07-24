# Linux 真实启动验收结果

本文只记录实际执行过的结果。未执行或失败的项目必须明确写出，不提前标记成功。

## 1. 当前产物

```text
09f48fb16f858a16e7cd507fbb3a0fa0c9b430c1d50f8dec2130598a7f42ddea  artifacts/firmware/fw_jump.bin
544deecebc94f5dc87fe52420f5ede9fa111a1321da189f571b6fe7038e84764  artifacts/kernel/Image
b8c86f9096057191bb7659e749772933347d25fd193cc43c2baab2f2afc113e6  artifacts/disk/rootfs.ext4
88d1ea033ef988082f0910bc263ee07d5ba58136789155f5c0718f9c6939b6ac  artifacts/buildroot.config
```

文件类型检查：

```text
artifacts/firmware/fw_jump.bin: data
artifacts/kernel/Image:         MS-DOS executable PE32+ executable (EFI application) RISC-V 64-bit
artifacts/disk/rootfs.ext4:     Linux rev 1.0 ext4 filesystem data, volume name "rootfs"
artifacts/buildroot.config:     ASCII text
```

## 2. 已完成验证

- Buildroot `2026.05.1` 在 Docker Linux 容器中完成真实构建。
- `artifacts/disk/rootfs.ext4` 是真实 ext4 文件系统，不是 initramfs 冒充。
- 常规项目构建通过。
- 常规 CTest：`26/26` 通过。
- ASan/UBSan 构建通过。
- ASan/UBSan CTest：`26/26` 通过。

## 3. 真实启动验证

状态：已完成 OpenSBI 真实 banner 验证；Linux 到 Shell 尚未完成。

待执行命令：

```sh
./build/riscv_vector_emulator \
  --bios artifacts/firmware/fw_jump.bin \
  --kernel artifacts/kernel/Image \
  --disk artifacts/disk/rootfs.ext4 \
  --net none
```

已保存证据：

- `artifacts/logs/linux-boot-uart.log`：`BOOT_SECONDS=35` 窗口，包含真实 `OpenSBI v1.6` banner。
- OpenSBI 识别 `rvemu,riscv64-gcv-single-hart`、hart 0、`aclint-mtimer @ 10000000Hz`、`uart8250`。
- OpenSBI next stage：`Domain0 Next Address=0x80200000`、`Domain0 Next Arg1=0x82200000`、`Domain0 Next Mode=S-mode`。
- OpenSBI 委托状态：`MIDELEG=0x0000000000000222`、`MEDELEG=0x000000000000b109`。

待保存证据：

- Linux kernel 启动到 init 前后的完整日志。
- VirtIO-Blk 探测日志。
- ext4 rootfs 挂载日志。
- 来宾 Shell 中 `ls /`、`pwd`、`cat /proc/cpuinfo` 的实际输出。

## 4. macOS 网络验收

状态：macOS 做不了，不执行、不打勾。

原因：macOS 没有 Linux TAP `/dev/net/tun` 链路；当前项目也没有 macOS tuntap/utun 后端补丁。不得使用宿主 DNS、宿主 ping 或伪造日志替代来宾 `eth0` 验收。
