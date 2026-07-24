# Linux 构建与运行手册

本文记录在仓库根目录执行的真实 OpenSBI、Linux kernel 和 ext4 rootfs 构建与运行流程。所有项目路径均为仓库相对路径；不要把宿主机绝对工作区路径写入文档、日志索引或任务证据。

## 1. 产物路径

- Buildroot 源码包：`artifacts/sources/buildroot-2026.05.1.tar.xz`
- Buildroot 解压源码：`artifacts/sources/buildroot-2026.05.1/`
- 构建输出副本：`artifacts/buildroot-linux-images/`
- 生产运行固件：`artifacts/firmware/fw_jump.bin`
- 生产运行内核：`artifacts/kernel/Image`
- 生产运行磁盘：`artifacts/disk/rootfs.ext4`
- Buildroot 配置副本：`artifacts/buildroot.config`
- 运行日志目录：`artifacts/logs/`

`artifacts/` 是外部产物目录，不提交 Git。

## 2. 已冻结版本与校验

- Buildroot：`2026.05.1`
- Buildroot 发布日期：`2026-07-15`
- Buildroot 下载页：`https://buildroot.org/download.html`
- Buildroot 新闻页：`https://buildroot.org/news.html`
- Buildroot 源码 SHA-256：
  `ae7f706f087b9ae9083a10a587368dfbf53103c28bf81c2d690198dc4090cb58`

本地构建产物 SHA-256：

```text
09f48fb16f858a16e7cd507fbb3a0fa0c9b430c1d50f8dec2130598a7f42ddea  artifacts/firmware/fw_jump.bin
544deecebc94f5dc87fe52420f5ede9fa111a1321da189f571b6fe7038e84764  artifacts/kernel/Image
b8c86f9096057191bb7659e749772933347d25fd193cc43c2baab2f2afc113e6  artifacts/disk/rootfs.ext4
88d1ea033ef988082f0910bc263ee07d5ba58136789155f5c0718f9c6939b6ac  artifacts/buildroot.config
```

## 3. 宿主工具

已使用的宿主工具：

- Docker Desktop：用于 Linux 容器内构建，避免 macOS 直接构建工具链。
- `dtc`：用于后续 DTB 验证。
- `qemu-img`：用于必要的磁盘镜像检查或转换。
- `mkfs.ext4`：来自 e2fsprogs，用于必要时检查 ext4 能力。
- `riscv64-elf-gcc`：用于小型 RISC-V 裸机测试辅助，不替代 Linux 构建。

## 4. Buildroot 构建命令

从仓库根目录执行。Docker 绑定目录使用当前目录变量，不在文档中写宿主机绝对路径。

```sh
docker run --rm \
  -v "$PWD:/work" \
  -w /tmp \
  ubuntu:24.04 \
  bash -lc 'set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends build-essential ca-certificates bc cpio file rsync unzip wget python3 perl libncurses-dev git patch xz-utils bzip2 gzip make
mkdir -p /tmp/br-src /tmp/br-output /tmp/br-dl /work/artifacts/buildroot-linux-images
tar -xf /work/artifacts/sources/buildroot-2026.05.1.tar.xz -C /tmp/br-src --strip-components=1
make -C /tmp/br-src O=/tmp/br-output qemu_riscv64_virt_defconfig
sed -i \
  -e "s/^BR2_PACKAGE_HOST_QEMU=y/# BR2_PACKAGE_HOST_QEMU is not set/" \
  -e "s/^BR2_PACKAGE_HOST_QEMU_SYSTEM_MODE=y/# BR2_PACKAGE_HOST_QEMU_SYSTEM_MODE is not set/" \
  -e "s/^BR2_TARGET_ROOTFS_EXT2_2=y/# BR2_TARGET_ROOTFS_EXT2_2 is not set/" \
  -e "s/^BR2_TARGET_ROOTFS_EXT2_2r1=y/# BR2_TARGET_ROOTFS_EXT2_2r1 is not set/" \
  -e "s/^# BR2_TARGET_ROOTFS_EXT2_4 is not set/BR2_TARGET_ROOTFS_EXT2_4=y/" \
  -e "s/^BR2_TARGET_ROOTFS_EXT2_GEN=.*/BR2_TARGET_ROOTFS_EXT2_GEN=4/" \
  -e "s/^BR2_TARGET_ROOTFS_EXT2_SIZE=.*/BR2_TARGET_ROOTFS_EXT2_SIZE=\"128M\"/" \
  /tmp/br-output/.config
make -C /tmp/br-src O=/tmp/br-output BR2_DL_DIR=/tmp/br-dl olddefconfig
make -C /tmp/br-src O=/tmp/br-output BR2_DL_DIR=/tmp/br-dl -j8
cp /tmp/br-output/images/Image /work/artifacts/buildroot-linux-images/Image
cp /tmp/br-output/images/fw_jump.bin /work/artifacts/buildroot-linux-images/fw_jump.bin
cp /tmp/br-output/images/rootfs.ext4 /work/artifacts/buildroot-linux-images/rootfs.ext4
cp /tmp/br-output/.config /work/artifacts/buildroot-linux-images/buildroot.config
sha256sum /work/artifacts/buildroot-linux-images/Image /work/artifacts/buildroot-linux-images/fw_jump.bin /work/artifacts/buildroot-linux-images/rootfs.ext4 /work/artifacts/buildroot-linux-images/buildroot.config'
```

说明：

- Buildroot 的大构建树放在容器内部 `/tmp/br-output`，最终只复制必要产物回 `artifacts/buildroot-linux-images/`。
- `-j8` 是为了降低内存压力和宿主 SSD 交换写入风险，不是降低 Linux 功能规格。
- 关闭 `BR2_PACKAGE_HOST_QEMU` 是因为本项目运行自己的模拟器，不需要 Buildroot 再构建 host QEMU。
- ext4 通过 `BR2_TARGET_ROOTFS_EXT2_4=y` 和 `BR2_TARGET_ROOTFS_EXT2_GEN=4` 生成；文件名符号链接仍来自 Buildroot 的 `rootfs.ext4`。

## 5. 复制到运行路径

```sh
cp artifacts/buildroot-linux-images/fw_jump.bin artifacts/firmware/fw_jump.bin
cp artifacts/buildroot-linux-images/Image artifacts/kernel/Image
cp artifacts/buildroot-linux-images/rootfs.ext4 artifacts/disk/rootfs.ext4
cp artifacts/buildroot-linux-images/buildroot.config artifacts/buildroot.config
```

校验：

```sh
shasum -a 256 artifacts/firmware/fw_jump.bin artifacts/kernel/Image artifacts/disk/rootfs.ext4 artifacts/buildroot.config
file artifacts/firmware/fw_jump.bin artifacts/kernel/Image artifacts/disk/rootfs.ext4 artifacts/buildroot.config
```

## 6. 项目回归验证

```sh
cmake --build build -j8
ctest --test-dir build --output-on-failure
cmake --build build/sanitize -j8
ctest --test-dir build/sanitize --output-on-failure
```

当前已验证：常规 CTest `26/26` 通过，ASan/UBSan CTest `26/26` 通过。

## 7. 一键刷新固定日志

从仓库根目录执行：

```sh
./run_all_logs.sh
```

默认固定覆盖：

- `artifacts/logs/build.log`
- `artifacts/logs/ctest.log`
- `artifacts/logs/linux-boot-uart.log`

需要同时覆盖诊断 UART 日志时执行：

```sh
RUN_DEBUG_BOOT=1 ./run_all_logs.sh
```

`BOOT_SECONDS` 控制单次 Linux 启动记录窗口，默认 `600` 秒。例如：

```sh
BOOT_SECONDS=900 ./run_all_logs.sh
```

## 8. macOS 真实启动命令

macOS 收尾只使用 `--net none`，不创建 TAP，不修改宿主网络。

```sh
./build/riscv_vector_emulator \
  --bios artifacts/firmware/fw_jump.bin \
  --kernel artifacts/kernel/Image \
  --disk artifacts/disk/rootfs.ext4 \
  --net none
```

期望：

- OpenSBI 从 `fw_jump.bin` 启动并跳转 Linux。
- Linux 通过 FDT 识别 RAM、Sv39、CLINT、PLIC、UART 和 VirtIO MMIO。
- Linux 识别 VirtIO-Blk，使用 `rootwait root=/dev/vda rootfstype=ext4` 挂载 `artifacts/disk/rootfs.ext4`。
- 进入真实来宾 Shell 后执行 `ls /`、`pwd`、`cat /proc/cpuinfo`。

## 9. macOS 网络限制

Linux TAP 网络验收在 macOS 不做，任务中统一标记为“macOS 做不了”。原因：

- macOS 没有 Linux `/dev/net/tun` TAP 工作流。
- 创建桥接、路由、转发或抓包通常需要宿主网络权限。
- 项目当前没有针对 macOS 的 tuntap/utun 后端实现和补丁链路。
- 不得用宿主 DNS、宿主 ping 或伪日志替代来宾 `eth0` 验收。
