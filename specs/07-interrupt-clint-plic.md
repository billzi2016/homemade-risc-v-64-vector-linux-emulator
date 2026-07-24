# CLINT, PLIC & Interrupt Specification

## 1. Interrupt System

Initial single-hart platform supports at least Machine software interrupts, Machine timer interrupts, Machine external interrupts, and corresponding Supervisor interrupt paths delegated via `mideleg`. Pending, enable, global enables, delegation, and priority jointly determine final Traps.

## 2. CLINT

- **INT-REQ-001**: CLINT is mapped at `0x02000000..0x0200BFFF`.
- **INT-REQ-002**: Single-hart provides at least `msip`, `mtimecmp`, and `mtime` compatible register layout.
- **INT-REQ-003**: MTIP is set when `mtime >= mtimecmp`; cleared after writing a larger `mtimecmp`.
- **INT-REQ-004**: `msip` valid bit controls MSIP, with remaining write bits ignored or preserved per spec policy.
- **INT-REQ-005**: `mtime` source is monotonic, with frequency matching FDT `timebase-frequency`.

When 32-bit split access to 64-bit counter registers occurs, software atomic update sequences used by Linux must be handled without generating uncontrolled transient interrupts during intermediate low-half writes.

## 3. PLIC Interrupt Sources

Assign stable, non-zero source IDs written into FDT for at least:

- UART 16550A.
- VirtIO-Blk.
- VirtIO-Net.

ID 0 permanently means "no interrupt" and cannot be assigned to devices. Specific IDs are frozen in boot spec and maintain a single source of constant truth.

## 4. PLIC State

- **INT-REQ-006**: Implement per-source priority, pending bits, and per-context enable bits.
- **INT-REQ-007**: Implement per-context threshold and claim/complete.
- **INT-REQ-008**: Claim returns highest-priority enabled pending source exceeding threshold; ties favor smaller IDs.
- **INT-REQ-009**: Claim produces spec-defined pending/in-service state changes for selected source; complete accepts only valid sources currently processed by matching context.
- **INT-REQ-010**: Level-triggered sources must remain visible after completion while conditions remain true, without permanent loss.

Initial release provides at least M-mode and S-mode external interrupt contexts, enabling OpenSBI to delegate platform interrupts to Linux.

## 5. MMIO Behavior

- Allow access widths and natural alignments supported by PLIC spec only.
- Unimplemented source register bits read zero and ignore writes, without affecting other bits.
- Priority 0 disables source; maximum priority is a fixed WARL value in machine model.
- Complete writes with invalid claim IDs are safely ignored per spec and can output controlled diagnostics.

## 6. Device Interrupt Interfaces

Devices do not write `mip/sip` directly. Devices assert/deassert their own interrupt lines, aggregated by PLIC to drive MEIP/SEIP. CLINT separately drives software and timer pending bits. CPU samples these lines at unified synchronization points and updates visible CSR state.

## 7. Interrupt Selection

- Synchronous exceptions and interrupts compete only at defined instruction boundaries.
- Interrupts must first satisfy local enable and matching global enable rules.
- High-privilege target interrupts preempt execution in lower privilege modes per spec, without incorrectly depending on lower-privilege global bits.
- When multiple interrupts are simultaneously available, priority specified by privileged spec is used.
- Selection result is handed to the unified Trap entry, disallowing each device from jumping PC independently.

## 8. Acceptance Criteria

- Verifies setting and clearing of `mtime` crossing `mtimecmp`.
- Verifies software interrupt write, clear, and delegation paths.
- Verifies all combinations of PLIC priority/enable/threshold and tie-breaking arbitration.
- Verifies claim/complete, level re-triggering, and multiple device concurrent pending signals.
- Verifies final `mcause/scause`, `mepc/sepc`, `tvec` entries, and interrupt enable stacks.
