# Physical Bus & MMIO Specification

## 1. Sole Physical Access Entry

- **BUS-REQ-001**: All CPU translated accesses, page table walks, Boot loading, and device DMA must pass through controlled physical bus or its explicit initialization interfaces.
- **BUS-REQ-002**: Runtime code must not bypass bounds, read-only, and device routing rules via raw RAM pointers.
- **BUS-REQ-003**: Bus supports 8, 16, 32, 64-bit little-endian accesses, explicitly distinguishing read, write, fetch, atomic, and DMA origins.

## 2. Fixed Address Map

| Region | Start Address | End Address (Inclusive) | Size / Notes |
| --- | ---: | ---: | --- |
| Boot ROM | `0x00001000` | `0x0000BFFF` | 44 KiB |
| CLINT | `0x02000000` | `0x0200BFFF` | 48 KiB |
| PLIC | `0x0C000000` | `0x0FFFFFFF` | 64 MiB |
| UART 16550A | `0x10000000` | `0x100000FF` | 256 B |
| VirtIO-Blk MMIO | `0x10001000` | `0x10001FFF` | 4 KiB |
| VirtIO-Net MMIO | `0x10002000` | `0x10002FFF` | 4 KiB |
| RAM | `0x80000000` | `0x80000000 + ram_size - 1` | Default recommended 1 GiB |

All regions are closed intervals and must not overlap. `base + size` calculation must check 64-bit overflow.

## 3. Boot ROM

- Writable via controlled loading interfaces prior to execution to populate reset trampoline and FDT.
- Guest writes after machine starts execution must generate store access faults.
- Uninitialized ROM region value strategy must be frozen and tested.
- Instruction fetch is permitted, but still undergoes range and access width checks.

## 4. RAM

- Capacity is determined by configuration, with default frozen in CLI spec.
- Allocation failures must exit cleanly before startup, without silently degrading to smaller RAM without notice.
- RAM initial contents adopt deterministic zero-initialization strategy, with image load regions overwritten per original bytes.
- Reads/writes must check the entire `[address, address+size)` resides within RAM.
- Any single access straddling RAM/MMIO boundaries must not be split into partial side effects.

## 5. MMIO Dispatch

- **BUS-REQ-004**: Device registration detects address overlaps and rejects startup.
- **BUS-REQ-005**: Unmapped addresses generate instruction/load/store access faults, rather than returning zero or ignoring writes.
- **BUS-REQ-006**: Devices must declare permitted widths and alignments; illegal accesses generate access faults.
- **BUS-REQ-007**: MMIO accesses must not misuse normal RAM atomic logic; device-supported atomic semantics must be explicitly defined.

## 6. Access Results

Bus returns structured results, distinguishing at least: success, unmapped, out of bounds, read-only, unsupported width, alignment error, and backend I/O fault. Upper layers map results to guest Traps or host fatal errors based on origin, without losing physical address and device information.

## 7. DMA Safety

VirtIO DMA interfaces must:

- Accept guest physical addresses only.
- Check addition overflow and full range.
- Default to allowing RAM only, disallowing devices from accessing MMIO or ROM via descriptors.
- Distinguish device-read guest buffers from device-write guest buffers.
- Handle failure across RAM boundaries without generating partial used ring completions, unless device specs explicitly specify error completions.

## 8. Atomic Transactions

Bus must provide a single atomic read-modify-write abstraction for A/D PTE updates and AMOs. Initial serial main loop serves as execution foundation for atomicity, but interfaces and state commit points must prevent device events from inserting between normal reads/writes.

- **BUS-REQ-008**: Read and reservation establishment of LR must be one transaction, returning an opaque token consumed only by subsequent SC.
- **BUS-REQ-009**: SC must consume token, check address and width, and conditionally write within one transaction; failures must not alter target memory.
- **BUS-REQ-010**: All successful normal stores, atomic updates, and DMA writes must pass through identical overlap checks, invalidating affected reservations; failures or read-only accesses must not fake memory changes.
- **BUS-REQ-011**: Initial single-hart reservation range is fixed to the exact naturally aligned byte range of LR operand; adding harts in future requires extending to per-hart isolated monitors, rather than sharing a single token.

## 9. Acceptance Criteria

- Address map boundary tests: start address, end address, previous byte, next byte, and boundary-straddling accesses.
- Tests covering all widths, little-endian order, read-only ROM, and unmapped accesses.
- Tests covering device registration overlap, RAM size overflow, and DMA pointing to illegal regions.
- Proves absence of bypasses across CPU, MMU page table accesses, and VirtIO DMA.
