# Quickstart Guide

## 1. Scope

This document describes the two real boot execution paths after the project reaches its final delivery state: booting to the Linux Shell under the macOS non-networked profile, and verifying public internet connectivity under the Linux TAP profile. Both profiles utilize identical emulator binaries, firmware, kernel images, and rootfs images without establishing alternative hardware logic.

The README describes the final product state; whether the current repository possesses a specific capability should be determined strictly by the acceptance status in `specs/tasks.md`. If the current commit has not yet built `riscv_vector_emulator`, complete the task checklist first rather than creating scripts with matching names to fake an executable.

## 2. Prerequisites

- 64-bit Linux or Apple Silicon/Intel macOS host.
- CMake 3.20+ and a C++17-compliant compiler.
- Installed RISC-V cross-toolchain, DTC, and e2fsprogs required for current steps.
- Built OpenSBI, Linux kernel, and ext4 rootfs configured for our machine layout.
- Only the Linux network profile requires administrative permissions to create or use TAP interfaces.

For third-party component roles, official sources, and installation commands, refer to `docs/third-party.md`.

## 3. Obtaining Source Code

```bash
git clone https://github.com/billzi2016/homemade-risc-v-64-vector-linux-emulator.git
cd homemade-risc-v-64-vector-linux-emulator
```

All subsequent commands are executed from the repository root directory. Documentation will never require entering or writing to any user path outside the project directory.

## 4. Building and Testing the Emulator

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Proceed to system boot steps only after configuration, compilation, and all mandatory tests pass cleanly. The final executable is located at:

```text
build/riscv_vector_emulator
```

If Ninja is not installed, remove `-G Ninja` to let CMake use the host default generator; this does not alter emulator functionality.

## 5. Placing Boot Resources

Prepare the following directory layout and files:

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

The three resource files must match our virtual hardware layout:

- `opensbi.bin`: Entry address and next-stage jump protocol match the Boot ROM.
- `vmlinux.bin`: Targeted at RV64, enabling serial console, PLIC, CLINT, VirtIO MMIO, block devices, NIC, and ext4.
- `rootfs.ext4`: Contains `init`, shell, device node management, DHCP client, DNS configuration, and `ping`.

Verify files exist and are non-empty:

```bash
test -s artifacts/firmware/opensbi.bin
test -s artifacts/kernel/vmlinux.bin
test -s artifacts/disk/rootfs.ext4
```

Execute a read-only check on the ext4 image:

```bash
e2fsck -fn artifacts/disk/rootfs.ext4
```

Do not execute `git add -f` on these files; they are local external artifacts excluded by `.gitignore`.

## 6. Selecting Host Profile

macOS uses the non-networked profile, without creating TAP or altering system networking. Skip directly to the next section and use `--net none`.

The Linux network profile requires an existing TAP interface in UP state, such as `tap0`. First check Linux TUN/TAP capabilities:

```bash
test -c /dev/net/tun
ip tuntap help
```

Creating TAP, adding it to a bridge, or configuring NAT alters host networking and firewalls. Administrators should perform this according to `specs/14-host-network-setup.md`, actual uplink interfaces, and LAN policies, rather than blindly copying fixed interface names or IP subnets.

Upon completion, perform a read-only check of the interface status:

```bash
ip -details link show tap0
```

The output must contain TAP information and show an UP state. The emulator process must also possess permissions to open `/dev/net/tun` and bind to the interface.

## 7. Launching Full Virtual Machine

macOS non-networked profile:

```bash
./build/riscv_vector_emulator \
  --bios artifacts/firmware/opensbi.bin \
  --kernel artifacts/kernel/vmlinux.bin \
  --disk artifacts/disk/rootfs.ext4 \
  --net none
```

Linux TAP network profile:

```bash
./build/riscv_vector_emulator \
  --bios artifacts/firmware/opensbi.bin \
  --kernel artifacts/kernel/vmlinux.bin \
  --disk artifacts/disk/rootfs.ext4 \
  --net tap0
```

The boot sequence should sequentially display:

1. OpenSBI Banner and platform details.
2. Early Linux boot logs, memory, and interrupt controller initialization.
3. UART and VirtIO-Blk driver probe messages; VirtIO-Net should also appear on the Linux network profile.
4. Successful ext4 root filesystem mount.
5. `init` startup showing an interactive command shell.

The host terminal enters Raw mode. Keyboard input is sent directly to guest UART; `Ctrl+C` should be handled by the guest rather than forcefully terminating the emulator. The emulator must restore host terminal attributes upon normal exit or error.

## 8. Guest Acceptance

On macOS non-networked profile, execute in guest shell:

```bash
ls /
pwd
cat /proc/cpuinfo
```

All three commands must execute successfully inside the real guest shell. This result proves the local full boot chain, but does not claim network acceptance.

On Linux TAP network profile, continue with PRD-required DHCP and public network tests in guest shell:

```bash
dhclient eth0
ip address show dev eth0
ip route show
ping -c 4 google.com
```

Passing criteria:

- `eth0` acquires an independent IPv4 address in the expected subnet.
- Default route points to the correct gateway.
- `google.com` resolves via guest DNS configuration.
- Receives 4 ICMP Echo Replies with 0% packet loss.

Public networks may rate-limit or block ICMP; if packet loss occurs, troubleshoot TAP RX/TX, ARP, DHCP, DNS, routing, firewalls, and upstream networks separately. Do not declare a VirtIO-Net implementation flaw based solely on a single failed `ping`.

## 9. Saving Acceptance Information

When evidence retention is required, restart from host and direct logs to the repository log directory:

```bash
./build/riscv_vector_emulator \
  --bios artifacts/firmware/opensbi.bin \
  --kernel artifacts/kernel/vmlinux.bin \
  --disk artifacts/disk/rootfs.ext4 \
  --net tap0 \
  2>artifacts/logs/emulator-stderr.log
```

When UART uses an interactive terminal, do not simply replace TTY with a plain pipe, as this alters acceptance conditions for Raw mode, control characters, and input timing. Logs must not contain host keys, tokens, passwords, or sensitive data.

## 10. Common Failure Troubleshooting

| Symptom | Primary Inspection Points |
| --- | --- |
| Executable not found | Current task status, CMake configuration, and link targets |
| OpenSBI no output | Boot ROM trampoline, firmware load address, PC, UART address, FDT pointer |
| Hangs after OpenSBI | CSRs, `medeleg`/`mideleg`, `MRET`, timers, and S-mode entry point |
| Linux page fault loop | `satp`, Sv39 canonical VA, PTE permissions, superpages, A/D bits, and TLB |
| Rootfs not found | VirtIO-MMIO descriptions, queue layouts, disk image, kernel ext4 config |
| Deadlock after block device runs | Descriptor Chain, Available/Used Ring index wrapping, memory publication order |
| `eth0` does not exist | FDT VirtIO nodes, VirtIO-Net feature negotiation, PLIC interrupts |
| DHCP timeout | TAP status, bridge/NAT, queue RX buffers, external interrupts |
| Can ping IP but cannot resolve domain | DNS configuration in rootfs and DHCP lease content |
| Abnormal terminal state upon exit | Termios save/restore and all error exit paths |

During debugging, fix the production execution chain; do not introduce secondary address translation, device logic, or fake returns solely to bypass boot nodes.

## 11. Cleanup Notes

Deleting `build/` cleans local build artifacts; `artifacts/` holds re-downloadable or regenerable local files. Cleaning TAP, bridges, routing, or nftables rules alters host network state; use controlled procedures matching their creation rather than broad deletion commands.
