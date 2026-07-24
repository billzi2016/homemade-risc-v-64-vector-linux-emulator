# Standard Version Baseline

## 1. Purpose

This document freezes the formal standards referenced during project implementation, preventing mixing of encodings, CSRs, page tables, or device semantics from different versions. Standard annotations in code and tests must cite versions in this document; upgrading standards requires an impact analysis and prior user confirmation.

## 2. RISC-V Unprivileged ISA

Adopts RISC-V International [Unprivileged ISA 20260120 Official Release](https://docs.riscv.org/reference/isa/unpriv/unpriv-index.html), locking the following ratified modules:

| Module | Version | Project Purpose |
| --- | --- | --- |
| RV64I | 2.1 | 64-bit base integer instructions |
| Zicsr | 2.0 | CSR instructions |
| Zifencei | 2.0 | `FENCE.I` |
| M | 2.0 | Integer multiply/divide |
| A | 2.1 | LR/SC and AMO |
| F | 2.2 | Single-precision floating-point |
| D | 2.2 | Double-precision floating-point |
| C | 2.0 | Compressed instructions |
| V | 1.0 | Base vector extension |

Optional extensions omitted from this table default to un-declared and un-decoded. If locked configurations of Linux or OpenSBI require extra extensions, update this document and corresponding tasks first.

## 3. RISC-V Privileged Architecture

Adopts RISC-V International [Privileged ISA 20260120 Official Release](https://docs.riscv.org/reference/isa/priv/priv-index.html), with Machine ISA and Supervisor ISA both observing ratified 1.13 semantics.

Initial release implements M/S/U, Sv39, and interrupts/CSRs required by PRD, omitting Hypervisor extension. A/D bits select hardware update paths; whether to formally declare `Svadu`, PMP scopes, and other Supervisor extensions must be frozen separately before starting CPU/MMU modules, rather than claiming support based solely on partial behaviors.

## 4. RVV

Vector semantics adopt official [`V` Standard Extension 1.0](https://docs.riscv.org/reference/isa/unpriv/v-st-ext). Hardware parameters are fixed at `VLEN=256`, `ELEN=64`, `vlenb=32`, containing 32 vector registers.

## 5. VirtIO

Adopts OASIS [Virtual I/O Device (VIRTIO) Version 1.2](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html). Project uses modern MMIO transport, Version 1 compliant device model, and split virtqueue; omitting legacy transports. Optional features are advertised only after completing implementation and testing.

## 6. External Software Version Status

Precise versions of OpenSBI, Linux LTS, cross toolchain, and minimal rootfs belong to external artifact decisions, currently un-frozen and un-downloaded. They must be selected from official sources, recording licenses and SHA-256 checksums, and confirmed by user prior to starting boot modules. Thus `SDD-004` cannot be marked complete currently.

## 7. Applicable Clauses for This Module

Physical bus and RAM/ROM modules directly observe:

- 64-bit physical access value representation and little-endian memory byte order of RV64.
- Explicit access widths of 8, 16, 32, 64 bits only.
- Physical ranges using half-open intervals with overflow checks on all additions.
- Atomic compare-exchange as formal bus transaction, providing sole base capability for subsequent A extension and PTE A/D updates.

This document freezes standards only, and does not imply any matching ISA or device module is implemented.
