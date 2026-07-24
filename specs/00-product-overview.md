# Product Overview

## 1. Product Positioning

`homemade-risc-v-64-vector-linux-emulator` is a pure command-line, standalone full-system emulator. It implements a minimal virtual hardware platform supporting RV64GCV in host software, capable of booting OpenSBI and Linux, mounting an ext4 root filesystem, and providing an interactive Shell via UART. Host Linux can also connect to real networks via VirtIO-Net and TAP.

## 2. Core Requirements

- **PRD-REQ-001**: Provide RV64GCV CPU, M/S/U privilege modes, key CSRs, exceptions, and interrupt semantics.
- **PRD-REQ-002**: Provide RVV 1.0 vector execution capability with VLEN=256.
- **PRD-REQ-003**: Provide Sv39, TLB, and `SFENCE.VMA` sufficient to run target Linux correctly.
- **PRD-REQ-004**: Provide machine models for Boot ROM, RAM, CLINT, PLIC, UART, VirtIO-Blk, and VirtIO-Net.
- **PRD-REQ-005**: Load BIOS, kernel, and disk via a single CLI command, explicitly choosing to disable networking or bind Linux TAP.
- **PRD-REQ-006**: Boot real OpenSBI and Linux, entering an interactive Shell.
- **PRD-REQ-007**: Under the Linux network profile, the guest accesses public networks over real DHCP/DNS/ICMP links, with a final `ping` packet loss rate of 0%.
- **PRD-REQ-008**: Under the macOS non-networked profile, the guest must genuinely enter Shell and successfully execute `ls /`, `pwd`, and `cat /proc/cpuinfo`.

## 3. Quality Goals

- Semantic correctness takes priority over execution speed.
- Core rules maintain a single authoritative implementation, adhering to SOLID and DRY.
- All code possesses complete and accurate maintenance comments.
- Errors are diagnosable, clearly distinguishing guest exceptions from host faults.
- Builds and external artifact acquisitions are reproducible; binary blobs are not committed to Git.

## 4. Explicit Scope

### 4.1 In Scope

- Single-hart RV64 full-system emulation; machine model retains boundaries for future multi-hart extension, but initial release makes no SMP commitment.
- RV64I, M, A, F, D, C, and RVV 1.0 instruction coverage meeting Linux/acceptance requirements.
- M/S/U privilege modes and key CSRs and delegation mechanisms used by Linux.
- Sv39 virtual memory and a TLB with at least 64 entries.
- Minimal MMIO platform mapped at specified physical addresses.
- VirtIO MMIO split virtqueues, block device, and network card.
- `/dev/net/tun` TAP backend on Linux hosts.
- Complete non-networked boot profile on macOS hosts; reuses the same CPU, MMU, block device, UART, and execution loop.
- POSIX terminal Raw mode and CLI lifecycle.

### 4.2 Out of Scope

- GUI, web console, or desktop manager.
- JIT, dynamic binary translation, or performance-first optimizations unless separately approved in future specs.
- PCI/PCIe, USB, GPU, audio, and full motherboard physical peripherals.
- Hardware virtualization acceleration or directly invoking host RISC-V execution.
- Substituting user-mode network simulation for PRD-specified TAP/bridge final links.
- Committing external firmware, kernel, and rootfs binaries to the repository.

## 5. Target Execution Commands

Linux network profile:

```text
./riscv_vector_emulator \
  --bios opensbi.bin \
  --kernel vmlinux.bin \
  --disk rootfs.ext4 \
  --net tap0
```

macOS non-networked profile changes the last option to `--net none`; omitting `--net` is equivalent to explicit `none`.

Actual load addresses, default RAM size, FDT address, and optional diagnostic parameters are fixed by subsequent specifications. When required arguments are missing or files are inaccessible, the program must display a clear error and exit before altering terminal or network states.

## 6. User-Observable Lifecycle

1. CLI validates parameters and host resources.
2. Allocates and initializes RAM, ROM, and devices.
3. Loads OpenSBI, Linux, and FDT, binds disk, and binds TAP per configuration option.
4. Safely switches terminal to Raw mode.
5. Executes main CPU loop and handles device events.
6. UART outputs OpenSBI and Linux boot logs.
7. User enters Shell; macOS profile executes basic commands, while Linux network profile configures `eth0` and accesses public networks.
8. Restores terminal and releases all resources upon normal or abnormal exit.

## 7. Final Acceptance Scenarios

Both host profiles must complete the first three items using real artifacts in recorded environments:

1. UART displays real OpenSBI Banner.
2. Linux kernel completes boot without Kernel Panic.
3. Root filesystem mounts successfully, showing an interactive Shell.
4. macOS non-networked profile successfully executes `ls /`, `pwd`, and `cat /proc/cpuinfo`.
5. Linux network profile executes `dhclient eth0`, domain resolution, and `ping -c 4 google.com`, receiving 4 replies with 0% packet loss.

Any artificial prints, pre-recorded logs, host-side execution on behalf of guest, or mock network results are invalid.

## 8. Version Decisions Pending User Freeze

Must be confirmed in `SDD-004` prior to implementation:

- Exact versions of RISC-V unprivileged, privileged, and RVV specifications.
- VirtIO specification version and MMIO transport version.
- OpenSBI, Linux LTS, cross-toolchain, and rootfs versions.
- Whether initial release strictly supports single-hart only.
- Host profiles frozen as macOS non-networked boot and Linux TAP full network; both share guest hardware implementations.
