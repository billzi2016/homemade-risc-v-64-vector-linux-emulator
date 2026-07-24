# Project Constitution

## Preamble

This Constitution defines the non-negotiable principles governing `homemade-risc-v-64-vector-linux-emulator`. It constrains all architectural decisions, code implementations, testing methodologies, documentation changes, and delivery claims. Speed, demo presentation, or local convenience cannot serve as excuses to violate specifications.

## Article I: Specification Supremacy

1. Specifications confirmed by the user serve as the authoritative reference for implementation and acceptance.
2. Every code change must trace to explicit requirements and tasks.
3. When specifications are missing, conflicting, or ambiguous, pause matching implementation and request confirmation.
4. Altering tests, lowering standards, or reinterpreting terms to mask unimplemented features is prohibited.
5. Requirement changes require updating relevant specifications, impact analyses, and tasks prior to modifying implementations.

## Article II: Authenticity

1. Using Mocks, Stubs, placeholder code, fixed outputs, or host execution on behalf of guest to fake emulator capabilities is prohibited.
2. Milestone test doubles serve only to isolate implemented modules, maintaining explicit boundaries, and cannot serve as system acceptance evidence.
3. Unexecuted commands, unobserved phenomena, and unpassed tests must not be reported as successful.
4. Faking OpenSBI Banner, Linux logs, Shell, IP addresses, DNS, or `ping` results is prohibited.
5. Final acceptance must utilize real guest software and real device links.

## Article III: Complete Hardware Semantics

1. CPU state, CSRs, privilege transitions, exceptions, and interrupts must observe declared RISC-V specification versions.
2. All instruction fetches and data accesses must pass through uniform permission check, address translation, and bus access paths.
3. MMU must implement Sv39 pages, superpages, canonical addresses, permissions, A/D bits, and TLB invalidation semantics.
4. Atomic instructions must possess real observable atomic and reservation set semantics, without degrading to normal read/write combinations.
5. VirtIO must complete feature negotiation, queue validation, descriptor processing, used ring updates, and interrupt notifications.

## Article IV: Single Source of Truth

1. Identical hardware rules possess exactly one authoritative implementation.
2. Writing secondary simplified CPU, MMU, bus, or device paths for testing, booting, or demos is prohibited.
3. Shared bitfields, constants, error types, and access interfaces must be centralized.
4. All device DMA must reuse uniform physical memory access rules.
5. When duplicate logic emerges, extract stable abstractions rather than patching copies separately.

## Article V: SOLID and Clear Boundaries

1. Every module maintains exactly one primary reason to change.
2. CPU core depends on abstract bus, independent of specific UART, disk, or network card implementations.
3. Devices collaborate via well-defined MMIO and interrupt interfaces.
4. Interfaces remain minimal; callers must not depend on unrelated methods.
5. External resources inject via replaceable controlled interfaces, but replaceability must not alter hardware semantics.

## Article VI: Maintainability First

1. Code must adopt C++17 or higher explicit standard versions, avoiding large unnecessary dependencies.
2. All code files must begin with Chinese intent comments.
3. Exported functions, complex state transitions, difficult logic, and critical spec points require accurate Chinese comments.
4. Naming must express hardware semantics; un-explained magic numbers and implicit states are prohibited.
5. Errors must carry sufficient context, while avoiding treating guest recoverable errors as host process crashes.

## Article VII: Patch-Based Modifications

1. File modifications must use `apply_patch` for incremental changes.
2. Re-writing full files to mask diffs or overwrite user work is prohibited.
3. Each patch covers approved scope only, containing no unrelated refactoring.
4. Explanation before modification, user confirmation, and report after modification constitute mandatory procedures.
5. All project file references within repository must use relative paths starting from repository root; writing user directories, workspace, or machine-specific absolute paths into documentation, configs, scripts, or implementations is prohibited.
6. System interfaces defined by platform standards like `/dev/net/tun` may be described by system absolute paths, but must be explicitly distinguished from repository file paths without introducing dependencies on user working directories.

## Article VII-A: Workspace Isolation & Host Safety

1. By default, all reading, modifying, generating, building, testing, and temporary artifacts must be restricted to repository root directory and its subdirectories.
2. Modifying, moving, deleting, or overwriting user files, application data, and system files outside repository is prohibited.
3. Altering host environment variables, shell configs, software installations, system services, network devices, routing, bridges, firewalls, DNS, and kernel parameters without authorization is prohibited.
4. Any recursive or data-destructive operations must not target user home directory, filesystem root directory, unresolved variables, or broad wildcards.
5. If real TAP, network configuration, external downloads, or remote repository operations genuinely require crossing workspace boundaries, explain precise target, necessity, state changes, and restoration procedures item-by-item, obtaining explicit user confirmation for that operation.
6. Permissions outside workspace cannot be inferred from "completing project", "running tests", or general authorizations; without explicit confirmation, stop at safety boundaries.

## Article VIII: Real and Layered Verification

1. Unit tests verify local semantics, integration tests verify real module combinations, and system tests verify real software stacks.
2. Tests must cover normal, boundary, illegal, permission, and exception paths.
3. Reference models or standard test suites serve as comparisons, but cannot replace actual emulator execution.
4. Final acceptance must sequentially observe OpenSBI, Linux, Shell, DHCP, and public ICMP.
5. Environmentally constrained items must remain incomplete; passing tests by skipping is prohibited.

## Article IX: External Artifacts Not Tracked

1. OpenSBI, Linux kernel, rootfs, disk images, download caches, and build outputs must not be committed to Git.
2. External artifacts must possess version, official source, license information, and cryptographic checksums.
3. Obtaining external artifacts requires user confirmation; network download failures cannot be substituted with unverified files.
4. `.gitignore` must explicitly cover convention directories during implementation, though modifications require separate confirmation.

## Article X: Honest Task Status

1. `tasks.md` is a progress tracker, not a wishlist.
2. Tasks are checked off only after satisfying all completion criteria.
3. Code existing without required real test verification leaves tasks incomplete.
4. Partial completion must record remaining work, without inferring full completion proportionally.
5. All final claims must be verifiable by commands, logs, test reports, or human observation.

## Article XI: Git Discipline

1. Executing Git operations without explicit user requests is prohibited.
2. Explain purpose and impact step-by-step in Chinese prior to Git operations.
3. Commit messages use Chinese, with concise subject, body explaining reasons and key changes, total length not exceeding 10 lines.
4. Commits maintain logical integrity and reviewability, without mixing unrelated changes.
5. Rolling back, overwriting, or cleaning up user changes without authorization is prohibited.

## Article XII: Amendment Rules

Any modification to this Constitution must:

1. Explain reason for change and affected specs, tasks, and implementations.
2. Obtain explicit user confirmation.
3. Not retroactively declare non-compliant implementations compliant.
4. Synchronously update `specs/README.md`, `specs/tasks.md`, or relevant specialized specs.

## Effective Status

This Constitution takes effect upon user confirmation and committing to repository. It defines engineering behaviors, and does not imply project features are already implemented or verified.
