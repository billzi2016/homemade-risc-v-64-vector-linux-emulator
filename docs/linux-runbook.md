# Linux Build and Execution Runbook

This document records the real OpenSBI, Linux kernel, and ext4 rootfs build and execution workflow executed from the repository root directory. All project paths are relative to the repository root; do not write host machine absolute workspace paths into documents, log indexes, or task evidence.

## 1. Artifact Paths

- Buildroot source archive: `artifacts/sources/buildroot-2026.05.1.tar.xz`
- Buildroot extracted source: `artifacts/sources/buildroot-2026.05.1/`
- Build output copy: `artifacts/buildroot-linux-images/`
- Production firmware: `artifacts/firmware/fw_jump.bin`
- Production kernel: `artifacts/kernel/Image`
- Production disk: `artifacts/disk/rootfs.ext4`
- Buildroot config copy: `artifacts/buildroot.config`
- Execution logs directory: `artifacts/logs/`

`artifacts/` is an external artifacts directory and is not committed to Git.

## 2. Frozen Versions and Checksums

- Buildroot: `2026.05.1`
- Buildroot release date: `2026-07-15`
- Buildroot download page: `https://buildroot.org/download.html`
- Buildroot news page: `https://buildroot.org/news.html`
- Buildroot source SHA-256:
  `ae7f706f087b9ae9083a10a587368dfbf53103c28bf81c2d690198dc4090cb58`

Local build artifact SHA-256:

```text
09f48fb16f858a16e7cd507fbb3a0fa0c9b430c1d50f8dec2130598a7f42ddea  artifacts/firmware/fw_jump.bin
544deecebc94f5dc87fe52420f5ede9fa111a1321da189f571b6fe7038e84764  artifacts/kernel/Image
b8c86f9096057191bb7659e749772933347d25fd193cc43c2baab2f2afc113e6  artifacts/disk/rootfs.ext4
88d1ea033ef988082f0910bc263ee07d5ba58136789155f5c0718f9c6939b6ac  artifacts/buildroot.config
```

## 3. Host Tools

Host tools utilized:

- Docker Desktop: Used for builds inside Linux containers to avoid direct macOS toolchain builds.
- `dtc`: Used for subsequent DTB verification.
- `qemu-img`: Used for necessary disk image inspection or conversion.
- `mkfs.ext4`: From e2fsprogs, used to inspect ext4 capabilities when necessary.
- `riscv64-elf-gcc`: Used for small RISC-V bare-metal test helpers; does not replace Linux builds.

## 4. Buildroot Build Command

Execute from the repository root directory. Docker bind mounts use the current directory variable, avoiding absolute host machine paths in documentation.

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

Notes:

- Buildroot's large build tree is kept inside the container at `/tmp/br-output`; only essential artifacts are copied back to `artifacts/buildroot-linux-images/`.
- `-j8` is used to reduce memory pressure and host SSD swap write risks, not to lower Linux feature specifications.
- `BR2_PACKAGE_HOST_QEMU` is disabled because this project runs its own emulator and does not need Buildroot to build host QEMU.
- ext4 is generated via `BR2_TARGET_ROOTFS_EXT2_4=y` and `BR2_TARGET_ROOTFS_EXT2_GEN=4`; the filename symlink originates from Buildroot's `rootfs.ext4`.

## 5. Copying to Execution Paths

```sh
cp artifacts/buildroot-linux-images/fw_jump.bin artifacts/firmware/fw_jump.bin
cp artifacts/buildroot-linux-images/Image artifacts/kernel/Image
cp artifacts/buildroot-linux-images/rootfs.ext4 artifacts/disk/rootfs.ext4
cp artifacts/buildroot-linux-images/buildroot.config artifacts/buildroot.config
```

Verification:

```sh
shasum -a 256 artifacts/firmware/fw_jump.bin artifacts/kernel/Image artifacts/disk/rootfs.ext4 artifacts/buildroot.config
file artifacts/firmware/fw_jump.bin artifacts/kernel/Image artifacts/disk/rootfs.ext4 artifacts/buildroot.config
```

## 6. Project Regression Verification

```sh
cmake --build build -j8
ctest --test-dir build --output-on-failure
cmake --build build/sanitize -j8
ctest --test-dir build/sanitize --output-on-failure
```

Currently verified: Standard CTest `26/26` passed, ASan/UBSan CTest `26/26` passed.

## 7. One-Click Refreshing Fixed Logs

Execute from the repository root:

```sh
./run_all_logs.sh
```

Overwrites fixed files by default:

- `artifacts/logs/build.log`
- `artifacts/logs/ctest.log`
- `artifacts/logs/linux-boot-uart.log`

To overwrite diagnostic UART logs simultaneously, execute:

```sh
RUN_DEBUG_BOOT=1 ./run_all_logs.sh
```

`BOOT_SECONDS` controls the single Linux boot recording window, defaulting to `600` seconds. For example:

```sh
BOOT_SECONDS=900 ./run_all_logs.sh
```

## 8. macOS Real Boot Command

macOS finalization strictly uses `--net none`, without creating TAP or modifying host network.

```sh
./build/riscv_vector_emulator \
  --bios artifacts/firmware/fw_jump.bin \
  --kernel artifacts/kernel/Image \
  --disk artifacts/disk/rootfs.ext4 \
  --net none
```

Expectations:

- OpenSBI boots from `fw_jump.bin` and jumps to Linux.
- Linux detects RAM, Sv39, CLINT, PLIC, UART, and VirtIO MMIO via FDT.
- Linux detects VirtIO-Blk and mounts `artifacts/disk/rootfs.ext4` using `rootwait root=/dev/vda rootfstype=ext4`.
- Enters real guest Shell and executes `ls /`, `pwd`, `cat /proc/cpuinfo`.

## 9. macOS Network Limitations

Linux TAP network acceptance is skipped on macOS and marked consistently as "Unsupported on macOS" in tasks. Reasons:

- macOS lacks the Linux `/dev/net/tun` TAP workflow.
- Creating bridges, routing, forwarding, or packet captures typically requires host network administrative privileges.
- The project currently lacks a macOS tuntap/utun backend implementation and patch chain.
- Host DNS, host ping, or fake logs must not be used to substitute guest `eth0` acceptance.
