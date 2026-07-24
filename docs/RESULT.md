# Real Linux Boot Acceptance Results

This document records only results that have actually been executed. Unexecuted or failed items must be explicitly stated and will not be marked as successful prematurely.

## 1. Current Artifacts

```text
09f48fb16f858a16e7cd507fbb3a0fa0c9b430c1d50f8dec2130598a7f42ddea  artifacts/firmware/fw_jump.bin
544deecebc94f5dc87fe52420f5ede9fa111a1321da189f571b6fe7038e84764  artifacts/kernel/Image
b8c86f9096057191bb7659e749772933347d25fd193cc43c2baab2f2afc113e6  artifacts/disk/rootfs.ext4
88d1ea033ef988082f0910bc263ee07d5ba58136789155f5c0718f9c6939b6ac  artifacts/buildroot.config
```

File type verification:

```text
artifacts/firmware/fw_jump.bin: data
artifacts/kernel/Image:         MS-DOS executable PE32+ executable (EFI application) RISC-V 64-bit
artifacts/disk/rootfs.ext4:     Linux rev 1.0 ext4 filesystem data, volume name "rootfs"
artifacts/buildroot.config:     ASCII text
```

## 2. Completed Verification

- Buildroot `2026.05.1` successfully built in a real Docker Linux container.
- `artifacts/disk/rootfs.ext4` is a real ext4 filesystem, not an initramfs imposter.
- Standard project build passed.
- Standard CTest: `26/26` passed.
- ASan/UBSan build passed.
- ASan/UBSan CTest: `26/26` passed.

## 3. Real Boot Verification

Status: Completed real OpenSBI banner verification; Linux to Shell remains pending.

Command to execute:

```sh
./build/riscv_vector_emulator \
  --bios artifacts/firmware/fw_jump.bin \
  --kernel artifacts/kernel/Image \
  --disk artifacts/disk/rootfs.ext4 \
  --net none
```

Saved Evidence:

- `artifacts/logs/linux-boot-uart.log`: `BOOT_SECONDS=35` window containing real `OpenSBI v1.6` banner.
- OpenSBI identifies `rvemu,riscv64-gcv-single-hart`, hart 0, `aclint-mtimer @ 10000000Hz`, `uart8250`.
- OpenSBI next stage: `Domain0 Next Address=0x80200000`, `Domain0 Next Arg1=0x82200000`, `Domain0 Next Mode=S-mode`.
- OpenSBI delegation status: `MIDELEG=0x0000000000000222`, `MEDELEG=0x000000000000b109`.

Pending Evidence:

- Complete Linux kernel boot logs up to and after init.
- VirtIO-Blk probe logs.
- ext4 rootfs mount logs.
- Actual output of `ls /`, `pwd`, `cat /proc/cpuinfo` in guest Shell.

## 4. macOS Networking Acceptance

Status: Unsupported on macOS; unexecuted and unchecked.

Reason: macOS lacks the Linux TAP `/dev/net/tun` link; the current project does not include macOS tuntap/utun backend patches. Host DNS, host ping, or fabricated logs must not be used to fake guest `eth0` acceptance.
