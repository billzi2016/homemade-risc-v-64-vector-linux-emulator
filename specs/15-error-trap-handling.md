# Exception, Trap & Error Handling Specification

## 1. Error Domains

The system must distinguish:

1. **Guest Synchronous Exceptions**: Illegal instruction, breakpoint, system calls, address misaligned, access fault, page fault.
2. **Guest Asynchronous Interrupts**: Software, timer, external device interrupts.
3. **Device Request Errors**: Invalid VirtIO chains, disk out-of-bounds, unsupported requests.
4. **Host Runtime Errors**: File, TAP, terminal, or memory allocation failures.
5. **Emulator Internal Defects**: Invariant violations and impossible states.

These errors must not be uniformly converted into process exits or simple boolean failures.

## 2. Synchronous Exception Requirements

- **TRAP-REQ-001**: Exceptions must associate with the original instruction PC that caused the exception.
- **TRAP-REQ-002**: `tval` for illegal instructions stores instruction bits or zero per frozen spec, with rules consistently testable for 16/32-bit instructions.
- **TRAP-REQ-003**: Address misaligned, access fault, and page fault save corresponding faulting virtual addresses.
- **TRAP-REQ-004**: `ECALL` cause depends on originating privilege level.
- **TRAP-REQ-005**: Exception occurrence must not commit disallowed destination registers, PC, or memory side effects per spec.

Precise exception exceptions for vector element-by-element instructions are controlled by RVV `vstart` semantics, permitting completed elements to remain.

## 3. Standard Cause Ranges

Correctly process at least:

- Instruction address misaligned / access fault / illegal instruction / breakpoint.
- Load address misaligned / access fault.
- Store/AMO address misaligned / access fault.
- ECALL from U / S / M.
- Instruction / load / store page fault.
- Supervisor / Machine software, timer, external interrupts.

Cause encodings and the interrupt MSB must be defined centrally, without scattered magic numbers.

## 4. Delegation and Target Selection

- Traps originating in M-mode do not delegate to lower privilege levels.
- Synchronous exceptions originating in S/U modes select M or S targets based on `medeleg`.
- Interrupts select targets based on `mideleg`, target enable, global bits, and current privilege.
- Delegation affects target CSRs and vectors, and should not incorrectly modify another level `epc/cause/tval`.
- Multiple receivable interrupts select one per privilege spec priority.

## 5. Trap Entry

Entering M-mode Trap:

1. `mepc` saves faulting / interrupted PC.
2. `mcause/mtval` write cause and additional value.
3. `MPIE <- MIE`, `MIE <- 0`, `MPP <- previous privilege level`.
4. Current mode switches to M.
5. PC is set to entry point calculated from `mtvec`.

Entering S-mode uses corresponding `sepc/scause/stval/SPIE/SIE/SPP/stvec`. Entry point addresses must validate mode fields and alignment WARL semantics.

## 6. Trap Return

- `MRET` is legal only in permitted modes, restoring `MIE <- MPIE`, `MPIE <- 1`, mode <- MPP, and resetting MPP/MPRV per spec.
- `SRET` is governed by current privilege and controls such as TSR, restoring S interrupt stack and mode.
- Return PC originates from corresponding epc; misaligned addresses are handled per C extension state.
- Executing xRET illegally must generate illegal instructions rather than host errors.

## 7. Exception Priorities and Partial Accesses

When address calculation, alignment, translation, permissions, and physical bus potentially expose errors simultaneously, fixed priorities must be maintained per spec. Page-crossing or cross-device accesses must not leave disallowed first-half writes after second-half failures; implementations should pre-check or use roll-backable transactions.

Exception paths for LR/SC, AMO, and PTE A/D updates must remain atomic. SC return failure is not equivalent to a Store Trap.

## 8. Device and Host Errors

- When guest submits illegal block/net requests, complete per VirtIO status or require device reset as priority, without crashing host.
- Host disk or TAP permanent I/O faults must revoke/complete related requests and produce clear diagnostics; whether to continue running is defined by device specs.
- Terminal restoration failures must be reported, but must not prevent attempting to close other resources.
- Internal defect reports contain at least PC, privilege mode, instruction, key CSRs, and recent device event summaries.

## 9. Acceptance Criteria

- Every synchronous exception is initiated from M/S/U to verify delegation targets.
- Verifies complete state transitions for `epc/cause/tval/status/tvec`.
- Verifies Direct/Vectored interrupt entry and multiple pending priorities.
- Verifies page-crossing fetch/access, vector mid-way exceptions, and atomic failures without illegal side effects.
- Verifies malicious device requests do not cause host out-of-bounds access or un-restored terminals.
