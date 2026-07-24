# C++ Coding and Chinese Annotation Specification

## 1. Language and Dependencies

- **CODE-REQ-001**: Adopt C++17 or a higher standard confirmed by the user, selecting a single standard version for the entire project.
- **CODE-REQ-002**: Default to standard libraries and necessary POSIX/Linux APIs, prohibiting introduction of GUI or heavy unnecessary dependencies.
- **CODE-REQ-003**: Enable strict compiler warnings for builds, treating project code warnings as errors; third-party code must not affect project by globally disabling warnings.
- **CODE-REQ-004**: Avoid host undefined behavior, including integer overflow, illegal shifts, unaligned pointer dereferences, and aliasing violations.

## 2. File Intent Comments

Every `.hpp`, `.cpp`, and subsequent script file must include top-level Chinese intent comments at the start, explaining at least:

```text
File Responsibility: What this file is solely responsible for.
Boundaries: What it is not responsible for, preventing scope creep.
Key Dependencies: Which stable abstractions it depends on.
Key Invariants: Which hardware/resource rules it maintains that must always hold true.
```

Do not write vague descriptions like "utility class" or "common logic" that fail to constrain boundaries.

## 3. Function and Type Comments

Exported types, interfaces, functions, and complex private functions must explain in Chinese:

- Call purpose and hardware semantics.
- Parameter units, address spaces, bit widths, ownership, and legal ranges.
- Return values and error types.
- Modified architectural states, memory, devices, or host resources.
- Exception safety, atomicity, thread/event-loop constraints.
- Correspondence to potentially confusing behaviors in specifications.

Simple accessors may use a single accurate comment line; complex page table walks or descriptor parsing must fully describe preconditions and post-failure states.

## 4. Critical Logic Comments

The following positions must explain "why":

- Instruction bitfield concatenation, immediate sign extension, and reserved encoding evaluation.
- CSR aliases, WARL, privilege and interrupt stack changes.
- Sv39 superpage synthesis, permission matrices, A/D atomic updates, and TLB flushes.
- Atomic instruction reservation sets and invalidation timing.
- Floating-point rounding, NaN boxing, and exception flags.
- RVV register grouping, tail/mask, overlap, and `vstart`.
- Virtqueue ring wraparounds, descriptor directions, submission order, and interrupt suppression.
- Terminal restoration, TAP non-blocking I/O, and resource cleanup.

Comments cannot merely translate code expressions into Chinese word-by-word, nor cite non-existent future implementations.

## 5. Naming and Types

- Type and function names express architectural concepts, such as virtual address, physical address, access type, privilege mode.
- Use distinct strong types or clean encapsulations for VA, PA, register values, CSR numbers, and device offsets, minimizing mis-passings.
- Use check utilities for address additions, ranges, and sizes, prohibiting scattered hand-written overflow logic.
- Fixed-width hardware values use `<cstdint>` types, while host container sizes use appropriate size types with explicit conversions.
- Constants are centralized in corresponding specification modules, represented in hexadecimal and semantic names, noting specification sections when necessary.

## 6. SOLID Constraints

- **Single Responsibility**: Separate decoding, execution, address translation, physical dispatch, and host I/O.
- **Open/Closed**: Adding devices registers via MMIO abstractions without modifying CPU device branches.
- **Liskov Substitution**: Replacing test or platform backends must not alter interface promises and hardware semantics.
- **Interface Segregation**: Keep read-only DMA, writable DMA, interrupt source, and clock interfaces minimal.
- **Dependency Inversion**: High-level machine orchestration depends on abstractions, placing specific POSIX backends at boundary layers.

## 7. DRY Constraints

The following contents must originate from a single source of truth:

- Address maps, device interrupt IDs, and FDT node data.
- CSR numbers, bitfields, permissions, and reset values.
- cause encodings and Trap state transitions.
- Bus range/overflow checks.
- Virtqueue descriptor traversals.
- Projections of CLI configuration in machine and documentation.

DRY does not mean abstracting all similar code. Share code only when semantics and reasons for change are identical; differing spec behaviors must not be forcibly merged due to surface similarities.

## 8. State and Errors

- Commit architectural state changes via reviewable transactions where possible, leaving no illegal half-states prior to exceptions.
- Use structured errors carrying address, access type, PC, and device context.
- Guest errors feedback via Trap/device status; host errors propagate via controlled results.
- Prohibit direct `exit` calls in deep library code; runtime cleans up uniformly before deciding to exit.
- Use RAII for resources; terminal attributes and file descriptors must feature idempotent restoration/closure.

## 9. Path Rules

- Repository documentation, configs, script default values, and test evidence uniformly use relative paths from repository root.
- Prohibit writing developer usernames, user directories, or workspace absolute paths.
- System device interfaces such as `/dev/net/tun` must be explicitly noted as host platform paths.
- Runtime may accept user-passed absolute external file paths, but must not fixate them into source code or committed configs.

## 10. Code Review Checklist

- Is it traceable to requirements and tasks?
- Is there exactly one authoritative implementation path?
- Are normal, boundary, and failure states fully handled?
- Does undefined behavior, overflow, out-of-bounds, or partial side effect exist?
- Are Chinese file, function, and critical logic comments accurate and complete?
- Do tests execute production paths without using Mocks to fake acceptance?
- Are unrelated modifications or machine absolute paths included?
